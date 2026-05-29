#pragma once
#include "UField.h"

enum class ECppForm
{
	Regular,
	NameSpaced,
	EnumClass,
};

class UEnum : public UField
{
public:
	UEnum(const char* InName, uint32 InUnderlyingSize = sizeof(int32), ECppForm InForm = ECppForm::EnumClass);

	void AddEnumerator(const char* InName, int64 InValue);
	uint32 NumEnums() const { return static_cast<uint32>(DisplayNames.size()); }
	const char* GetNameByIndex(uint32 Index) const;
	int64 GetValueByIndex(uint32 Index) const;
	FString GetNameByValue(int64 Value) const;
	int32 GetIndexByValue(int64 Value) const;
	int64 GetValueByName(FString Name) const;
	int64 GetMaxEnumValue() const;
	uint32 GetUnderlyingSize() const { return UnderlyingSize; }

	const std::string& GetCppType() const { return CppType; }
	void SetCppType(const std::string& InCppType) { CppType = InCppType; }

	static UClass* StaticClass() { return &StaticClassInstance; }
	UClass* GetClass() const override { return StaticClass(); }

public:
	static UClass StaticClassInstance;

private:
	TArray<TPair<FString, int64>> DisplayNames;
	FString CppType;
	uint32 UnderlyingSize = sizeof(int32);
	ECppForm CppForm;
};
