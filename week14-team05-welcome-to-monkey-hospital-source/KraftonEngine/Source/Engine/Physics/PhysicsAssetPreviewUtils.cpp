#include "Physics/PhysicsAssetPreviewUtils.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/PhysicsAsset.h"

namespace
{
    bool HasMeaningfulBoneName(const FName& BoneName)
    {
        return BoneName.IsValid() && BoneName != FName::None;
    }

    // Preview uses the same authored transform convention as runtime creation, but it
    // stays completely detached from live physics state so editor tools can call it freely.
    FTransform ComposePreviewTransforms(const FTransform& ParentWorld, const FTransform& Local)
    {
        FTransform Result = Local;
        Result.Location = ParentWorld.Location + ParentWorld.Rotation.RotateVector(Local.Location);
        Result.Rotation = (ParentWorld.Rotation * Local.Rotation).GetNormalized();
        Result.Scale = FVector::OneVector;
        return Result;
    }

    FTransform GetComponentWorldTransform(const USkeletalMeshComponent* Component)
    {
        FTransform Result;
        if (!Component)
        {
            return Result;
        }

        Result.Location = Component->GetWorldLocation();
        Result.Rotation = Component->GetWorldMatrix().ToQuat().GetNormalized();
        Result.Scale = FVector::OneVector;
        return Result;
    }

    int32 FindBoneIndexInMesh(const USkeletalMesh* SkeletalMesh, const FName& BoneName)
    {
        if (!SkeletalMesh || !HasMeaningfulBoneName(BoneName))
        {
            return -1;
        }

        const FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
        if (!MeshAsset)
        {
            return -1;
        }

        const FString BoneNameString = BoneName.ToString();
        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
        {
            if (MeshAsset->Bones[BoneIndex].Name == BoneNameString)
            {
                return BoneIndex;
            }
        }

        return -1;
    }
}



bool FPhysicsAssetPreviewPoseCache::Initialize(
    const USkeletalMeshComponent* InPreviewComponent,
    const UPhysicsAsset* InPhysicsAsset)
{
    PreviewComponent = InPreviewComponent;
    PhysicsAsset = InPhysicsAsset;
    BoneComponentSpaceTransforms.clear();
    BodyWorldTransforms.clear();
    BodyWorldTransformValid.clear();

    if (!PreviewComponent || !PhysicsAsset || !PreviewComponent->GetSkeletalMesh())
    {
        return false;
    }

    PreviewComponent->GetCurrentBoneGlobalTransforms(BoneComponentSpaceTransforms);
    if (BoneComponentSpaceTransforms.empty())
    {
        return false;
    }

    const TArray<FPhysicsAssetBodySetup>& BodySetups = PhysicsAsset->GetBodySetups();
    BodyWorldTransforms.resize(BodySetups.size());
    BodyWorldTransformValid.resize(BodySetups.size(), 0);

    const USkeletalMesh* SkeletalMesh = PreviewComponent->GetSkeletalMesh();
    const FTransform ComponentWorldTransform = GetComponentWorldTransform(PreviewComponent);
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
    {
        int32 BoneIndex = -1;
        if (!FPhysicsAssetPreviewUtils::ResolveBodyBoneIndex(SkeletalMesh, BodySetups[BodyIndex], BoneIndex) ||
            BoneIndex < 0 ||
            BoneIndex >= static_cast<int32>(BoneComponentSpaceTransforms.size()))
        {
            continue;
        }

        const FTransform BoneWorldTransform =
            ComposePreviewTransforms(ComponentWorldTransform, BoneComponentSpaceTransforms[BoneIndex]);
        BodyWorldTransforms[BodyIndex] = ComposePreviewTransforms(BoneWorldTransform, BodySetups[BodyIndex].BodyLocalFrame);
        BodyWorldTransformValid[BodyIndex] = 1;
    }

    return true;
}

bool FPhysicsAssetPreviewPoseCache::HasPose() const
{
    return PreviewComponent && PhysicsAsset && !BoneComponentSpaceTransforms.empty();
}

bool FPhysicsAssetPreviewPoseCache::ComputeBodyWorldTransform(
    int32 BodyIndex,
    FTransform& OutWorldTransform) const
{
    if (!HasPose() ||
        BodyIndex < 0 ||
        BodyIndex >= static_cast<int32>(BodyWorldTransforms.size()) ||
        BodyIndex >= static_cast<int32>(BodyWorldTransformValid.size()) ||
        !BodyWorldTransformValid[BodyIndex])
    {
        return false;
    }

    OutWorldTransform = BodyWorldTransforms[BodyIndex];
    return true;
}

bool FPhysicsAssetPreviewPoseCache::ComputeBodyWorldTransformByBoneName(
    const FName& BoneName,
    FTransform& OutWorldTransform) const
{
    if (!PhysicsAsset)
    {
        return false;
    }

    return ComputeBodyWorldTransform(PhysicsAsset->FindBodySetupIndexByBoneName(BoneName), OutWorldTransform);
}

