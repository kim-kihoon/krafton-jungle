#pragma once
#include "PropertyTypes.h"
#include "Object/ScriptStruct.h"

class FStructProperty final : public FProperty
{
public:
	FStructProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		UScriptStruct* InScriptStruct)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
		, ScriptStruct(InScriptStruct)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Struct; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;

	const TArray<FProperty*>& GetStructProperties() const
	{
		if (ScriptStruct)
		{
			return ScriptStruct->GetProperties();
		}

		static const TArray<FProperty*> Empty;
		return Empty;
	}

public:
	UScriptStruct* ScriptStruct = nullptr;
};

