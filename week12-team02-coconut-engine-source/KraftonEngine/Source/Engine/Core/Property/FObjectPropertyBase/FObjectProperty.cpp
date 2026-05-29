#include "FObjectProperty.h"
#include "Core/UObject/FObjectPtr.h"
#include "Serialization/Archive.h"
#include "SimpleJSON/json.hpp"

// TODO(PIE): FObjectProperty does not yet survive PIE duplication. Both the
// JSON path (FindObjectByPath) and the binary path (FLinkerSave/Load's path
// table) key references by GetPathName(), which encodes the source object's
// outer world. After duplication, PIE-world objects have new paths, so refs
// either resolve to the editor-world original (identity bug) or to null.
// Fix requires a duplicate-specific FArchive subclass (Unreal-style
// FArchiveUObject + old->new pointer fixup map) that overrides
// operator<<(UObject*&) to substitute mapped pointers in-scope and pass
// out-of-scope refs through unchanged.

UObject* FObjectProperty::GetObjectPropertyValue(void* Addr) const
{
	return static_cast<FObjectPtr*>(Addr)->Get();
}

void FObjectProperty::SetObjectPropertyValue(void* Addr, UObject* Value) const
{
	auto* Ptr = static_cast<FObjectPtr*>(Addr);
	if (!Value || (PropertyClass && !Value->IsA(PropertyClass)))
	{
		Ptr->Reset();
		return;
	}
	*Ptr = Value;
}

json::JSON FObjectProperty::Serialize(const void* Instance) const
{
	UObject* Obj = static_cast<const FObjectPtr*>(ContainerPtrToValuePtr(Instance))->Get();
	return json::JSON(Obj ? Obj->GetPathName() : FString("None"));
}

void FObjectProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	UObject* Resolved = FindObjectByPath(Value.ToString());
	SetObjectPropertyValue(ContainerPtrToValuePtr(Instance), Resolved);
}

// TODO: Implement FLinker subclass of FArchive
void FObjectProperty::SerializeItem(FArchive& Ar, void* Value, const void* /*Defaults*/) const
{
	UObject* Obj = static_cast<FObjectPtr*>(Value)->Get();
	Ar << Obj;
	if (Ar.IsLoading())
	{
		SetObjectPropertyValue(Value, Obj);
	}
}