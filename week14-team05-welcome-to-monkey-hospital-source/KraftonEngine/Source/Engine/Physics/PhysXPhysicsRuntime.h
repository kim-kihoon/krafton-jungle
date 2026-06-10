#pragma once

#include "Physics/PhysicsBodyInstance.h"
#include "Physics/PhysicsCommandQueue.h"
#include "Physics/PhysicsConstraintInstance.h"
#include "Physics/PhysicsDebugTypes.h"
#include "Physics/PhysicsRuntime.h"
#include "Physics/PhysicsShapeInstance.h"
#include "Physics/PhysicsStats.h"
#include "Physics/PhysicsWorldSnapshot.h"

#include <memory>
#include <mutex>

class UWorld;
class FPhysXVehicleRuntime;

namespace physx
{
    class PxPhysics;
    class PxScene;
    class PxDefaultCpuDispatcher;
    class PxMaterial;
    class PxRigidActor;
    class PxShape;
}

struct FActorCompoundBody
{
    uint32 OwnerActorId    = 0;
    uint32 RootComponentId = 0;
    uint32 RootGeneration  = 0;

    FPhysicsBodyHandle Body;

    TArray<uint32> Components;
};

class FPhysXPhysicsRuntime : public IPhysicsRuntime
{
public:
    FPhysXPhysicsRuntime();
    ~FPhysXPhysicsRuntime() override;

    void Initialize(
        UWorld*            InWorld,
        physx::PxPhysics*  InPhysics,
        physx::PxScene*    InScene,
        physx::PxMaterial* InDefaultMaterial
    );

    void Shutdown();

    void Tick(float DeltaTime);
    void Tick(float DeltaTime, const TArray<FPhysicsCommand>& FrameCommands);
    void DrainPendingCommands_GameThread(TArray<FPhysicsCommand>& OutCommands);
    void ConsumeCreationResults_GameThread(TArray<FPhysicsCreationResult>& OutResults);

    FPhysicsBodyHandle  ReserveBodyHandle_GameThread();
    FPhysicsShapeHandle ReserveShapeHandle_GameThread();
    void                RegisterComponent(const FPhysicsBodyCreatePayload& Payload);
    void                UnregisterComponent(const FPhysicsObjectKey& Object);
    void                RebuildBody(const TArray<FPhysicsBodyCreatePayload>& Payloads);

    FPhysicsBodyHandle  FindBodyByComponentId(uint32 ComponentId) const;
    FPhysicsShapeHandle FindShapeByComponentId(uint32 ComponentId) const;

    FPhysicsBodyHandle CreateRigidBody(const FBodyCreationDesc& Desc) override;
    void               DestroyRigidBody(FPhysicsBodyHandle Body) override;

    FPhysicsConstraintHandle CreateConstraint(
        FPhysicsBodyHandle             Parent,
        FPhysicsBodyHandle             Child,
        const FConstraintCreationDesc& Desc
    ) override;

    FPhysicsConstraintHandle CreateFixedJoint(
        FPhysicsBodyHandle Parent,
        const FTransform&  ParentLocalFrame,
        FPhysicsBodyHandle Child,
        const FTransform&  ChildLocalFrame
    ) override;

    FPhysicsConstraintHandle CreateSphericalJoint(
        FPhysicsBodyHandle          Parent,
        const FTransform&           ParentLocalFrame,
        FPhysicsBodyHandle          Child,
        const FTransform&           ChildLocalFrame,
        const FConstraintLimitDesc& AngularLimits
    ) override;

    void DestroyConstraint(FPhysicsConstraintHandle Constraint) override;

    FTransform GetBodyTransform(FPhysicsBodyHandle Body) const override;

    void SetBodyTransform(
        FPhysicsBodyHandle   Body,
        const FTransform&    Transform,
        EPhysicsTeleportMode TeleportMode
    ) override;

    void SetBodyVelocity(
        FPhysicsBodyHandle Body,
        const FVector&     LinearVelocity,
        const FVector&     AngularVelocity
    ) override;

