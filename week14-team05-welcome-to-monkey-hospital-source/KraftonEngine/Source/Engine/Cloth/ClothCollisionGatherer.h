#pragma once

#include "Cloth/ClothCollisionTypes.h"

class UPhysicsAsset;
class USkeletalMesh;
class USkeletalMeshComponent;
class IPhysicsRuntime;
struct FSkeletalClothConfig;
struct FTransform;

class FClothCollisionGatherer
{
public:
    FClothCollisionGatherResult GatherForSection(
        const USkeletalMeshComponent& Component,
        const USkeletalMesh& SkeletalMesh,
        const UPhysicsAsset* PhysicsAsset,
        const IPhysicsRuntime* PhysicsRuntime,
        const FSkeletalClothConfig& ClothConfig,
        const FBoundingBox& ComponentWorldBounds,
        const FBoundingBox& SectionWorldBounds,
        const FClothCollisionBudget& Budget = FClothCollisionBudget()) const;

    FClothCollisionGatherResult GatherPhysicsAsset(
        const USkeletalMeshComponent& Component,
        const USkeletalMesh& SkeletalMesh,
        const UPhysicsAsset* PhysicsAsset,
        const FSkeletalClothConfig& ClothConfig,
        const FClothCollisionBudget& Budget = FClothCollisionBudget()) const;

private:
    void GatherPhysicsAssetCandidates(
        FClothCollisionGatherResult& Result,
        const USkeletalMeshComponent& Component,
        const USkeletalMesh& SkeletalMesh,
        const UPhysicsAsset* PhysicsAsset,
        const FSkeletalClothConfig& ClothConfig) const;

    void GatherWorldStaticCandidates(
        FClothCollisionGatherResult& Result,
        const IPhysicsRuntime& PhysicsRuntime,
        const USkeletalMeshComponent& Component,
        const FSkeletalClothConfig& ClothConfig,
        const FBoundingBox& ComponentWorldBounds,
        const FBoundingBox& SectionWorldBounds) const;

    void GatherWorldDynamicCandidates(
        FClothCollisionGatherResult& Result,
        const IPhysicsRuntime& PhysicsRuntime,
        const USkeletalMeshComponent& Component,
        const FSkeletalClothConfig& ClothConfig,
        const FBoundingBox& ComponentWorldBounds,
        const FBoundingBox& SectionWorldBounds) const;

    void GatherWorldCandidates(
        FClothCollisionGatherResult& Result,
        const IPhysicsRuntime& PhysicsRuntime,
        const USkeletalMeshComponent& Component,
        const FBoundingBox& ComponentWorldBounds,
        const FBoundingBox& SectionWorldBounds,
        EClothCollisionSource Source,
        uint32 ObjectTypeMask) const;

    void SelectWithinBudget(
        FClothCollisionGatherResult& Result,
        const FClothCollisionBudget& Budget) const;
};
