#pragma once

#include "Physics/IPhysicsScene.h"
#include "Core/Types/CoreTypes.h"
#include "Physics/PhysXPhysicsRuntime.h"
#include "Physics/PhysicsComponentBinding.h"
#include "Physics/PhysicsEventSnapshot.h"
#include "Physics/PhysicsQueryTypes.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class AActor;

// Forward declarations — PhysX types
namespace physx
{
    class PxFoundation;
    class PxPhysics;
    class PxScene;
    class PxDefaultCpuDispatcher;
    class PxMaterial;
}

class FPhysXSimulationCallback;

// ============================================================
// FPhysXPhysicsScene — PhysX 4.1 기반 물리 시스템
//
// IPhysicsScene 인터페이스를 구현하는 PhysX scene 어댑터.
//
// 등록 단위는 Actor — 한 액터의 여러 PrimitiveComponent는 하나의
// PxRigidActor에 compound shape로 합쳐진다. 각 shape의 LocalPose는
// 액터 RootComponent에 대한 상대 transform. 이로써 차체 Box + 바퀴
// Sphere 4개처럼 다중 콜라이더가 자연스럽게 한 강체로 동작한다.
// ============================================================
class FPhysXPhysicsScene : public IPhysicsScene
{
public:
    void Initialize(UWorld* InWorld) override;
    void Shutdown() override;

    void RegisterComponent(UPrimitiveComponent* Comp) override;
    void UnregisterComponent(UPrimitiveComponent* Comp) override;
    void RebuildBody(UPrimitiveComponent* Comp) override;
    void SyncComponentTransform(UPrimitiveComponent* Comp) override;

    void Tick(float DeltaTime) override;
    void SubmitPhysicsFrame(uint64 FrameIndex, float DeltaTime) override;
    void WaitPhysicsFrame(uint64 FrameIndex) override;
    void DispatchPendingEvents() override;

    void AddForce(UPrimitiveComponent* Comp, const FVector& Force) override;
    void AddForceAtLocation(
        UPrimitiveComponent* Comp,
        const FVector&       Force,
        const FVector&       WorldLocation
    ) override;
    void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) override;
    void AddImpulse(UPrimitiveComponent* Comp, const FVector& Impulse) override;

    FVector GetLinearVelocity(UPrimitiveComponent* Comp) const override;
    void    SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;

    FVector GetAngularVelocity(UPrimitiveComponent* Comp) const override;
    void    SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;

    void  SetMass(UPrimitiveComponent* Comp, float Mass) override;
    float GetMass(UPrimitiveComponent* Comp) const override;

    void    SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) override;
    FVector GetCenterOfMass(UPrimitiveComponent* Comp) const override;

    void SetLinearLock(UPrimitiveComponent* Comp, bool bX, bool bY, bool bZ) override;
    void SetAngularLock(UPrimitiveComponent* Comp, bool bX, bool bY, bool bZ) override;

    bool Raycast(
        const FVector&    Start,
        const FVector&    Dir,
        float             MaxDist,
        FHitResult&       OutHit,
        ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
        const AActor*     IgnoreActor  = nullptr
    ) override;

    bool RaycastByObjectTypes(
        const FVector& Start,
        const FVector& Dir,
        float          MaxDist,
        FHitResult&    OutHit,
        uint32         ObjectTypeMask,
        const AActor*  IgnoreActor = nullptr
    ) override;

    bool RaycastPhysicsByObjectTypes(
        const FVector&          Start,
        const FVector&          Dir,
        float                   MaxDist,
        FPhysicsRaycastResult&  OutResult,
        uint32                  ObjectTypeMask,
        const AActor*           IgnoreActor = nullptr
    ) override;

    bool Sweep(
        const FVector&         Start,
        const FVector&         End,
        const FQuat&           Rotation,
        const FCollisionShape& Shape,
        FHitResult&            OutHit,
        ECollisionChannel      TraceChannel = ECollisionChannel::WorldStatic,
        const AActor*          IgnoreActor  = nullptr
    ) override;

    bool SweepByObjectTypes(
        const FVector&         Start,
        const FVector&         End,
        const FQuat&           Rotation,
        const FCollisionShape& Shape,
        FHitResult&            OutHit,
        uint32                 ObjectTypeMask,
        const AActor*          IgnoreActor = nullptr
    ) override;

    IPhysicsRuntime* GetRuntime() override
    {
        return &Runtime;
    }

    const IPhysicsRuntime* GetRuntime() const override
    {
        return &Runtime;
    }

    uint32 GetComponentGeneration_GameThread(uint32 ComponentId) const override;

    FVehicleHandle CreateVehicle(const FVehicleDesc& Desc) override;
    void                DestroyVehicle(FVehicleHandle Vehicle) override;
    void                SetVehicleInput(FVehicleHandle Vehicle, const FVehicleInputState& Input) override;
    void                ResetVehicle(FVehicleHandle Vehicle, const FTransform& WorldTransform) override;

