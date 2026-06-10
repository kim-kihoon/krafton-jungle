#pragma once

#include "Math/Transform.h"
#include "Physics/PhysicsAssetTypes.h"

class UPhysicsAsset;
class USkeletalMesh;
class USkeletalMeshComponent;

struct FPhysicsAssetPreviewPoseCache
{
    const USkeletalMeshComponent* PreviewComponent = nullptr;
    const UPhysicsAsset* PhysicsAsset = nullptr;

    TArray<FTransform> BoneComponentSpaceTransforms;
    TArray<FTransform> BodyWorldTransforms;
    TArray<uint8> BodyWorldTransformValid;

    bool Initialize(const USkeletalMeshComponent* InPreviewComponent, const UPhysicsAsset* InPhysicsAsset);
    bool HasPose() const;
    bool ComputeBodyWorldTransform(int32 BodyIndex, FTransform& OutWorldTransform) const;
    bool ComputeBodyWorldTransformByBoneName(const FName& BoneName, FTransform& OutWorldTransform) const;
    bool ComputeConstraintWorldFrames(
        int32 ConstraintIndex,
        FTransform& OutParentFrameWorld,
        FTransform& OutChildFrameWorld) const;
};

// Editor preview math stays separate from runtime ragdoll execution so tools can
// place gizmos without requiring live physics objects.
class FPhysicsAssetPreviewUtils
{
public:
    // Tools can query this before attempting preview math instead of guessing whether
    // the preview component already has a usable skeletal pose.
    static bool HasPreviewPose(const USkeletalMeshComponent* PreviewComponent);
    static bool IsBodySetupIndexValid(const UPhysicsAsset* PhysicsAsset, int32 BodyIndex);
    static bool IsConstraintSetupIndexValid(const UPhysicsAsset* PhysicsAsset, int32 ConstraintIndex);
    static bool ResolveBodyBoneIndex(const USkeletalMesh* SkeletalMesh, const FPhysicsAssetBodySetup& BodySetup, int32& OutBoneIndex);
    static bool ResolveConstraintBoneIndices(
        const USkeletalMesh* SkeletalMesh,
        const FPhysicsAssetConstraintSetup& ConstraintSetup,
        int32& OutParentBoneIndex,
        int32& OutChildBoneIndex);

    static bool ComputePreviewBodyWorldTransform(
        const USkeletalMeshComponent* PreviewComponent,
        const UPhysicsAsset* PhysicsAsset,
        int32 BodyIndex,
        FTransform& OutWorldTransform);

    static bool ComputePreviewBodyWorldTransformByBoneName(
        const USkeletalMeshComponent* PreviewComponent,
        const UPhysicsAsset* PhysicsAsset,
        const FName& BoneName,
        FTransform& OutWorldTransform);

    static bool ComputePreviewConstraintWorldFrames(
        const USkeletalMeshComponent* PreviewComponent,
        const UPhysicsAsset* PhysicsAsset,
        int32 ConstraintIndex,
        FTransform& OutParentFrameWorld,
        FTransform& OutChildFrameWorld);
};
