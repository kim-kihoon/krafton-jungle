#include "FSoftObjectProperty.h"
#include "Core/UObject/FSoftObjectPtr.h"
#include "Object/Object.h"
#include "Serialization/Archive.h"
#include "SimpleJSON/json.hpp"

UObject* FSoftObjectProperty::GetObjectPropertyValue(void* Addr) const
{
	return static_cast<FSoftObjectPtr*>(Addr)->Get();
}

void FSoftObjectProperty::SetObjectPropertyValue(void* Addr, UObject* Value) const
{
	auto* Ptr = static_cast<FSoftObjectPtr*>(Addr);

	// Type-safety check lives on the property (PropertyClass), per the UE doc:
	// "Data is dumb. Metadata is smart."
	if (!Value || (PropertyClass && !Value->IsA(PropertyClass)))
	{
		Ptr->Reset();
		return;
	}

	Ptr->SetPath(Value->GetAssetPathFileName());
	Ptr->SetCache(Value);
}

json::JSON FSoftObjectProperty::Serialize(const void* Instance) const
{
	return json::JSON(
		static_cast<const FSoftObjectPtr*>(ContainerPtrToValuePtr(Instance))->GetPath().ToString());
}

void FSoftObjectProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	static_cast<FSoftObjectPtr*>(ContainerPtrToValuePtr(Instance))->SetPath(Value.ToString());
}

void FSoftObjectProperty::SerializeItem(FArchive& Ar, void* Value, const void* Defaults) const
{
	Ar << *static_cast<FSoftObjectPtr*>(Value);
}
