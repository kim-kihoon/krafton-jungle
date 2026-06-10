#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Physics/PhysicsBodyHandle.h"

class UPhysicsAsset;
class USkeletalMeshComponent;

struct FPhysicsAssetSimulationOptions
{
    bool bNoGravity = false;
    bool bSelectedOnly = false;
    bool bPartialSimulation = false;
    bool bIncludePartialDescendants = true;
    bool bForceQueryAndPhysicsCollision = false;
    bool bUseIndependentRagdollCollision = false;
    ECollisionEnabled IndependentCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
    bool bIndependentGenerateOverlapEvents = false;
    bool bSuppressSameActorPrimitiveCollisionForPartial = false;
    bool bSuppressSameActorPrimitiveOverlapForPartial = false;
    FName SelectedBoneName = FName::None;
    FName PartialRootBoneName = FName::None;
};

// Runtime state is separated from UPhysicsAsset so the asset stays editable/serializable data only.
class FPhysicsAssetInstance
{
public:
    bool Initialize(USkeletalMeshComponent* InOwner, UPhysicsAsset* InAsset);
    // Runtime objects are created from the currently selected asset on demand so the
    // component can decide policy while this instance owns low-level physics handles.
    bool CreateBodiesAndConstraints();
    bool CreateBodiesAndConstraints(const FPhysicsAssetSimulationOptions& Options);
    void DestroyBodiesAndConstraints();
    void Shutdown();
    // Reset only drops live runtime objects; cached binding metadata stays so the
    // same asset can be re-created without rebuilding ownership state.
    void ResetRuntimeState();
    bool HasLivePhysicsObjects() const;
    int32 GetLiveBodyCount() const;
    int32 GetLiveConstraintCount() const;
    // Pulls simulated body state back into bone world transforms for pose sync. The
    // component consumes the result, but the instance remains the source of truth.
    bool PullPhysicsPose(
        TArray<FTransform>& OutBoneWorldTransforms,
        const TArray<FTransform>* ReferenceBoneComponentSpaceTransforms = nullptr,
        const TArray<FTransform>* ReferenceBoneLocalTransforms = nullptr) const;

    UPhysicsAsset* GetAsset() const;
    USkeletalMeshComponent* GetOwnerComponent() const;

    const TArray<FPhysicsBodyHandle>& GetBodies() const { return BodiesByBone; }
    const TArray<FPhysicsConstraintHandle>& GetConstraints() const { return Constraints; }
    FPhysicsBodyHandle GetBodyHandleByBoneName(const FName& BoneName) const;
    FTransform GetBodyWorldTransformByBoneName(const FName& BoneName) const;
    bool FindNearestBodyToWorldLocation(
        const FVector& WorldLocation,
        FName& OutBoneName,
        FVector& OutBodyWorldLocation) const;
    bool AddImpulseToBody(FPhysicsBodyHandle BodyHandle, const FVector& Impulse) const;
    bool AddImpulseToBone(const FName& BoneName, const FVector& Impulse) const;
    bool HasValidBodyForBone(const FName& BoneName) const;
    FName GetPrimarySimulatedBodyBoneName() const;
    FName FindNearestSimulatedAncestorBodyBoneName(const FName& BoneName) const;
    FName ResolveBestImpulseTargetBoneName(const FName& HitBoneName, const FName& FallbackRootBoneName) const;
    int32 FindBodySetupIndexByBoneName(const FName& BoneName) const;
    int32 FindBoneIndexForBody(const FName& BoneName) const;
    bool IsPartialSameActorPrimitiveSuppressionActive() const { return bPartialSameActorPrimitiveSuppressionActive; }
    bool IsInitialized() const { return bInitialized; }
    int32 GetRagdollRootBoneIndex() const { return RagdollRootBoneIndex; }

private:
    TWeakObjectPtr<USkeletalMeshComponent> OwnerComponent;
    TWeakObjectPtr<UPhysicsAsset> SourceAsset;
    // Bodies are stored in PhysicsAsset body-setup order so asset-side lookups and
    // runtime handles can stay aligned without duplicating another mapping layer.
    TArray<FPhysicsBodyHandle> BodiesByBone;
    TArray<FPhysicsConstraintHandle> Constraints;
    TMap<FString, int32> BoneNameToIndex;
    int32 RagdollRootBoneIndex = -1;
    bool bPartialSameActorPrimitiveSuppressionActive = false;
    bool bInitialized = false;
};
