#pragma once
#include "FObjectPropertyBase.h"

class FArchive;
class UClass;
class UObject;

// Hard reference to a UObject. Storage at PropertyMemoryAddress is an FObjectPtr.
// Round-trips through path-name lookup; only assets (objects with a non-empty
// GetAssetPathFileName) will deserialize correctly in this first pass.
class FObjectProperty final : public FObjectPropertyBase
{
public:
	FObjectProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		UClass* InPropertyClass)
		: FObjectPropertyBase(InName, InCategory, InPropertyFlag, InOffset, InElementSize, InPropertyClass)
	{
	}

	// FProperty
	EPropertyType GetType() const override { return EPropertyType::Object; }
	json::JSON    Serialize(const void* Instance) const override;
	void          Deserialize(void* Instance, const json::JSON& Value) const override;
	void          SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const override;

	// FObjectPropertyBase
	UObject* GetObjectPropertyValue(void* Addr) const override;
	void     SetObjectPropertyValue(void* Addr, UObject* Value) const override;
};