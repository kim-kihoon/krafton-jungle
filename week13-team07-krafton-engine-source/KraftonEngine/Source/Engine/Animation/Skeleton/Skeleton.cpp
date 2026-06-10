#include "Animation/Skeleton/Skeleton.h"
void USkeleton::Serialize(FArchive& Ar)
{
    UObject::Serialize(Ar);

    Ar << AssetPathFileName;
    Ar << SkeletonAssetGuid;
    Ar << CompatibilitySignature;
    Ar << ReferenceSkeleton;

    if (Ar.IsSaving() || Ar.HasRemaining())
    {
        Ar << Sockets;
    }
    else if (Ar.IsLoading())
    {
        Sockets.clear();
    }

    if (Ar.IsLoading())
    {
        RebuildBoneNameCache();
        for (FSkeletonSocket& Socket : Sockets)
        {
            Socket.BoneIndex = ResolveSocketBoneIndex(Socket);
            if (Socket.BoneName.empty() && Socket.BoneIndex >= 0 && Socket.BoneIndex < ReferenceSkeleton.GetNumBones())
            {
                Socket.BoneName = ReferenceSkeleton.Bones[Socket.BoneIndex].Name;
            }
            if (Socket.PreviewStaticMeshPath.empty())
            {
                Socket.PreviewStaticMeshPath = "None";
            }
        }
    }
}

void USkeleton::RebuildBoneNameCache()
{
    BoneNameToIndex.clear();

    for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNumBones(); ++BoneIndex)
    {
        BoneNameToIndex[ReferenceSkeleton.Bones[BoneIndex].Name] = BoneIndex;
    }
}

int32 USkeleton::FindBoneIndex(const FString& BoneName) const
{
    auto It = BoneNameToIndex.find(BoneName);
    if (It != BoneNameToIndex.end())
    {
        return It->second;
    }

    return ReferenceSkeleton.FindBoneIndex(BoneName);
}


int32 USkeleton::FindSocketIndex(const FName& SocketName) const
{
    if (SocketName == FName::None)
    {
        return -1;
    }

    for (int32 SocketIndex = 0; SocketIndex < static_cast<int32>(Sockets.size()); ++SocketIndex)
    {
        if (Sockets[SocketIndex].Name == SocketName)
        {
            return SocketIndex;
        }
    }

    return -1;
}

const FSkeletonSocket* USkeleton::FindSocket(const FName& SocketName) const
{
    const int32 SocketIndex = FindSocketIndex(SocketName);
    return SocketIndex >= 0 ? &Sockets[SocketIndex] : nullptr;
}

FSkeletonSocket* USkeleton::FindSocket(const FName& SocketName)
{
    const int32 SocketIndex = FindSocketIndex(SocketName);
    return SocketIndex >= 0 ? &Sockets[SocketIndex] : nullptr;
}

bool USkeleton::HasSocket(const FName& SocketName) const
{
    return FindSocket(SocketName) != nullptr;
}

int32 USkeleton::ResolveSocketBoneIndex(const FSkeletonSocket& Socket) const
{
    if (!Socket.BoneName.empty())
    {
        return FindBoneIndex(Socket.BoneName);
    }

    if (Socket.BoneIndex >= 0 && Socket.BoneIndex < ReferenceSkeleton.GetNumBones())
    {
        return Socket.BoneIndex;
    }

    return -1;
}
