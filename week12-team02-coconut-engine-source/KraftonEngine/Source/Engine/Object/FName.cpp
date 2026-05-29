#include "FName.h"
#include <algorithm>
#include <cctype>
#include <cstring>

// ============================================================
// FNamePool
// ============================================================
uint32 FNamePool::Store(const FString& InString)
{
	auto It = LookupMap.find(InString);
	if (It != LookupMap.end())
	{
		return It->second;
	}
	uint32 Index = static_cast<uint32>(Entries.size());
	Entries.push_back(InString);
	LookupMap[InString] = Index;
	return Index;
}

const FString& FNamePool::Resolve(uint32 Index) const
{
	static const FString Empty;
	if (Index < static_cast<uint32>(Entries.size()))
	{
		return Entries[Index];
	}
	return Empty;
}

// ============================================================
// FName
// ============================================================
static void ToLower(FString& Str)
{
    for (char& C : Str)
        C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
}

const FName FName::None = "Name_None";

FName::FName()
	: ComparisonIndex(0)
	, DisplayIndex(0)
{
}

FName::FName(const char* InName) : FName(InName ? FString(InName) : FString())
{
}

FName::FName(const FString& InName)
{
	if (InName.empty())
	{
		ComparisonIndex = 0;
		DisplayIndex = 0;
		return;
	}

	FNamePool& Pool = FNamePool::Get();
	DisplayIndex = Pool.Store(InName);

	FString LowerStr = InName;
	ToLower(LowerStr);

	ComparisonIndex = Pool.Store(LowerStr);
}

bool FName::operator==(const FName& Other) const
{
	return ComparisonIndex == Other.ComparisonIndex;
}

bool FName::operator!=(const FName& Other) const
{
	return ComparisonIndex != Other.ComparisonIndex;
}

size_t FName::Hash::operator()(const FName& Name) const
{
	return std::hash<uint32>()(Name.ComparisonIndex);
}

FString FName::ToString() const
{
	return FNamePool::Get().Resolve(DisplayIndex);
}

bool FName::IsValid() const
{
	return DisplayIndex != 0 || ComparisonIndex != 0;
}

FString FName::NameToDisplayString(const FString& InName, bool bIsBool)
{
	const char* Chars = InName.c_str();
	const int32 Length = static_cast<int32>(InName.length());

	// Run = 대문자/숫자 연속 구간. "Draw Scale 3D" 가 "Draw Scale 3 D" 로 쪼개지지 않도록 유지.
	bool bInARun        = false;
	bool bWasSpace      = false;
	bool bWasOpenParen  = false;
	bool bWasNumber     = false;
	bool bWasMinusSign  = false;

	FString OutDisplayName;
	OutDisplayName.reserve(static_cast<size_t>(Length));

	for (int32 CharIndex = 0; CharIndex < Length; ++CharIndex)
	{
		char ch = Chars[CharIndex];
		const unsigned char uch = static_cast<unsigned char>(ch);

		const bool bLowerCase     = std::islower(uch) != 0;
		const bool bUpperCase     = std::isupper(uch) != 0;
		const bool bIsDigit       = std::isdigit(uch) != 0;
		const bool bIsUnderscore  = (ch == '_');

		// Bool 프로퍼티 (예 bUseRaycastSuspension) 의 선두 'b' 제거 — 두 번째 글자가 대문자일 때만.
		if (CharIndex == 0 && bIsBool && ch == 'b')
		{
			if (Length > 1 && std::isupper(static_cast<unsigned char>(Chars[1])))
			{
				continue;
			}
		}

		// 대문자/숫자가 새로 시작되면 그 앞에 공백 삽입.
		// 단, 숫자 표현식 ("-1.2") 은 분해하지 않는다.
		if ((bUpperCase || (bIsDigit && !bWasMinusSign)) && !bInARun && !bWasOpenParen && !bWasNumber)
		{
			if (!bWasSpace && !OutDisplayName.empty())
			{
				OutDisplayName += ' ';
				bWasSpace = true;
			}
			bInARun = true;
		}

		// 소문자는 run 을 끊는다.
		if (bLowerCase)
		{
			bInARun = false;
		}

		// 밑줄 -> 공백 치환, run 유지.
		if (bIsUnderscore)
		{
			ch = ' ';
			bInARun = true;
		}

		// 출력 첫 글자는 무조건 대문자.
		if (OutDisplayName.empty())
		{
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}
		else if (!bIsDigit && (bWasSpace || bWasOpenParen))
		{
			// 공백/( 직후 첫 글자 케이스 보정.
			// 일부 관사/전치사는 소문자로 강제 ("Add To Cart" → "Add to Cart"),
			// 그 외는 대문자.
			static const char* const Articles[] =
			{
				"In", "As", "To", "Or", "At", "On", "If", "Be", "By",
				"The", "For", "And", "With", "When", "From",
			};
			constexpr size_t ArticleCount = sizeof(Articles) / sizeof(Articles[0]);

			bool bIsArticle = false;
			for (size_t i = 0; i < ArticleCount; ++i)
			{
				const int32 ArticleLength = static_cast<int32>(std::strlen(Articles[i]));

				// 관사 뒤 문자가 소문자면 매칭하지 않는다 ("In" 이 "Instance" 에 매칭되면 안 됨).
				if ((Length - CharIndex) > ArticleLength
					&& !std::islower(static_cast<unsigned char>(Chars[CharIndex + ArticleLength]))
					&& Chars[CharIndex + ArticleLength] != '\0')
				{
					if (std::strncmp(&Chars[CharIndex], Articles[i], static_cast<size_t>(ArticleLength)) == 0)
					{
						bIsArticle = true;
						break;
					}
				}
			}

			ch = bIsArticle
				? static_cast<char>(std::tolower(static_cast<unsigned char>(ch)))
				: static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}

		bWasSpace     = (ch == ' ');
		bWasOpenParen = (ch == '(');
		bWasMinusSign = (ch == '-');

		// "-1.2" 같은 표현식을 한 덩어리로 본다.
		const bool bPotentialNumericalChar = bWasMinusSign || (ch == '.');
		bWasNumber = bIsDigit || (bWasNumber && bPotentialNumericalChar);

		OutDisplayName += ch;
	}

	return OutDisplayName;
}