bool FPhysicsAssetPreviewPoseCache::ComputeConstraintWorldFrames(
    int32 ConstraintIndex,
    FTransform& OutParentFrameWorld,
    FTransform& OutChildFrameWorld) const
{
    if (!HasPose() || !FPhysicsAssetPreviewUtils::IsConstraintSetupIndexValid(PhysicsAsset, ConstraintIndex))
    {
        return false;
    }

    const FPhysicsAssetConstraintSetup& ConstraintSetup = PhysicsAsset->GetConstraintSetups()[ConstraintIndex];
    FTransform ParentBodyWorld;
    FTransform ChildBodyWorld;
    if (!ComputeBodyWorldTransformByBoneName(ConstraintSetup.ParentBoneName, ParentBodyWorld) ||
        !ComputeBodyWorldTransformByBoneName(ConstraintSetup.ChildBoneName, ChildBodyWorld))
    {
        return false;
    }

    OutParentFrameWorld = ComposePreviewTransforms(ParentBodyWorld, ConstraintSetup.ParentLocalFrame);
    OutChildFrameWorld = ComposePreviewTransforms(ChildBodyWorld, ConstraintSetup.ChildLocalFrame);
    return true;
}

bool FPhysicsAssetPreviewUtils::HasPreviewPose(const USkeletalMeshComponent* PreviewComponent)
{
    if (!PreviewComponent || !PreviewComponent->GetSkeletalMesh())
    {
        return false;
    }

    TArray<FTransform> BoneComponentSpaceTransforms;
    PreviewComponent->GetCurrentBoneGlobalTransforms(BoneComponentSpaceTransforms);
    return !BoneComponentSpaceTransforms.empty();
}

bool FPhysicsAssetPreviewUtils::IsBodySetupIndexValid(const UPhysicsAsset* PhysicsAsset, int32 BodyIndex)
{
    return PhysicsAsset &&
        BodyIndex >= 0 &&
        BodyIndex < static_cast<int32>(PhysicsAsset->GetBodySetups().size());
}

bool FPhysicsAssetPreviewUtils::IsConstraintSetupIndexValid(const UPhysicsAsset* PhysicsAsset, int32 ConstraintIndex)
{
    return PhysicsAsset &&
        ConstraintIndex >= 0 &&
        ConstraintIndex < static_cast<int32>(PhysicsAsset->GetConstraintSetups().size());
}

bool FPhysicsAssetPreviewUtils::ResolveBodyBoneIndex(
    const USkeletalMesh* SkeletalMesh,
    const FPhysicsAssetBodySetup& BodySetup,
    int32& OutBoneIndex)
{
    OutBoneIndex = FindBoneIndexInMesh(SkeletalMesh, BodySetup.BoneName);
    return OutBoneIndex >= 0;
}

bool FPhysicsAssetPreviewUtils::ResolveConstraintBoneIndices(
    const USkeletalMesh* SkeletalMesh,
    const FPhysicsAssetConstraintSetup& ConstraintSetup,
    int32& OutParentBoneIndex,
    int32& OutChildBoneIndex)
{
    OutParentBoneIndex = FindBoneIndexInMesh(SkeletalMesh, ConstraintSetup.ParentBoneName);
    OutChildBoneIndex = FindBoneIndexInMesh(SkeletalMesh, ConstraintSetup.ChildBoneName);
    return OutParentBoneIndex >= 0 && OutChildBoneIndex >= 0;
}

bool FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransform(
    const USkeletalMeshComponent* PreviewComponent,
    const UPhysicsAsset* PhysicsAsset,
    int32 BodyIndex,
    FTransform& OutWorldTransform)
{
    // Single-shot callers keep the old API, but internally go through the same cache path
    // used by the editor preview to avoid duplicated transform logic.
    FPhysicsAssetPreviewPoseCache PoseCache;
    return PoseCache.Initialize(PreviewComponent, PhysicsAsset) &&
        PoseCache.ComputeBodyWorldTransform(BodyIndex, OutWorldTransform);
}

bool FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransformByBoneName(
    const USkeletalMeshComponent* PreviewComponent,
    const UPhysicsAsset* PhysicsAsset,
    const FName& BoneName,
    FTransform& OutWorldTransform)
{
    if (!PhysicsAsset)
    {
        return false;
    }

    return ComputePreviewBodyWorldTransform(
        PreviewComponent,
        PhysicsAsset,
        PhysicsAsset->FindBodySetupIndexByBoneName(BoneName),
        OutWorldTransform);
}

bool FPhysicsAssetPreviewUtils::ComputePreviewConstraintWorldFrames(
    const USkeletalMeshComponent* PreviewComponent,
    const UPhysicsAsset* PhysicsAsset,
    int32 ConstraintIndex,
    FTransform& OutParentFrameWorld,
    FTransform& OutChildFrameWorld)
{
    FPhysicsAssetPreviewPoseCache PoseCache;
    return PoseCache.Initialize(PreviewComponent, PhysicsAsset) &&
        PoseCache.ComputeConstraintWorldFrames(ConstraintIndex, OutParentFrameWorld, OutChildFrameWorld);
}
