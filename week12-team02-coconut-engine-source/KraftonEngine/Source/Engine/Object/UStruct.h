#pragma once
#include "Object/UField.h"
#include "Core/CoreTypes.h"
#include "Core/Property/PropertyTypes.h"
#include <cstring>
#include <unordered_set>

class UClass;

class UStruct : public UField {
public:
	virtual ~UStruct() {
		for (FProperty* Property : ChildProperties)
		{
			delete Property;
		}
		ChildProperties.clear();
	}

	UStruct() = default;
	UStruct(const char* InName, UStruct* InSuperStruct, size_t InSize)
		: UField(InName), SuperStruct(InSuperStruct), Size(InSize)
	{
	}

	bool IsChildOf(const UStruct* Other);

	UStruct*	GetSuperStruct()	const { return SuperStruct; }
	size_t      GetSize() const { return Size; }

	const TArray<FProperty*>& GetProperties() const { return ChildProperties; }
	FProperty*				  GetProperty(uint32 Index) const { return Index < ChildProperties.size() ? ChildProperties[Index] : nullptr; }
	void					  AddProperty(FProperty* InProperty) { if (InProperty) ChildProperties.push_back(InProperty); }

	// Hide the property from both the editor AND the serializing process
	void HideInheritedProperty(FString InName);
	bool IsPropertyHidden(FString InName) const;

	// Base-to-derived order, matching the previous enumeration behavior.
	void GetAllProperties(TArray<const FProperty*>& OutProps) const
	{
		if (SuperStruct) SuperStruct->GetAllProperties(OutProps);
		for (const FProperty* P : ChildProperties)
		{
			if (P) OutProps.push_back(P);
		}
	}

	void GetEditableProperties(TArray<const FProperty*>& OutProps) const;
	void GetNonTransientProperties(TArray<const FProperty*>& OutProps) const;

	// 이름으로 프로퍼티 룩업. 자기 클래스 → 베이스 순서로 검색.
	const FProperty* FindPropertyByName(const char* InName) const
	{
		if (!InName) return nullptr;
		for (const FProperty* P : ChildProperties)
		{
			if (P && std::strcmp(P->Name.c_str(), InName) == 0) return P;
		}
		return SuperStruct ? SuperStruct->FindPropertyByName(InName) : nullptr;
	}

	static UClass* StaticClass() { return &StaticClassInstance; }
	UClass* GetClass() const override { return StaticClass(); }


public:
	static UClass StaticClassInstance;


protected:
	void GetEditablePropertiesFor(TArray<const FProperty*>& OutProps, const UStruct* Target) const;
	void GetNonTransientPropertiesFor(TArray<const FProperty*>& OutProps, const UStruct* Target) const;


protected:
	UStruct*	SuperStruct	= nullptr;
	size_t      Size		= 0;

	// Make this into a linked list once implementing UField and TFieldIterator
	TArray<FProperty*> ChildProperties;
	std::unordered_set<FString> HiddenProperties;
};
