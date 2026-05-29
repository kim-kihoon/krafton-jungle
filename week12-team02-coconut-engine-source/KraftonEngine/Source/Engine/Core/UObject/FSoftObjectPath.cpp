#include "Core/UObject/FSoftObjectPath.h"

#include "Object/Object.h"
#include "Serialization/Archive.h"

FArchive& operator<<(FArchive& Ar, FSoftObjectPath& Path)
{
	FString SerializedPath = Ar.IsSaving() ? Path.ToString() : FString();
	Ar << SerializedPath;

	if (Ar.IsLoading())
	{
		Path.SetPath(SerializedPath);
	}

	return Ar;
}

UObject* FSoftObjectPath::ResolveObject() const
{
	if (IsNull()) return nullptr;

	// Iterate through the GUObjectArray (Unreal does this)
	for (const FUObjectItem& Item : GUObjectArray.GetItems())
	{
		UObject* Object = Item.Object;
		if (!Object) continue;
		if (Object->GetAssetPathFileName() == AssetPathName) return Object;
	}

	return nullptr;
}
