#pragma once
#include "PropertyTypes.h"

class FArrayProperty final : public FProperty
{
public:
	FArrayProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		std::unique_ptr<FProperty> InInner,
		FArrayAccessor* InAccessor)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
		, Inner(std::move(InInner))
		, Accessor(InAccessor)
	{
	}

	std::unique_ptr<FProperty> Inner;
	FArrayAccessor* Accessor = nullptr;

	EPropertyType GetType() const override { return EPropertyType::Array; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};
