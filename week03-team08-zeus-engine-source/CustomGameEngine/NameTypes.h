#pragma once
#include "EngineTypes.h"

struct FNamePool
{
	TArray<FString> NameTable;
	TArray<FString> DisplayNameTable;
	TMap<FString, int32> ComparisonMap;
	TMap<FString, int32> DisplayMap;

	static FNamePool& Get()
	{
		static FNamePool instance;
		return instance;
	}

	int32 GetOrAdd(const FString& str, int32& outDisplayIndex);
	int32 MakeNumber(int32 index) const;
	FString GetName(int32 index) const;
	const FString& GetDisplayName(int32 index) const;
};

struct FName
{
	int32 DisplayIndex = 0;
	int32 ComparisonIndex = 0;
	int32 Number = 1;

	FName() : ComparisonIndex(0), DisplayIndex(0) {}
	FName(const char* pStr);
	FName(const char* pStr, int32 number);

	int32 Compare(const FName& other) const;
	bool operator==(const FName& other) const;

	FString GetDisplayName() const;
	FString ToString() const;
};
