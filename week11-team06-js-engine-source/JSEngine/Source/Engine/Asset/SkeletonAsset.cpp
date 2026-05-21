#include "Asset/SkeletonAsset.h"

#include "Engine/Geometry/Transform.h"

DEFINE_CLASS(USkeletonAsset, UObject)

FMatrix FSkeletalMeshSocket::GetRelativeTransform() const
{
    return FTransform(RelativeRotation, RelativeLocation, RelativeScale).ToMatrixWithScale();
}

USkeletonAsset::~USkeletonAsset()
{
    delete SkeletonData;
    SkeletonData = nullptr;
}

void USkeletonAsset::SetSkeletonData(FSkeleton* InSkeletonData)
{
    if (SkeletonData == InSkeletonData)
    {
        return;
    }

    delete SkeletonData;
    SkeletonData = InSkeletonData;
}

FSkeleton* USkeletonAsset::GetSkeletonData()
{
    return SkeletonData;
}

const FSkeleton* USkeletonAsset::GetSkeletonData() const
{
    return SkeletonData;
}

const FString& USkeletonAsset::GetAssetPathFileName() const
{
    static const FString Empty = {};
    return SkeletonData ? SkeletonData->PathFileName : Empty;
}

const FString& USkeletonAsset::GetRootNodeName() const
{
    static const FString Empty = {};
    return SkeletonData ? SkeletonData->RootNodeName : Empty;
}

const TArray<FBoneInfo>& USkeletonAsset::GetBones() const
{
    static const TArray<FBoneInfo> Empty = {};
    return SkeletonData ? SkeletonData->Bones : Empty;
}

const FBoneInfo* USkeletonAsset::GetBoneInfo(int32 BoneIndex) const
{
    if (!SkeletonData)
    {
        return nullptr;
    }

    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(SkeletonData->Bones.size()))
    {
        return nullptr;
    }

    return &SkeletonData->Bones[BoneIndex];
}

const TArray<FSkeletalMeshSocket>& USkeletonAsset::GetSockets() const
{
    static const TArray<FSkeletalMeshSocket> Empty = {};
    return SkeletonData ? SkeletonData->Sockets : Empty;
}

const FSkeletalMeshSocket* USkeletonAsset::FindSocket(const FName& Name) const
{
    if (!SkeletonData || !Name.IsValid())
    {
        return nullptr;
    }

    for (const FSkeletalMeshSocket& Socket : SkeletonData->Sockets)
    {
        if (Socket.Name == Name)
        {
            return &Socket;
        }
    }

    return nullptr;
}

bool USkeletonAsset::HasSocket(const FName& Name) const
{
    return FindSocket(Name) != nullptr;
}

bool USkeletonAsset::HasValidSkeletonData() const
{
    return SkeletonData != nullptr && !SkeletonData->Bones.empty();
}
