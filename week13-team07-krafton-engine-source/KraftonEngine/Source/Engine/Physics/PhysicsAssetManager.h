#pragma once

#include "Asset/AssetRegistry.h"
#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Object/GarbageCollection.h"

class UPhysicsAsset;

class FPhysicsAssetManager : public TSingleton<FPhysicsAssetManager>, public FGCObject
{
	friend class TSingleton<FPhysicsAssetManager>;

public:
	UPhysicsAsset* Load(const FString& Path);
	UPhysicsAsset* Find(const FString& Path) const;
	UPhysicsAsset* Reload(const FString& Path);
	bool Save(UPhysicsAsset* Asset);
	UPhysicsAsset* CreatePhysicsAsset(const FString& Path);

	void RefreshAvailablePhysicsAssets();
	const TArray<FAssetListItem>& GetAvailablePhysicsAssetFiles() const
	{
		return AvailablePhysicsAssetFiles;
	}

	const char* GetReferencerName() const override { return "FPhysicsAssetManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void ClearCache();

private:
	TMap<FString, UPhysicsAsset*> LoadedPhysicsAssets;
	TArray<FAssetListItem> AvailablePhysicsAssetFiles;
};
