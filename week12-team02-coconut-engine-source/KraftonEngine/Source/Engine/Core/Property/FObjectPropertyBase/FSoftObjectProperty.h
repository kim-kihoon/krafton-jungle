#pragma once  
#include "FObjectPropertyBase.h"  

// ---------------------------------------------------------------------------------------------------
// About
// 
// FSoftObjectProperty is the reflection class responsible for managing Indirect References to assets.
// Its primary purpose is to allow an Actor to “know about” an asset
// (via a string path) without forcing that asset to be loaded into memory.
// ---------------------------------------------------------------------------------------------------

class FSoftObjectProperty final : public FObjectPropertyBase  
{  
public:  
	FSoftObjectProperty(const FString& InName, const FString& InCategory,  
		uint32 InFlag, uint32 InOffset, uint32 InSize,  
		UClass* InPropertyClass)  
		: FObjectPropertyBase(InName, InCategory, InFlag, InOffset, InSize, InPropertyClass) 
	{   
	}  

	EPropertyType GetType() const override { return EPropertyType::SoftObject; }  
	json::JSON Serialize(const void* Instance) const override;        // FString path to JSON  
	void Deserialize(void* Instance, const json::JSON& Value) const override;  
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;

	// In Unreal Engine 5.6, calling GetObjectPropertyValue on an FSoftObjectProperty returns a UObject* to the referenced asset, 
	// but only if that asset is already loaded in memory. If the asset is not loaded, it returns nullptr.
	UObject* GetObjectPropertyValue(void* PropertyMemoryAddress) const override;  
	void SetObjectPropertyValue(void* PropertyMemoryAddress, UObject* Value) const override;  
};