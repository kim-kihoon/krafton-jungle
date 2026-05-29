#pragma once
#include "PropertyTypes.h"
#include "Object/UEnum.h"

class FEnumProperty final : public FProperty
{
public:
	FEnumProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		UEnum* InEnum = nullptr)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
		, EnumDesc(InEnum)
	{
	}

	UEnum* GetEnum() const { return EnumDesc; }

	EPropertyType GetType() const override { return EPropertyType::Enum; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;

private:
	UEnum* EnumDesc = nullptr;
};
