#include "UEnum.h"
#include "UClass.h"

UClass UEnum::StaticClassInstance(
	"UEnum",
	UField::StaticClass(),
	sizeof(UEnum),
	CF_None,
	CASTCLASS_UEnum
);

UEnum::UEnum(const char* InName, uint32 InUnderlyingSize, ECppForm InForm)
	: UField(InName)
	, UnderlyingSize(InUnderlyingSize)
	, CppForm(InForm)
{
	// FName + DeferStaticObject are handled by UField's ctor.
}

void UEnum::AddEnumerator(const char* InName, int64 InValue)
{
	if (!InName) return;
	DisplayNames.emplace_back(FString(InName), InValue);
}

const char* UEnum::GetNameByIndex(uint32 Index) const
{
	return Index < DisplayNames.size() ? DisplayNames[Index].first.c_str() : "";
}

int64 UEnum::GetValueByIndex(uint32 Index) const
{
	return Index < DisplayNames.size() ? DisplayNames[Index].second : 0;
}

FString UEnum::GetNameByValue(int64 Value) const
{
	for (const auto& Pair : DisplayNames)
	{
		if (Pair.second == Value)
		{
			return Pair.first;
		}
	}
	return FString();
}

int32 UEnum::GetIndexByValue(int64 Value) const
{
	for (uint32 Index = 0; Index < DisplayNames.size(); ++Index)
	{
		if (DisplayNames[Index].second == Value)
		{
			return static_cast<int32>(Index);
		}
	}
	return -1;
}

int64 UEnum::GetValueByName(FString Name) const
{
	for (const auto& Pair : DisplayNames)
	{
		if (Pair.first == Name)
		{
			return Pair.second;
		}
	}
	return -1;
}

int64 UEnum::GetMaxEnumValue() const
{
	int64 Max = -1;
	for (const auto& Pair : DisplayNames)
	{
		if (Pair.second > Max)
		{
			Max = Pair.second;
		}
	}
	return Max;
}
