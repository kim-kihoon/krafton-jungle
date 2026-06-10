#pragma once

#include "Object/GarbageCollection.h"

#include "Asset/AssetRegistry.h"
#include "Animation/Skeleton/SkeletonTypes.h"
#include "Core/Types/CoreTypes.h"

class UPhysicsAsset;
class USkeletalMesh;

class FPhysicsAssetManager : public FGCObject
{
public:
    static FPhysicsAssetManager& Get();

    static bool IsPhysicsAssetPackage(const FString& PackagePath);

    UPhysicsAsset* LoadPhysicsAsset(const FString& PackagePath);
    UPhysicsAsset* CreatePhysicsAssetForSkeleton(const USkeletalMesh* SkeletalMesh, const FString& PackagePath = FString());

    bool SavePhysicsAsset(UPhysicsAsset* PhysicsAsset, const FString& PackagePath, const FString& SourcePath = FString());
    bool SavePhysicsAssetPreservingMetadata(UPhysicsAsset* PhysicsAsset);

    const TArray<FAssetListItem>& GetAvailablePhysicsAssetFiles();
    void                          ScanPhysicsAssets();

    TArray<FAssetListItem> FindPhysicsAssetsForSkeleton(const FSkeletonBinding& Skeleton, bool bAllowSameStructure = false);

    static FString BuildDefaultPhysicsAssetPackagePath(const FString& SkeletonPackagePath);

    const char* GetReferencerName() const override { return "FPhysicsAssetManager"; }
    void AddReferencedObjects(FReferenceCollector& Collector) override;

    void ClearCache();

private:
    FPhysicsAssetManager() = default;

private:
    TMap<FString, UPhysicsAsset*> PhysicsAssetCaches;
    TArray<FAssetListItem>        AvailablePhysicsAssetFiles;
};
