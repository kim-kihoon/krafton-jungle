#pragma once

#include "Physics/PhysicsTypes.h"

namespace physx
{
    class PxRigidActor;
}

struct FBodyInstance
{
    FPhysicsBodyHandle Handle;

    uint32 OwnerActorId             = 0;
    uint32 OwnerComponentId         = 0; // = root component UUID
    uint32 OwnerComponentGeneration = 0;

    FName BoneName = FName::None;

    physx::PxRigidActor* Actor = nullptr;

    TArray<FPhysicsShapeHandle>      Shapes;
    TArray<FPhysicsConstraintHandle> Constraints;

    EPhysicsBodyType BodyType = EPhysicsBodyType::Static;
    EPhysicsSyncMode SyncMode = EPhysicsSyncMode::EngineToPhysics;

    FTransform PreviousTransform;
    FTransform CurrentTransform;

    bool bRegisteredInScene     = false;
    bool bGenerateHitEvents     = false;
    bool bGenerateOverlapEvents = false;

    // Lifecycle / 도메인
    EPhysicsRuntimeObjectState State        = EPhysicsRuntimeObjectState::Free;
    EPhysicsBodyDomain         Domain       = EPhysicsBodyDomain::ActorComponent;
    bool                       bPendingKill = false;

    // Runtime mutable 속성 단일 소스 (mass/COM/damping/lock/solver/gravity).
    FBodyRuntimeProperties Properties;

    // simulate 결과 캐시 — snapshot/보간/디버그가 PhysX 직접 접근 없이 읽는다.
    FVector LinearVelocity  = FVector::ZeroVector;
    FVector AngularVelocity = FVector::ZeroVector;
    bool    bIsSleeping     = false;

    uint64 CreatedStep   = 0;
    uint64 DestroyedStep = 0;

    bool IsAlive() const
    {
        return State == EPhysicsRuntimeObjectState::Alive;
    }

    bool IsManualSync() const
    {
        return SyncMode == EPhysicsSyncMode::Manual;
    }

    bool ShouldPullTransformToEngine() const
    {
        return SyncMode == EPhysicsSyncMode::PhysicsToEngine;
    }

    bool ShouldPushTransformToPhysics() const
    {
        return SyncMode == EPhysicsSyncMode::EngineToPhysics;
    }

    bool IsKinematicTargetDriven() const
    {
        return SyncMode == EPhysicsSyncMode::KinematicTarget;
    }
};