#pragma once

#include "Object/Object.h"
#include "Asset/SkeletonTypes.h"

class USkeletonAsset : public UObject
{
public:
    DECLARE_CLASS(USkeletonAsset, UObject)

    USkeletonAsset() = default;
    ~USkeletonAsset() override;

    void SetSkeletonData(FSkeleton* InSkeletonData);

    FSkeleton* GetSkeletonData();
    const FSkeleton* GetSkeletonData() const;

    const FString& GetAssetPathFileName() const;
    const FString& GetRootNodeName() const;

    const TArray<FBoneInfo>& GetBones() const;
    const FBoneInfo* GetBoneInfo(int32 BoneIndex) const;

    const TArray<FSkeletalMeshSocket>& GetSockets() const;
    const FSkeletalMeshSocket* FindSocket(const FName& Name) const;
    bool HasSocket(const FName& Name) const;

    bool HasValidSkeletonData() const;

private:
    FSkeleton* SkeletonData = nullptr;
};
