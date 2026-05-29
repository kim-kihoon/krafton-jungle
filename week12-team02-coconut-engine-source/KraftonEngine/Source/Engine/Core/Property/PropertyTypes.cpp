#include "Core/Property/PropertyTypes.h"

#include <algorithm>
#include <cstring>

#include "Core/Property/FArrayProperty.h"
#include "Core/Property/FEnumProperty.h"
#include "Core/Property/FStructProperty.h"
#include "Object/FName.h"
#include "SimpleJSON/json.hpp"
#include "Serialization/Archive.h"
#include "Math/Vector.h"

namespace
{
	json::JSON SerializeFloatArray(const float* Values, uint32 Count)
	{
		json::JSON Array = json::Array();
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			Array.append(static_cast<double>(Values[Index]));
		}
		return Array;
	}

	void DeserializeFloatArray(float* Values, uint32 Count, const json::JSON& Value)
	{
		uint32 Index = 0;
		for (const auto& Element : Value.ArrayRange())
		{
			if (Index < Count)
			{
				Values[Index] = static_cast<float>(Element.ToFloat());
			}
			++Index;
		}
	}
}

json::JSON FBoolProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const bool*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FBoolProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<bool*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToBool();
}

void FBoolProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<bool*>(Value);
}

json::JSON FByteBoolProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const uint8_t*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(static_cast<bool>(*Value != 0));
}

void FByteBoolProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<uint8_t*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToBool() ? 1 : 0;
}

void FByteBoolProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<uint8_t*>(Value);
}

json::JSON FIntProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const int32*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FIntProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<int32*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToInt();
}

void FIntProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar.Serialize(Value, ElementSize);
}

json::JSON FFloatProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(static_cast<double>(*Value));
}

void FFloatProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	*Target = static_cast<float>(Value.ToFloat());
}

void FFloatProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<float*>(Value);
}

json::JSON FVec3Property::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return SerializeFloatArray(Value, 3);
}

void FVec3Property::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	DeserializeFloatArray(Target, 3, Value);
}

void FVec3Property::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<FVector*>(Value);
}

json::JSON FRotatorProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return SerializeFloatArray(Value, 3);
}

void FRotatorProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	DeserializeFloatArray(Target, 3, Value);
}

void FRotatorProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar.Serialize(Value, ElementSize);
}

json::JSON FVec4Property::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return SerializeFloatArray(Value, 4);
}

void FVec4Property::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<FVector4*>(Value);
}

void FVec4Property::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	DeserializeFloatArray(Target, 4, Value);
}

json::JSON FColor4Property::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return SerializeFloatArray(Value, 4);
}

void FColor4Property::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	DeserializeFloatArray(Target, 4, Value);
}

void FColor4Property::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar.Serialize(Value, ElementSize);
}

json::JSON FStringProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const FString*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FStringProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<FString*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToString();
}

void FStringProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<FString*>(Value);
}

json::JSON FScriptProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const FString*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FScriptProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<FString*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToString();
}

void FScriptProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<FString*>(Value);
}

json::JSON FSceneComponentRefProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const FString*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FSceneComponentRefProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<FString*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToString();
}

void FSceneComponentRefProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<FString*>(Value);
}

json::JSON FMaterialSlotProperty::Serialize(const void* Instance) const
{
	const auto* Slot = static_cast<const FMaterialSlot*>(ContainerPtrToValuePtr(Instance));
	json::JSON Object = json::Object();
	Object["SlotName"] = json::JSON(Slot->SlotName);
	Object["Path"] = json::JSON(Slot->Path);
	return Object;
}

void FMaterialSlotProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Slot = static_cast<FMaterialSlot*>(ContainerPtrToValuePtr(Instance));
	if (Value.hasKey("SlotName"))
	{
		Slot->SlotName = Value.at("SlotName").ToString();
	}
	if (Value.hasKey("Path"))
	{
		Slot->Path = Value.at("Path").ToString();
	}
}

void FMaterialSlotProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	FMaterialSlot* Slot = static_cast<FMaterialSlot*>(Value);
	Ar << Slot->SlotName << Slot->Path;
}

json::JSON FNameProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const FName*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(Value->ToString());
}

void FNameProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<FName*>(ContainerPtrToValuePtr(Instance));
	*Target = FName(Value.ToString());
}

void FNameProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<FName*>(Value);
}

