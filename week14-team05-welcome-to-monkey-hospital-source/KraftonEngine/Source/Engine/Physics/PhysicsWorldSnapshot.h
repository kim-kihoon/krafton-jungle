#pragma once

#include "Physics/PhysicsDebugTypes.h"
#include "Physics/PhysicsStats.h"
#include "Physics/PhysicsTypes.h"
#include "Physics/Vehicle/VehicleTypes.h"


inline uint64 MakeBodyHandleKey(FPhysicsBodyHandle Body)
{
    return (static_cast<uint64>(Body.Index) << 32) | static_cast<uint64>(Body.Generation);
}

// Physics step 이후 publish 되는 gameplay/render 적용용 불변 스냅샷.
// 이후 Physics Thread 를 도입할 때 Game Thread 는 live PhysX/BodyInstance 를 직접 읽지 않고
// 이 스냅샷만 acquire 해서 Component/Ragdoll/Vehicle visual 을 적용한다.
struct FPhysicsBodySnapshot
{
    FPhysicsBodyHandle Body;

    uint32 OwnerActorId             = 0;
    uint32 OwnerComponentId         = 0;
    uint32 OwnerComponentGeneration = 0;

    FName BoneName = FName::None;

    EPhysicsBodyDomain Domain = EPhysicsBodyDomain::ActorComponent;
    EPhysicsSyncMode   SyncMode = EPhysicsSyncMode::EngineToPhysics;
    EPhysicsBodyType   BodyType = EPhysicsBodyType::Static;

    FTransform PreviousTransform;
    FTransform CurrentTransform;

    FVector LinearVelocity  = FVector::ZeroVector;
    FVector AngularVelocity = FVector::ZeroVector;

    float   Mass         = 1.0f;
    FVector CenterOfMass = FVector::ZeroVector;

    bool bIsSleeping  = false;
    bool bIsStatic    = false;
    bool bIsDynamic   = false;
    bool bIsKinematic = false;

    bool ShouldApplyToComponent() const
    {
        return Domain == EPhysicsBodyDomain::ActorComponent
            && SyncMode == EPhysicsSyncMode::PhysicsToEngine
            && OwnerComponentId != 0;
    }

    bool ShouldApplyToRagdoll() const
    {
        return Domain == EPhysicsBodyDomain::Ragdoll
            && (SyncMode == EPhysicsSyncMode::PhysicsToEngine || SyncMode == EPhysicsSyncMode::Manual)
            && OwnerComponentId != 0;
    }
};

struct FPhysicsWorldSnapshot
{
    uint64 StepIndex  = 0;
    uint64 FrameIndex = 0;

    float FixedDt = 0.0f;
    float InterpolationAlpha = 0.0f;

    TArray<FPhysicsBodySnapshot> Bodies;

    // Game Thread 가 UObject(UUID)/ragdoll bone 으로 body 를 O(1) 조회하기 위한 인덱스.
    TMap<uint64, int32> BodyToIndex;              // key = MakeBodyHandleKey(Body)
    TMap<uint32, int32> ComponentToBodyIndex;     // key = OwnerComponentId
    TMap<uint64, int32> ComponentBoneToBodyIndex; // key = MakeComponentBoneKey(ComponentId, Bone)

    // Debug renderer/stat UI 도 같은 publish boundary 를 보도록 world snapshot 에 포함한다.
    TArray<FPhysicsDebugBody>       DebugBodies;
    TArray<FPhysicsDebugConstraint> DebugConstraints;

    TArray<FVehicleSnapshot> Vehicles;
    TMap<uint64, int32> VehicleToIndex;
    TMap<uint32, int32> ComponentToVehicleIndex;

    FPhysicsStats Stats;

    const FPhysicsBodySnapshot* FindByBody(FPhysicsBodyHandle BodyHandle) const
    {
        const auto It = BodyToIndex.find(MakeBodyHandleKey(BodyHandle));
        return It != BodyToIndex.end() ? &Bodies[It->second] : nullptr;
    }

    const FPhysicsBodySnapshot* FindByComponent(uint32 ComponentId) const
    {
        const auto It = ComponentToBodyIndex.find(ComponentId);
        return It != ComponentToBodyIndex.end() ? &Bodies[It->second] : nullptr;
    }

    const FPhysicsBodySnapshot* FindRagdollBone(uint32 ComponentId, const FName& BoneName) const
    {
        const auto It = ComponentBoneToBodyIndex.find(MakeComponentBoneKey(ComponentId, BoneName));
        return It != ComponentBoneToBodyIndex.end() ? &Bodies[It->second] : nullptr;
    }

    const FVehicleSnapshot* FindVehicleByHandle(FVehicleHandle VehicleHandle) const
    {
        const auto It = VehicleToIndex.find(MakeVehicleHandleKey(VehicleHandle));
        return It != VehicleToIndex.end() ? &Vehicles[It->second] : nullptr;
    }

    const FVehicleSnapshot* FindVehicleByComponent(uint32 ComponentId) const
    {
        const auto It = ComponentToVehicleIndex.find(ComponentId);
        return It != ComponentToVehicleIndex.end() ? &Vehicles[It->second] : nullptr;
    }
};