    FVector GetBodyLinearVelocity(FPhysicsBodyHandle Body) const override;
    FVector GetBodyAngularVelocity(FPhysicsBodyHandle Body) const override;

    void AddForce(FPhysicsBodyHandle Body, const FVector& Force) override;
    void AddForceAtLocation(
        FPhysicsBodyHandle Body,
        const FVector&     Force,
        const FVector&     WorldLocation
    ) override;
    void AddTorque(FPhysicsBodyHandle Body, const FVector& Torque) override;
    void AddImpulse(FPhysicsBodyHandle Body, const FVector& Impulse) override;

    void  SetMass(FPhysicsBodyHandle Body, float Mass) override;
    float GetMass(FPhysicsBodyHandle Body) const override;

    void    SetCenterOfMass(FPhysicsBodyHandle Body, const FVector& LocalOffset) override;
    FVector GetCenterOfMass(FPhysicsBodyHandle Body) const override;

    void SetLinearLock(FPhysicsBodyHandle Body, bool bX, bool bY, bool bZ) override;
    void SetAngularLock(FPhysicsBodyHandle Body, bool bX, bool bY, bool bZ) override;

    // Game Thread 는 이 스냅샷을 acquire 해서 Component/Ragdoll/Vehicle visual 을 적용한다.
    std::shared_ptr<const FPhysicsWorldSnapshot> AcquireLatestSnapshotRef() const override;
    void QueryClothCollisionShapes(
        const FBoundingBox& WorldBounds,
        uint32 ObjectTypeMask,
        TArray<FPhysicsClothCollisionShape>& OutShapes) const override;

    // 외부 소비자(C/D)는 이 세 함수로만 디버그 데이터를 얻는다 — 전부 step 직후 publish 된
    // 스냅샷의 복사본을 lock 아래에서 반환한다 (live PhysX 직접 접근 없음).
    void GatherDebugBodies(TArray<FPhysicsDebugBody>& OutBodies) const override;
    void GatherDebugConstraints(TArray<FPhysicsDebugConstraint>& OutConstraints) const override;
    void GetDebugSnapshot(FPhysicsDebugSnapshot& OutSnapshot) const;

    FVehicleHandle ReserveVehicleHandle_GameThread();

    void CreateVehicle(const FVehicleDesc& Desc);
    void DestroyVehicle(FVehicleHandle Vehicle);
    void SetVehicleInput(FVehicleHandle Vehicle, const FVehicleInputState& Input);
    void ResetVehicle(FVehicleHandle Vehicle, const FTransform& WorldTransform);

    void RecordRaycastQuery();
    void SetEventStats(int32 NumContactPairs, int32 NumTriggerPairs, float DispatchEventMs);

    FPhysicsStats GetStats() const override;

private:
    FPhysicsBodyHandle       AllocateBody();
    FPhysicsShapeHandle      AllocateShape();
    FPhysicsConstraintHandle AllocateConstraint();
    FPhysicsBodyHandle       CreateRigidBody_Internal(const FBodyCreationDesc& Desc);
    void                     DestroyRigidBody_Internal(FPhysicsBodyHandle Body);
    FPhysicsConstraintHandle CreateConstraint_Internal(
        FPhysicsBodyHandle             Parent,
        FPhysicsBodyHandle             Child,
        const FConstraintCreationDesc& Desc,
        FPhysicsConstraintHandle       ReservedConstraint = FPhysicsConstraintHandle {}
    );
    void DestroyConstraint_Internal(FPhysicsConstraintHandle Constraint);


    FBodyInstance*       ResolveBody(FPhysicsBodyHandle Handle);
    const FBodyInstance* ResolveBody(FPhysicsBodyHandle Handle) const;

    // generation + State(Alive) 까지 검사. PendingDestroy/Destroyed body 의 stale access 차단.
    FBodyInstance*       ResolveAliveBody(FPhysicsBodyHandle Handle);
    const FBodyInstance* ResolveAliveBody(FPhysicsBodyHandle Handle) const;