json::JSON FEnumProperty::Serialize(const void* Instance) const
{
	const void* ValuePtr = ContainerPtrToValuePtr(Instance);
	int64 Value = 0;
	const uint32 ValueSize = EnumDesc ? EnumDesc->GetUnderlyingSize() : ElementSize;
	std::memcpy(&Value, ValuePtr, std::min<uint32>(ValueSize, sizeof(Value)));
	return json::JSON(static_cast<int32>(Value));
}

void FEnumProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	void* Target = ContainerPtrToValuePtr(Instance);
	int64 StoredValue = Value.ToInt();
	const uint32 ValueSize = EnumDesc ? EnumDesc->GetUnderlyingSize() : ElementSize;
	std::memcpy(Target, &StoredValue, std::min<uint32>(ValueSize, sizeof(StoredValue)));
}

void FEnumProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	const uint32 ValueSize = EnumDesc ? EnumDesc->GetUnderlyingSize() : ElementSize;
	Ar.Serialize(Value, ValueSize);
}

json::JSON FArrayProperty::Serialize(const void* Instance) const
{
	const void* ArrayInstance = ContainerPtrToValuePtr(Instance);
	if (!Accessor || !Inner || !ArrayInstance)
	{
		return json::JSON();
	}

	json::JSON Array = json::Array();
	const uint32 Count = Accessor->Num(ArrayInstance);
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		void* ElementInstance = Accessor->GetAt(const_cast<void*>(ArrayInstance), Index);
		Array.append(Inner->Serialize(ElementInstance));
	}
	return Array;
}

void FArrayProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	void* ArrayInstance = ContainerPtrToValuePtr(Instance);
	if (!Accessor || !Inner || !ArrayInstance)
	{
		return;
	}

	const bool bFixedSize = (PropertyFlag & CPF_FixedSize) != 0;
	if (!bFixedSize)
	{
		Accessor->Clear(ArrayInstance);
	}

	uint32 Index = 0;
	for (const auto& Element : Value.ArrayRange())
	{
		if (!bFixedSize)
		{
			Accessor->AddDefault(ArrayInstance);
		}
		else if (Index >= Accessor->Num(ArrayInstance))
		{
			break;
		}

		const uint32 TargetIndex = bFixedSize ? Index : Accessor->Num(ArrayInstance) - 1;
		void* ElementInstance = Accessor->GetAt(ArrayInstance, TargetIndex);
		Inner->Deserialize(ElementInstance, Element);
		++Index;
	}
}

void FArrayProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	if (!Accessor || !Inner || !Value)
	{
		return;
	}

	uint32 Count = Accessor->Num(Value);
	Ar << Count;

	if (Ar.IsLoading())
	{
		Accessor->Clear(Value);
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			Accessor->AddDefault(Value);
		}
	}

	const uint32 DefaultCount = Defaults ? Accessor->Num(Defaults) : 0;
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		void* ElementValue = Accessor->GetAt(Value, Index);
		const void* ElementDefaults = Index < DefaultCount
			? Accessor->GetAt(const_cast<void*>(Defaults), Index)
			: nullptr;

		Inner->SerializeItem(Ar, ElementValue, ElementDefaults);
	}
}

json::JSON FStructProperty::Serialize(const void* Instance) const
{
	const void* StructInstance = ContainerPtrToValuePtr(Instance);
	if (!StructInstance)
	{
		return json::JSON();
	}

	const TArray<FProperty*>& StructProperties = GetStructProperties();
	if (StructProperties.empty())
	{
		return json::JSON();
	}

	json::JSON Object = json::Object();
	for (const FProperty* Child : StructProperties)
	{
		if (Child)
		{
			Object[Child->Name] = Child->Serialize(StructInstance);
		}
	}
	return Object;
}

void FStructProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	void* StructInstance = ContainerPtrToValuePtr(Instance);
	if (!StructInstance)
	{
		return;
	}

	const TArray<FProperty*>& StructProperties = GetStructProperties();
	for (const FProperty* Child : StructProperties)
	{
		if (!Child || !Value.hasKey(Child->Name.c_str()))
		{
			continue;
		}

		const json::JSON& ChildValue = Value.at(Child->Name.c_str());
		Child->Deserialize(StructInstance, ChildValue);
	}
}

void FStructProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	if (!Value)
	{
		return;
	}

	for (const FProperty* Child : GetStructProperties())
	{
		if (!Child)
		{
			continue;
		}

		void* ChildValue = Child->ContainerPtrToValuePtr(Value);
		const void* ChildDefaults = Defaults ? Child->ContainerPtrToValuePtr(Defaults) : nullptr;
		Child->SerializeItem(Ar, ChildValue, ChildDefaults);
	}
}
