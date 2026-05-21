#pragma once
#include "Object/Object.h"
#include "SkeletalMeshTypes.h"

class USkeletonAsset;

class USkeletalMesh : public UObject
{
public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void SetMeshData(FSkeletalMesh* InMeshData);
    void SetSkeletonAsset(USkeletonAsset* InSkeletonAsset);

    FSkeletalMesh* GetMeshData();
    const FSkeletalMesh* GetMeshData() const;
    USkeletonAsset* GetSkeletonAsset();
    const USkeletonAsset* GetSkeletonAsset() const;

    const FString& GetAssetPathFileName() const;
    const FString& GetSkeletonSourcePath() const;

    const TArray<FSkeletalMeshVertex>& GetVertices() const;
    const TArray<uint32>& GetIndices() const;

    const TArray<FBoneInfo>& GetBones() const;

	const FBoneInfo* GetBoneInfo(int32 BoneIndex) const;

    const FMatrix& GetLocalBindTransform(int32 BoneIndex) const;
    const FMatrix& GetGlobalBindTransform(int32 BoneIndex) const;
    const FMatrix& GetInverseBindPose(int32 BoneIndex) const;

    const TArray<FStaticMeshSection>& GetSections() const;
    const TArray<FStaticMeshMaterialSlot>& GetMaterialSlots() const;

    // Sockets
    const TArray<FSkeletalMeshSocket>& GetSockets() const;
    const FSkeletalMeshSocket*         FindSocket(const FName& Name) const;
    bool                               HasSocket(const FName& Name) const;

    const FAABB& GetLocalBounds() const;
    const FAABB& GetConservativeLocalBounds() const;

    bool HasValidMeshData() const;

private:
    bool CanUseSkeletonAssetBones() const;

    void RebuildLocalBoundsFromMeshData();
    void RebuildConservativeLocalBoundsFromMeshData();

private:
    FSkeletalMesh* MeshData = nullptr;
    USkeletonAsset* SkeletonAsset = nullptr;
};
