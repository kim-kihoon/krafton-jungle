#pragma once

#include "Object/Object.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Animation/Skeleton/SkeletonTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include <memory>

class USkeleton;
class UPhysicsAsset;


#include "Source/Engine/Mesh/Skeletal/SkeletalMesh.generated.h"

UCLASS()
class USkeletalMesh : public UObject
{
public:
	GENERATED_BODY()
	USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void Serialize(FArchive& Ar) override;
    void SerializeLegacyPayload(FArchive& Ar);
    void SerializeCurrentPayload(FArchive& Ar, uint32 PackageVersion);
    void SerializeClothPayload(FArchive& Ar);

    const FString& GetAssetPathFileName() const
    {
        return AssetPathFileName;
    }

    void SetAssetPathFileName(const FString& InPathFileName)
    {
        AssetPathFileName = InPathFileName;
    }

    void                             SetSkeletalMeshAsset(FSkeletalMesh* InMesh);
    void                             SetSkeletalMeshAsset(std::unique_ptr<FSkeletalMesh> InMesh);
    FSkeletalMesh*                   GetSkeletalMeshAsset() const;
    void                             SetSkeletalMaterials(TArray<FSkeletalMaterial>&& InMaterials);
    const TArray<FSkeletalMaterial>& GetSkeletalMaterials() const;

    void InitResources(ID3D11Device* InDevice);

    void       SetSkeleton(USkeleton* InSkeleton);
    USkeleton* GetSkeleton() const;

    void SetSkeletonBinding(const FSkeletonBinding& InBinding);
    const FSkeletonBinding& GetSkeletonBinding() const { return SkeletonBinding; }

    // The mesh owns the default PhysicsAsset choice; components may override it later.
    void SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset);
    UPhysicsAsset* GetPhysicsAsset() const;
    void SetPhysicsAssetPath(const FString& InPath);
    const FString& GetPhysicsAssetPath() const { return PhysicsAssetPath; }
    bool ResolvePhysicsAsset();
    void ClearPhysicsAsset();

private:
    uint32 CalculateTrackedMemorySize() const;
    void   ReleaseTrackedMemory();
    void   CacheSectionMaterialIndices();
    void   SyncSkeletonBindingToAsset();
    void   SyncSkeletonBindingFromAsset();
    bool   CanUsePhysicsAsset(UPhysicsAsset* InPhysicsAsset, FSkeletonCompatibilityReport* OutReport = nullptr) const;

private:
    FString AssetPathFileName = "None";

    std::unique_ptr<FSkeletalMesh> SkeletalMeshAsset;
    TArray<FSkeletalMaterial>      SkeletalMaterials;
    bool                           bMemoryTracked    = false;
    uint32                         TrackedMemorySize = 0;

    FSkeletonBinding SkeletonBinding;
    TWeakObjectPtr<USkeleton> Skeleton;
    FString PhysicsAssetPath = "None";
    mutable TWeakObjectPtr<UPhysicsAsset> PhysicsAsset;
};