    FShapeInstance*       ResolveShape(FPhysicsShapeHandle Handle);
    const FShapeInstance* ResolveShape(FPhysicsShapeHandle Handle) const;

    FConstraintInstance*       ResolveConstraint(FPhysicsConstraintHandle Handle);
    const FConstraintInstance* ResolveConstraint(FPhysicsConstraintHandle Handle) const;

    void FreeBody(FPhysicsBodyHandle Handle);
    void FreeShape(FPhysicsShapeHandle Handle);
    void FreeConstraint(FPhysicsConstraintHandle Handle);

    void RegisterComponent_Internal(const FPhysicsBodyCreatePayload& Payload);
    void UnregisterComponent_Internal(const FPhysicsObjectKey& Object);
    void RebuildBody_Internal(const TArray<FPhysicsBodyCreatePayload>& Payloads);

    FPhysicsShapeHandle AddShapeToBody(
        FPhysicsBodyHandle       BodyHandle,
        uint32                   SourceComponentId,
        uint32                   SourceActorId,
        uint32                   SourceGeneration,
        const FPhysicsShapeDesc& Desc,
        FPhysicsShapeHandle      ReservedShape = FPhysicsShapeHandle {}
    );

    void DetachShape(FPhysicsShapeHandle ShapeHandle);
    void DetachShapeForComponentId(uint32 ComponentId);

    void SyncEngineToPhysics(FBodyInstance& Body);
    void CachePhysicsResult(FBodyInstance& Body);

    // Public mutator 들은 command 만 큐에 넣는다. 실제 PhysX 변경은 physics step 의
    // ApplyPendingCommands 가 아래 ApplyXxx_Internal 들을 호출할 때만 일어난다.
    void EnqueueCommand(const FPhysicsCommand& Command);

    void ApplyAddForce_Internal(FPhysicsBodyHandle Body, const FVector& Force);
    void ApplyAddForceAtLocation_Internal(FPhysicsBodyHandle Body, const FVector& Force, const FVector& WorldLocation);
    void ApplyAddTorque_Internal(FPhysicsBodyHandle Body, const FVector& Torque);
    void ApplyAddImpulse_Internal(FPhysicsBodyHandle Body, const FVector& Impulse);
    void QueueContinuousForceCommand(const FPhysicsCommand& Command, float DurationSeconds);
    void ApplyContinuousForcesForSubstep(float SubstepDt);
    void PurgeContinuousForcesForBody(FPhysicsBodyHandle Body);
    void PurgeExpiredContinuousForces();
    void ApplySetBodyVelocity_Internal(FPhysicsBodyHandle Body, const FVector& LinearVelocity, const FVector& AngularVelocity);
    void ApplySetBodyTransform_Internal(FPhysicsBodyHandle Body, const FTransform& Transform, EPhysicsTeleportMode TeleportMode);
    void ApplySetMass_Internal(FPhysicsBodyHandle Body, float Mass);
    void ApplySetCenterOfMass_Internal(FPhysicsBodyHandle Body, const FVector& LocalOffset);
    void ApplySetLinearLock_Internal(FPhysicsBodyHandle Body, bool bX, bool bY, bool bZ);
    void ApplySetAngularLock_Internal(FPhysicsBodyHandle Body, bool bX, bool bY, bool bZ);

    // 적용한 command 수를 반환 (Stat 용).
    int32 ApplyPendingCommands();
    int32 ApplyCommands(const TArray<FPhysicsCommand>& Commands, float ContinuousForceDurationSeconds = 0.0f);
    void  QueueCreationResult(const FPhysicsCreationResult& Result);
    void  UpdateStats();

