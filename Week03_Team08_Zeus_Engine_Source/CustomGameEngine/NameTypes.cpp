#include "NameTypes.h"
#include "Object.h"

extern TArray<UObject*> GUObjectArray;

int32 FNamePool::GetOrAdd(const FString& str, int32& outDisplayIndex)
{
	FString lowerStr = str;
	for (char& c : lowerStr)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	int32 compIndex;
	auto itComp = ComparisonMap.find(lowerStr);
	if (itComp != ComparisonMap.end())
	{
		compIndex = itComp->second;
	}
	else
	{
		compIndex = static_cast<int32>(NameTable.size());
		NameTable.push_back(lowerStr);
		ComparisonMap[lowerStr] = compIndex;
	}

	auto itDisplay = DisplayMap.find(str);
	if (itDisplay != DisplayMap.end())
	{
		outDisplayIndex = itDisplay->second;
	}
	else
	{
		outDisplayIndex = static_cast<int32>(DisplayNameTable.size());
		DisplayNameTable.push_back(str);
		DisplayMap[str] = outDisplayIndex;
	}

	return compIndex;
}

int32 FNamePool::MakeNumber(int32 index) const
{
	int32 newNumber = 1;
	
	for (int i = 0; i < GUObjectArray.size(); ++i)
	{
		UObject* obj = GUObjectArray[i];
		if (obj == nullptr) continue;
		if (obj->Name.ComparisonIndex != index) continue;

		if (obj->Name.Number >= newNumber)
		{
			newNumber = obj->Name.Number + 1;
		}
	}

	return newNumber;
}

FString FNamePool::GetName(int32 index) const
{
	if (index >= 0 && index < static_cast<int32>(NameTable.size()))
	{
		return NameTable[index];
	}
	return FString();
}

const FString& FNamePool::GetDisplayName(int32 index) const
{
	static const FString empty;
	if (index >= 0 && index < static_cast<int32>(DisplayNameTable.size()))
	{
		return DisplayNameTable[index];
	}
	return empty;
}

FName::FName(const char* pStr)
{
	int32 displayIndex;
	int32 index = FNamePool::Get().GetOrAdd(pStr, displayIndex);

	this->ComparisonIndex = index;
	this->DisplayIndex = displayIndex;
	this->Number = FNamePool::Get().MakeNumber(index);
}

FName::FName(const char* pStr, int32 number)
{
	int32 displayIndex;
	int32 index = FNamePool::Get().GetOrAdd(pStr, displayIndex);

	this->ComparisonIndex = index;
	this->DisplayIndex = displayIndex;
	this->Number = number;
}

int32 FName::Compare(const FName& other) const
{
	if (ComparisonIndex < other.ComparisonIndex)
		return -1;
	else if (ComparisonIndex > other.ComparisonIndex)
		return 1;

	return 0;
}

bool FName::operator==(const FName& other) const
{
	return ComparisonIndex == other.ComparisonIndex;
}

FString FName::GetDisplayName() const
{
	return FNamePool::Get().GetDisplayName(DisplayIndex);
}

FString FName::ToString() const
{
	const FString& name = FNamePool::Get().GetDisplayName(DisplayIndex);

	if (Number == 1) return name;
	return name + "_" + std::to_string(Number);
}

