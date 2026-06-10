#include "Animation/Skeleton/Skeleton.h"
#include "Animation/Skeleton/SkeletonManager.h"
#include "Core/Logging/Log.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"

void USkeleton::Serialize(FArchive& Ar)
{
    SerializeCurrentPayload(Ar);
}

void USkeleton::SerializeLegacyPayload(FArchive& Ar)
{
    UObject::Serialize(Ar);

    Ar << AssetPathFileName;
    Ar << SkeletonAssetGuid;
    Ar << CompatibilitySignature;
    Ar << ReferenceSkeleton;

    if (Ar.IsLoading())
    {
        DefaultPhysicsAssetPath = "None";
        DefaultPhysicsAsset.Reset();
        Sockets.clear();
        RebuildReferenceSkeletonDerivedData();
        RebuildBoneNameCache();
    }
}

void USkeleton::SerializeCurrentPayload(FArchive& Ar)
{
    UObject::Serialize(Ar);

    Ar << AssetPathFileName;
    Ar << SkeletonAssetGuid;
    Ar << CompatibilitySignature;
    Ar << DefaultPhysicsAssetPath;
    Ar << ReferenceSkeleton;
	
    uint32 SocketCount = static_cast<uint32>(Sockets.size());
    Ar << SocketCount;
    if (Ar.IsLoading())
    {
	    Sockets.resize(SocketCount);
    }
    for (FSkeletalMeshSocket& Socket : Sockets)
    {
	    Ar << Socket;
    }

    if (Ar.IsLoading())
    {
        DefaultPhysicsAsset.Reset();
        RebuildReferenceSkeletonDerivedData();
        RebuildBoneNameCache();
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

const FSkeletalMeshSocket* USkeleton::FindSocket(const FName& Name) const
{
    const int32 SocketIndex = FindSocketIndex(Name);
    return SocketIndex >= 0 ? &Sockets[SocketIndex] : nullptr;
}

bool USkeleton::HasSocket(const FName& Name) const
{
    return FindSocketIndex(Name) >= 0;
}

int32 USkeleton::FindSocketIndex(const FName& Name) const
{
    if (Name == FName::None || !Name.IsValid())
    {
        return -1;
    }

    for (int32 SocketIndex = 0; SocketIndex < static_cast<int32>(Sockets.size()); ++SocketIndex)
    {
        if (Sockets[SocketIndex].Name == Name)
        {
            return SocketIndex;
        }
    }

    return -1;
}

bool USkeleton::ResolveSocketBoneIndex(const FSkeletalMeshSocket& Socket, int32& OutBoneIndex) const
{
    OutBoneIndex = FindBoneIndex(Socket.BoneName.ToString());
    return OutBoneIndex >= 0;
}

void USkeleton::RebuildReferenceSkeletonDerivedData()
{
    for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNumBones(); ++BoneIndex)
    {
        FReferenceBone& Bone = ReferenceSkeleton.Bones[BoneIndex];
        const int32 ParentIndex = Bone.ParentIndex;
        Bone.GlobalBindPose = (ParentIndex >= 0 && ParentIndex < BoneIndex)
            ? Bone.LocalBindPose * ReferenceSkeleton.Bones[ParentIndex].GlobalBindPose
            : Bone.LocalBindPose;
        Bone.InverseBindPose = Bone.GlobalBindPose.GetInverse();
    }
}

void USkeleton::SetDefaultPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
    if (!InPhysicsAsset)
    {
        ClearDefaultPhysicsAsset();
        return;
    }

    const FSkeletonCompatibilityReport Report =
        FSkeletonManager::CheckCompatibility(GetSkeletonBinding(), InPhysicsAsset->GetSkeletonBinding());
    if (Report.Result != ESkeletonCompatibilityResult::ExactSkeleton)
    {
        UE_LOG("SetDefaultPhysicsAsset rejected: skeleton mismatch. Skeleton=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            InPhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        ClearDefaultPhysicsAsset();
        return;
    }

    DefaultPhysicsAsset = InPhysicsAsset;
    DefaultPhysicsAssetPath = InPhysicsAsset->GetAssetPathFileName().empty()
        ? FString("None")
        : InPhysicsAsset->GetAssetPathFileName();
}

UPhysicsAsset* USkeleton::GetDefaultPhysicsAsset() const
{
    if (DefaultPhysicsAsset.Get())
    {
        const FSkeletonCompatibilityReport Report =
            FSkeletonManager::CheckCompatibility(GetSkeletonBinding(), DefaultPhysicsAsset->GetSkeletonBinding());
        if (Report.Result == ESkeletonCompatibilityResult::ExactSkeleton)
        {
            return DefaultPhysicsAsset.Get();
        }

        UE_LOG("GetDefaultPhysicsAsset cleared incompatible cached PhysicsAsset. Skeleton=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            DefaultPhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        const_cast<USkeleton*>(this)->ClearDefaultPhysicsAsset();
        return nullptr;
    }

    if (DefaultPhysicsAssetPath.empty() || DefaultPhysicsAssetPath == "None")
    {
        return nullptr;
    }

    USkeleton* MutableThis = const_cast<USkeleton*>(this);
    return MutableThis->ResolveDefaultPhysicsAsset() ? DefaultPhysicsAsset.Get() : nullptr;
}

void USkeleton::SetDefaultPhysicsAssetPath(const FString& InPath)
{
    DefaultPhysicsAssetPath = InPath.empty() ? FString("None") : InPath;
    DefaultPhysicsAsset.Reset();
}

bool USkeleton::ResolveDefaultPhysicsAsset()
{
    if (DefaultPhysicsAsset.Get())
    {
        const FSkeletonCompatibilityReport Report =
            FSkeletonManager::CheckCompatibility(GetSkeletonBinding(), DefaultPhysicsAsset->GetSkeletonBinding());
        if (Report.Result == ESkeletonCompatibilityResult::ExactSkeleton)
        {
            return true;
        }

        UE_LOG("ResolveDefaultPhysicsAsset cleared incompatible cached PhysicsAsset. Skeleton=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            DefaultPhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        ClearDefaultPhysicsAsset();
        return false;
    }

    if (DefaultPhysicsAssetPath.empty() || DefaultPhysicsAssetPath == "None")
    {
        DefaultPhysicsAsset.Reset();
        return false;
    }

    UPhysicsAsset* LoadedPhysicsAsset = FPhysicsAssetManager::Get().LoadPhysicsAsset(DefaultPhysicsAssetPath);
    if (!LoadedPhysicsAsset)
    {
        DefaultPhysicsAsset.Reset();
        return false;
    }

    const FSkeletonCompatibilityReport Report =
        FSkeletonManager::CheckCompatibility(GetSkeletonBinding(), LoadedPhysicsAsset->GetSkeletonBinding());
    if (Report.Result != ESkeletonCompatibilityResult::ExactSkeleton)
    {
        UE_LOG("ResolveDefaultPhysicsAsset rejected loaded PhysicsAsset: skeleton mismatch. Skeleton=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            LoadedPhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        ClearDefaultPhysicsAsset();
        return false;
    }

    DefaultPhysicsAsset = LoadedPhysicsAsset;
    return true;
}

void USkeleton::ClearDefaultPhysicsAsset()
{
    DefaultPhysicsAsset.Reset();
    DefaultPhysicsAssetPath = "None";
}