private:
    FPhysicsComponentBinding&       TouchBinding_GameThread(UPrimitiveComponent* Comp);
    const FPhysicsComponentBinding* FindBinding_GameThread(uint32 ComponentId) const;
    FPhysicsComponentBinding*       FindBinding_GameThread(uint32 ComponentId);

    FPhysicsBodyCreatePayload BuildRegisterPayload_GameThread(UPrimitiveComponent* Comp, FPhysicsComponentBinding& Binding);
    FBodyCreationDesc         BuildBodyDescFromComponent_GameThread(UPrimitiveComponent* Comp, uint32 Generation) const;
    FPhysicsShapeDesc         BuildShapeDescFromComponent_GameThread(UPrimitiveComponent* Comp, UPrimitiveComponent* RootComponent) const;
    void                      EnqueueEngineTransformSync_GameThread();
    bool                      ResolveRaycastResult_GameThread(const FPhysicsRaycastResult& PhysicsResult, FHitResult& OutHit) const;
    bool                      ResolveSweepResult_GameThread(const FPhysicsSweepResult& PhysicsResult, FHitResult& OutHit) const;
    bool                      ExecuteRaycast_PhysicsThread(const FVector& Start, const FVector& Dir, float MaxDist, ECollisionChannel TraceChannel, uint32 IgnoreActorId, FPhysicsRaycastResult& OutResult) const;
    bool                      ExecuteRaycastByObjectTypes_PhysicsThread(const FVector& Start, const FVector& Dir, float MaxDist, uint32 ObjectTypeMask, uint32 IgnoreActorId, FPhysicsRaycastResult& OutResult) const;
    bool                      ExecuteSweep_PhysicsThread(const FVector& Start, const FVector& Dir, float MaxDist, const FQuat& Rotation, const FCollisionShape& Shape, ECollisionChannel TraceChannel, uint32 IgnoreActorId, FPhysicsSweepResult& OutResult) const;
    bool                      ExecuteSweepByObjectTypes_PhysicsThread(const FVector& Start, const FVector& Dir, float MaxDist, const FQuat& Rotation, const FCollisionShape& Shape, uint32 ObjectTypeMask, uint32 IgnoreActorId, FPhysicsSweepResult& OutResult) const;
    bool                      SubmitRaycastQuery_GameThread(bool bObjectTypes, const FVector& Start, const FVector& Dir, float MaxDist, ECollisionChannel TraceChannel, uint32 ObjectTypeMask, uint32 IgnoreActorId, FPhysicsRaycastResult& OutResult);
    bool                      SubmitSweepQuery_GameThread(bool bObjectTypes, const FVector& Start, const FVector& Dir, float MaxDist, const FQuat& Rotation, const FCollisionShape& Shape, ECollisionChannel TraceChannel, uint32 ObjectTypeMask, uint32 IgnoreActorId, FPhysicsSweepResult& OutResult);

    void StartPhysicsThread();
    void StopPhysicsThreadAndJoin();
    void PhysicsThreadMain();
    void RunPhysicsFrame_PhysicsThread(float DeltaTime, const TArray<FPhysicsCommand>& FrameCommands);
    void ConsumeCreationResults_GameThread();

    TMap<uint32, FPhysicsComponentBinding> GameThreadBindings;
    TMap<uint32, FPhysicsBodyHandle>       GameThreadActorBodies;

	UWorld* World = nullptr;

	// PhysX core objects
    physx::PxFoundation*           Foundation      = nullptr;
    physx::PxPhysics*              Physics         = nullptr;
    physx::PxScene*                Scene           = nullptr;
    physx::PxDefaultCpuDispatcher* Dispatcher      = nullptr;
    physx::PxMaterial*             DefaultMaterial = nullptr;

    FPhysXSimulationCallback* EventCallback = nullptr;

    FPhysXPhysicsRuntime Runtime;

    std::thread                     PhysicsThread;
    mutable std::mutex              PhysicsThreadMutex;
    mutable std::condition_variable PhysicsThreadCv;
    mutable std::condition_variable PhysicsThreadDoneCv;
    bool                            bPhysicsThreadStarted       = false;
    bool                            bPhysicsThreadStopRequested = false;
    bool                            bPhysicsFramePending        = false;
    bool                            bPhysicsFrameInProgress     = false;
    uint64                          PendingPhysicsFrameIndex    = 0;
    uint64                          CompletedPhysicsFrameIndex  = 0;
    float                           PendingPhysicsDeltaTime     = 0.0f;
    TArray<FPhysicsCommand>         PendingPhysicsFrameCommands;

    mutable bool                  bPhysicsQueryPending     = false;
    mutable bool                  bPhysicsQueryInProgress  = false;
    mutable bool                  bPhysicsQueryCompleted   = false;
    mutable bool                  bPendingQueryObjectTypes = false;
    mutable bool                  bPendingQuerySweep       = false;
    mutable FVector               PendingQueryStart;
    mutable FVector               PendingQueryDir;
    mutable float                 PendingQueryMaxDist        = 0.0f;
    mutable FQuat                 PendingQueryRotation       = FQuat::Identity;
    mutable FCollisionShape       PendingQueryShape;
    mutable ECollisionChannel     PendingQueryTraceChannel   = ECollisionChannel::WorldStatic;
    mutable uint32                PendingQueryObjectTypeMask = 0;
    mutable uint32                PendingQueryIgnoreActorId  = 0;
    mutable bool                  bPendingQueryHit           = false;
    mutable FPhysicsRaycastResult PendingQueryResult;
    mutable FPhysicsSweepResult   PendingSweepQueryResult;
};