    // 스냅샷 빌더 — step 내부(read 시점)에서만 live PhysX 를 읽어 스냅샷을 구성한다.
    void BuildBodySnapshots_Internal(TArray<FPhysicsBodySnapshot>& OutBodies) const;
    void BuildDebugBodies_Internal(TArray<FPhysicsDebugBody>& OutBodies) const;
    void BuildDebugConstraints_Internal(TArray<FPhysicsDebugConstraint>& OutConstraints) const;
    void QueryClothCollisionShapes_Internal(
        const FBoundingBox& WorldBounds,
        uint32 ObjectTypeMask,
        TArray<FPhysicsClothCollisionShape>& OutShapes) const;
    void BuildWorldSnapshot_Internal();
    void BuildDebugSnapshot_Internal();

    FActorCompoundBody*       FindCompoundByActorId(uint32 ActorId);
    const FActorCompoundBody* FindCompoundByActorId(uint32 ActorId) const;

private:
    UWorld* World = nullptr;

    mutable std::mutex RuntimeStateMutex;

    physx::PxPhysics*  Physics         = nullptr;
    physx::PxScene*    Scene           = nullptr;
    physx::PxMaterial* DefaultMaterial = nullptr;

    TArray<std::unique_ptr<FBodyInstance>> Bodies;
    TArray<uint32>                         BodyGenerations;

    TArray<std::unique_ptr<FShapeInstance>> Shapes;
    TArray<uint32>                          ShapeGenerations;

    TArray<std::unique_ptr<FConstraintInstance>> Constraints;
    TArray<uint32>                               ConstraintGenerations;

    // UObject raw pointer를 보관하지 않는다. Game Thread에서 복사한 UUID/handle만 runtime map의 key/value로 사용한다.
    TMap<uint32, FActorCompoundBody>  ActorCompounds;
    TMap<uint32, FPhysicsBodyHandle>  ComponentToBody;
    TMap<uint32, FPhysicsShapeHandle> ComponentToShape;

    FPhysicsCommandQueue CommandQueue;

    struct FPendingContinuousForce
    {
        EPhysicsCommandType Type = EPhysicsCommandType::AddForce;
        FPhysicsBodyHandle  Body;
        FVector             VectorValue  = FVector::ZeroVector;
        FVector             VectorValue2 = FVector::ZeroVector;
        float               RemainingTimeSeconds = 0.0f;
    };

    TArray<FPendingContinuousForce> PendingContinuousForces;

    mutable std::mutex             CreationResultMutex;
    TArray<FPhysicsCreationResult> PendingCreationResults;

    FPhysicsStats Stats;

    mutable std::mutex ExternalStatsMutex;
    mutable int32      PendingRaycastQueries = 0;
    mutable int32      LastContactPairs      = 0;
    mutable int32      LastTriggerPairs      = 0;
    mutable float      LastDispatchEventMs   = 0.0f;

    int32 FrameDeferredDestroys = 0;

    float Accumulator        = 0.0f;
    float FixedDt            = 1.0f / 60.0f; // target publish/catch-up step
    float SimulationSubstepDt = 1.0f / 60.0f; // actual PxScene::simulate dt cap
    float MaxFrameDt         = 0.1f;
    int32 MaxSubsteps        = 4;

    // 단조 증가 physics step 카운터 — body CreatedStep/DestroyedStep, snapshot StepIndex 용.
    uint64 StepIndex = 0;

    // Gameplay/Render 적용용 world snapshot — step 직후 immutable snapshot 을 publish 한다.
    // Game Thread hot path 는 shared_ptr 만 acquire 하므로 배열 전체 복사를 피한다.
    mutable std::mutex WorldSnapshotMutex;
    std::shared_ptr<const FPhysicsWorldSnapshot> PublishedWorldSnapshot;

    // Debug/Stat 스냅샷 — step 직후 publish, 외부는 lock 아래에서 복사본만 읽는다.
    bool                  bDebugSnapshotEnabled = true;
    mutable std::mutex    DebugSnapshotMutex;
    FPhysicsDebugSnapshot DebugSnapshot;

    std::unique_ptr<FPhysXVehicleRuntime> VehicleRuntime;
};
