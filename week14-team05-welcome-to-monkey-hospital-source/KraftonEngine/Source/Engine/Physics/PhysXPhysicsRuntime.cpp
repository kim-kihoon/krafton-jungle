#include "Physics/PhysXPhysicsRuntime.h"
#include "Physics/PhysXBodyBuilder.h"
#include "Physics/PhysXConstraintBuilder.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXVehicleRuntime.h"
#include "Core/ProjectSettings.h"
#include "Core/Logging/Log.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <PxPhysicsAPI.h>

#include "fbxsdk/scene/shading/fbxsurfacematerial.h"

using namespace physx;

namespace
{
    bool IsHandleIndexValid(uint32 Index, size_t Size)
    {
        return Index != UINT32_MAX && static_cast<size_t>(Index) < Size;
    }

    FTransform ComposePhysicsTransforms(const FTransform& ParentWorld, const FTransform& Local)
    {
        FTransform Result = Local;
        Result.Location   = ParentWorld.Location + ParentWorld.Rotation.RotateVector(Local.Location);
        Result.Rotation   = ParentWorld.Rotation * Local.Rotation;
        return Result;
    }

    void ExpandBoxByOrientedBox(FBoundingBox& OutBounds, const FTransform& Transform, const FVector& HalfExtent)
    {
        const FVector Corners[8] =
        {
            FVector(-HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z),
            FVector(-HalfExtent.X, -HalfExtent.Y,  HalfExtent.Z),
            FVector(-HalfExtent.X,  HalfExtent.Y, -HalfExtent.Z),
            FVector(-HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z),
            FVector( HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z),
            FVector( HalfExtent.X, -HalfExtent.Y,  HalfExtent.Z),
            FVector( HalfExtent.X,  HalfExtent.Y, -HalfExtent.Z),
            FVector( HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z)
        };

        for (const FVector& Corner : Corners)
        {
            OutBounds.Expand(Transform.Location + Transform.Rotation.RotateVector(Corner));
        }
    }

    FBoundingBox BuildClothShapeWorldBounds(
        EPhysicsShapeType Type,
        const FTransform& WorldTransform,
        const FVector& BoxHalfExtent,
        float SphereRadius,
        float CapsuleRadius,
        float CapsuleHalfHeight)
    {
        FBoundingBox Bounds;
        switch (Type)
        {
        case EPhysicsShapeType::Sphere:
        {
            const FVector Extent(SphereRadius, SphereRadius, SphereRadius);
            Bounds.Expand(WorldTransform.Location - Extent);
            Bounds.Expand(WorldTransform.Location + Extent);
            break;
        }
        case EPhysicsShapeType::Capsule:
        {
            const FVector Axis = WorldTransform.Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));
            const float HalfSegment = (std::max)(0.0f, CapsuleHalfHeight - CapsuleRadius);
            const FVector Extent(CapsuleRadius, CapsuleRadius, CapsuleRadius);
            Bounds.Expand(WorldTransform.Location - Axis * HalfSegment - Extent);
            Bounds.Expand(WorldTransform.Location - Axis * HalfSegment + Extent);
            Bounds.Expand(WorldTransform.Location + Axis * HalfSegment - Extent);
            Bounds.Expand(WorldTransform.Location + Axis * HalfSegment + Extent);
            break;
        }
        case EPhysicsShapeType::Box:
            ExpandBoxByOrientedBox(Bounds, WorldTransform, BoxHalfExtent);
            break;
        default:
            break;
        }
        return Bounds;
    }
}


FPhysXPhysicsRuntime::FPhysXPhysicsRuntime()
    : VehicleRuntime(std::make_unique<FPhysXVehicleRuntime>())
{
}

FPhysXPhysicsRuntime::~FPhysXPhysicsRuntime() = default;

void FPhysXPhysicsRuntime::Initialize(
    UWorld* InWorld,
    PxPhysics* InPhysics,
    PxScene* InScene,
    PxMaterial* InDefaultMaterial
)
{
    World = InWorld;
    Physics = InPhysics;
    Scene = InScene;
    DefaultMaterial = InDefaultMaterial;

    const auto& PhysicsSettings = FProjectSettings::Get().Physics;
    FixedDt                     = (std::max)(1.0f / 240.0f, PhysicsSettings.FixedTimeStep);
    SimulationSubstepDt         = (std::max)(1.0f / 240.0f, (std::min)(PhysicsSettings.MaxSimulationSubstepDeltaTime, FixedDt));
    MaxFrameDt                  = (std::max)(FixedDt, PhysicsSettings.MaxFrameDeltaTime);
    MaxSubsteps                 = (std::max)(1, PhysicsSettings.MaxSubsteps);
    bDebugSnapshotEnabled       = PhysicsSettings.bBuildDebugSnapshot;

    if (!VehicleRuntime)
    {
        VehicleRuntime = std::make_unique<FPhysXVehicleRuntime>();
    }
    VehicleRuntime->Initialize(Physics, Scene, DefaultMaterial);

    Accumulator = 0.0f;
    StepIndex   = 0;
    Stats       = FPhysicsStats();
}

void FPhysXPhysicsRuntime::Shutdown()
{
    std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
    const bool                  bSceneLockedForShutdown = Scene != nullptr;
    if (bSceneLockedForShutdown)
    {
        Scene->lockWrite();
    }

    if (VehicleRuntime)
    {
        VehicleRuntime->Shutdown();
    }

    for (auto& ConstraintPtr : Constraints)
    {
        if (ConstraintPtr && ConstraintPtr->Joint)
        {
            ConstraintPtr->Joint->release();
            ConstraintPtr->Joint = nullptr;
        }
    }

    for (auto& BodyPtr : Bodies)
    {
        if (BodyPtr && BodyPtr->Actor)
        {
            if (Scene && BodyPtr->bRegisteredInScene)
            {
                Scene->removeActor(*BodyPtr->Actor);
            }

            BodyPtr->Actor->release();
            BodyPtr->Actor = nullptr;
        }
    }

    Constraints.clear();
    ConstraintGenerations.clear();

    Shapes.clear();
    ShapeGenerations.clear();

    Bodies.clear();
    BodyGenerations.clear();

    ActorCompounds.clear();
    ComponentToBody.clear();
    ComponentToShape.clear();
    PendingContinuousForces.clear();

    CommandQueue.Clear();
    Stats = FPhysicsStats();
    Accumulator = 0.0f;
    {
        std::lock_guard<std::mutex> Lock(WorldSnapshotMutex);
        PublishedWorldSnapshot.reset();
    }
    {
        std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
        DebugSnapshot = FPhysicsDebugSnapshot();
    }

    if (bSceneLockedForShutdown)
    {
        Scene->unlockWrite();
    }

    World = nullptr;
    Physics = nullptr;
    Scene = nullptr;
    DefaultMaterial = nullptr;
}

void FPhysXPhysicsRuntime::Tick(float DeltaTime)
{
    TArray<FPhysicsCommand> FrameCommands;
    DrainPendingCommands_GameThread(FrameCommands);
    Tick(DeltaTime, FrameCommands);
}

void FPhysXPhysicsRuntime::Tick(float DeltaTime, const TArray<FPhysicsCommand>& FrameCommands)
{
    std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
    FrameDeferredDestroys = 0;

    if (!Scene)
    {
        Stats.NumSubsteps = 0;
        UpdateStats();
        return;
    }

    if (DeltaTime <= 0.0f)
    {
        int32 AppliedCommands = 0;
        {
            PxSceneWriteLock WriteLock(*Scene);
            AppliedCommands = ApplyCommands(FrameCommands, 0.0f);
            for (auto& BodyPtr : Bodies)
            {
                if (!BodyPtr || !BodyPtr->Actor)
                {
                    continue;
                }
                FBodyInstance& Body = *BodyPtr;
                if (Body.ShouldPushTransformToPhysics() || Body.IsKinematicTargetDriven())
                {
                    SyncEngineToPhysics(Body);
                }
            }
        }

        Stats.NumSubsteps = 0;
        {
            PxSceneReadLock ReadLock(*Scene);
            UpdateStats();
            BuildWorldSnapshot_Internal();
        }
        Stats.NumPendingCommands = static_cast<int32>(FrameCommands.size());
        Stats.NumAppliedCommands = AppliedCommands;
        return;
    }

    if (DeltaTime > MaxFrameDt)
    {
        DeltaTime = MaxFrameDt;
    }

    Accumulator += DeltaTime;

    int32 StepCount = 0;

    // 프레임 누적 타이밍 (substep 합산). UpdateStats() 가 Stats 를 전부 초기화하므로,
    // 측정값은 local 에 모아 두었다가 UpdateStats() 호출 이후에 Stats 로 복사한다.
    using FClock = std::chrono::high_resolution_clock;
    const auto DurationMs = [](FClock::time_point A, FClock::time_point B)
    {
        return std::chrono::duration<float, std::milli>(B - A).count();
    };
    float      PrePhysicsMs           = 0.0f;
    float      ApplyCommandsMs        = 0.0f;
    float      SyncEngineToPhysicsMs  = 0.0f;
    float      SimulateMs             = 0.0f;
    float      FetchResultsMs         = 0.0f;
    float      SyncPhysicsToEngineMs  = 0.0f;
    float      VehicleRaycastMs       = 0.0f;
    float      VehicleUpdateMs        = 0.0f;
    float      PostPhysicsMs          = 0.0f;
    int32      AppliedCommands        = 0;
    int32      PendingCommandsAtDrain = 0;
    const auto PreApplyStart          = FClock::now();
    auto       PreApplyEnd            = PreApplyStart;
    auto       PreSyncEnd             = PreApplyStart;
    {
        PxSceneWriteLock WriteLock(*Scene);
        PendingCommandsAtDrain += static_cast<int32>(FrameCommands.size());
        AppliedCommands        += ApplyCommands(FrameCommands, DeltaTime);
        PreApplyEnd            = FClock::now();

        for (auto& BodyPtr : Bodies)
        {
            if (!BodyPtr || !BodyPtr->Actor)
            {
                continue;
            }

            FBodyInstance& Body = *BodyPtr;
            if (Body.ShouldPushTransformToPhysics() || Body.IsKinematicTargetDriven())
            {
                SyncEngineToPhysics(Body);
            }
        }
        PreSyncEnd = FClock::now();
    }

    ApplyCommandsMs       += DurationMs(PreApplyStart, PreApplyEnd);
    SyncEngineToPhysicsMs += DurationMs(PreApplyEnd, PreSyncEnd);
    PrePhysicsMs          += DurationMs(PreApplyStart, PreSyncEnd);

    bool bDroppedAccumulatedTime = false;
    while (Accumulator >= FixedDt && StepCount < MaxSubsteps)
    {
        const int32 RequiredSubstepsForTargetStep = (SimulationSubstepDt > 0.0f)
            ? static_cast<int32>(std::ceil(FixedDt / SimulationSubstepDt - 1.e-6f))
            : 1;
        if (StepCount + RequiredSubstepsForTargetStep > MaxSubsteps)
        {
            bDroppedAccumulatedTime = true;
            break;
        }

        float RemainingStepTime = FixedDt;

        while (RemainingStepTime > 1.e-6f)
        {
            const float SubstepDt = (std::min)(SimulationSubstepDt, RemainingStepTime);

            const auto T1 = FClock::now();
            auto       T2 = T1;
            auto       T3 = T1;
            auto       VehicleUpdateEnd = T1;
            {
                PxSceneWriteLock WriteLock(*Scene);
                ApplyContinuousForcesForSubstep(SubstepDt);
                if (VehicleRuntime)
                {
                    VehicleRuntime->PreSimulate(SubstepDt);
                    VehicleRaycastMs += VehicleRuntime->GetLastRaycastMs();
                }
                VehicleUpdateEnd = FClock::now();
                Scene->simulate(SubstepDt);
                T2 = FClock::now();
                Scene->fetchResults(true);
                T3 = FClock::now();
            }

            {
                PxSceneReadLock ReadLock(*Scene);
                for (auto& BodyPtr : Bodies)
                {
                    if (!BodyPtr || !BodyPtr->Actor)
                    {
                        continue;
                    }

                    CachePhysicsResult(*BodyPtr);
                }
            }

            const auto T4 = FClock::now();

            VehicleUpdateMs       += DurationMs(T1, VehicleUpdateEnd);
            SimulateMs            += DurationMs(VehicleUpdateEnd, T2);
            FetchResultsMs        += DurationMs(T2, T3);
            SyncPhysicsToEngineMs += DurationMs(T3, T4);
            PostPhysicsMs         += DurationMs(T3, T4);

            RemainingStepTime -= SubstepDt;
            ++StepCount;
            ++StepIndex;
        }

        Accumulator -= FixedDt;
    }

    int32 DroppedSubsteps = 0;
    if (bDroppedAccumulatedTime || (StepCount == MaxSubsteps && Accumulator >= FixedDt))
    {
        // 남은 누적 시간을 버린다 (spiral-of-death 방지). 버린 step 수를 기록.
        DroppedSubsteps = (SimulationSubstepDt > 0.0f) ? static_cast<int32>(Accumulator / SimulationSubstepDt) : 0;
        Accumulator     = 0.0f;
        PendingContinuousForces.clear();
    }

    Stats.NumSubsteps = StepCount;
    {
        PxSceneReadLock ReadLock(*Scene);
        UpdateStats();
    }

    // UpdateStats() 가 Stats 를 초기화한 뒤이므로 여기서 타이밍/누적 상태를 채운다.
    Stats.PrePhysicsMs          = PrePhysicsMs;
    Stats.ApplyCommandsMs       = ApplyCommandsMs;
    Stats.SyncEngineToPhysicsMs = SyncEngineToPhysicsMs;
    Stats.SimulateMs            = SimulateMs;
    Stats.FetchResultsMs        = FetchResultsMs;
    Stats.SyncPhysicsToEngineMs = SyncPhysicsToEngineMs;
    Stats.VehicleRaycastMs      = VehicleRaycastMs;
    Stats.VehicleUpdateMs       = VehicleUpdateMs;
    Stats.PostPhysicsMs         = PostPhysicsMs;
    Stats.NumDroppedSubsteps    = DroppedSubsteps;
    Stats.AccumulatorSeconds    = Accumulator;
    Stats.InterpolationAlpha    = (FixedDt > 0.0f) ? (Accumulator / FixedDt) : 0.0f;
    Stats.NumPendingCommands    = PendingCommandsAtDrain;
    Stats.NumAppliedCommands    = AppliedCommands;

    const auto SnapStart = FClock::now();
    {
        PxSceneReadLock ReadLock(*Scene);
        BuildWorldSnapshot_Internal();
    }
    Stats.BuildSnapshotMs = DurationMs(SnapStart, FClock::now());
    {
        std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
        DebugSnapshot.Stats = Stats;
    }
}

FPhysicsBodyHandle FPhysXPhysicsRuntime::ReserveBodyHandle_GameThread()
{
    std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
    return AllocateBody();
}

FPhysicsShapeHandle FPhysXPhysicsRuntime::ReserveShapeHandle_GameThread()
{
    std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
    return AllocateShape();
}

void FPhysXPhysicsRuntime::DrainPendingCommands_GameThread(TArray<FPhysicsCommand>& OutCommands)
{
    CommandQueue.Drain(OutCommands);
}

void FPhysXPhysicsRuntime::ConsumeCreationResults_GameThread(TArray<FPhysicsCreationResult>& OutResults)
{
    std::lock_guard<std::mutex> Lock(CreationResultMutex);
    OutResults.insert(OutResults.end(), PendingCreationResults.begin(), PendingCreationResults.end());
    PendingCreationResults.clear();
}

void FPhysXPhysicsRuntime::QueueCreationResult(const FPhysicsCreationResult& Result)
{
    std::lock_guard<std::mutex> Lock(CreationResultMutex);
    PendingCreationResults.push_back(Result);
}

void FPhysXPhysicsRuntime::RegisterComponent(const FPhysicsBodyCreatePayload& Payload)
{
    if (!Scene || !Payload.ShapeOwner.ComponentId)
    {
        return;
    }

    FPhysicsCommand Cmd;
    Cmd.Type       = EPhysicsCommandType::CreateBody;
    Cmd.Object     = Payload.ShapeOwner;
    Cmd.Body       = Payload.ReservedBody;
    Cmd.Shape      = Payload.ReservedShape;
    Cmd.CreateBody = Payload;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::UnregisterComponent(const FPhysicsObjectKey& Object)
{
    if (!Object.ComponentId)
    {
        return;
    }

    FPhysicsCommand Cmd;
    Cmd.Type   = EPhysicsCommandType::UnregisterComponent;
    Cmd.Object = Object;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::RebuildBody(const TArray<FPhysicsBodyCreatePayload>& Payloads)
{
    FPhysicsCommand Cmd;
    Cmd.Type = EPhysicsCommandType::RebuildComponentBody;
    EnqueueCommand(Cmd);

    for (const FPhysicsBodyCreatePayload& Payload : Payloads)
    {
        FPhysicsObjectKey Object = Payload.ShapeOwner;
        UnregisterComponent(Object);
        RegisterComponent(Payload);
    }
}

void FPhysXPhysicsRuntime::RegisterComponent_Internal(const FPhysicsBodyCreatePayload& Payload)
{
    if (!Physics || !Scene || !DefaultMaterial || Payload.ShapeOwner.ComponentId == 0)
    {
        return;
    }

    if (ComponentToShape.find(Payload.ShapeOwner.ComponentId) != ComponentToShape.end())
    {
        return;
    }

    FActorCompoundBody* Compound = FindCompoundByActorId(Payload.BodyOwner.ActorId);

    if (!Compound)
    {
        FBodyCreationDesc BodyDesc = ToBodyCreationDesc(Payload);
        BodyDesc.Shapes.clear();

        FPhysicsBodyHandle BodyHandle = CreateRigidBody_Internal(BodyDesc);
        if (!BodyHandle.IsValid())
        {
            return;
        }

        FActorCompoundBody NewCompound;
        NewCompound.OwnerActorId    = Payload.BodyOwner.ActorId;
        NewCompound.RootComponentId = Payload.BodyOwner.ComponentId;
        NewCompound.RootGeneration  = Payload.BodyOwner.ComponentGeneration;
        NewCompound.Body            = BodyHandle;

        ActorCompounds[Payload.BodyOwner.ActorId] = NewCompound;
        Compound                                  = &ActorCompounds[Payload.BodyOwner.ActorId];
    }
    else if (Payload.BodyOwner.ComponentId == Compound->RootComponentId)
    {
        Compound->RootGeneration = Payload.BodyOwner.ComponentGeneration;

        if (FBodyInstance* Body = ResolveBody(Compound->Body))
        {
            Body->OwnerComponentGeneration = Payload.BodyOwner.ComponentGeneration;
            Body->SyncMode                 = Payload.SyncMode;
            Body->BodyType                 = Payload.BodyType;
        }
    }

    const FPhysicsShapeDesc* ShapeDesc = Payload.Shapes.empty() ? nullptr : &Payload.Shapes[0];
    if (!ShapeDesc)
    {
        return;
    }

    FPhysicsShapeHandle ShapeHandle = AddShapeToBody(
        Compound->Body,
        Payload.ShapeOwner.ComponentId,
        Payload.ShapeOwner.ActorId,
        Payload.ShapeOwner.ComponentGeneration,
        *ShapeDesc,
        Payload.ReservedShape
    );
    if (!ShapeHandle.IsValid())
    {
        return;
    }

    if (std::find(Compound->Components.begin(), Compound->Components.end(), Payload.ShapeOwner.ComponentId)
        == Compound->Components.end())
    {
        Compound->Components.push_back(Payload.ShapeOwner.ComponentId);
    }

    ComponentToBody[Payload.ShapeOwner.ComponentId]  = Compound->Body;
    ComponentToShape[Payload.ShapeOwner.ComponentId] = ShapeHandle;

    if (FBodyInstance* Body = ResolveBody(Compound->Body))
    {
        if (Body->Actor)
        {
            FBodyCreationDesc MassDesc = ToBodyCreationDesc(Payload);
            FPhysXBodyBuilder::UpdateMassAndInertia(Body->Actor, MassDesc);
        }
    }
}

void FPhysXPhysicsRuntime::UnregisterComponent_Internal(const FPhysicsObjectKey& Object)
{
    if (!Object.ComponentId)
    {
        return;
    }

    auto BodyIt = ComponentToBody.find(Object.ComponentId);
    if (BodyIt == ComponentToBody.end())
    {
        return;
    }

    auto ShapeIt = ComponentToShape.find(Object.ComponentId);
    if (ShapeIt == ComponentToShape.end())
    {
        return;
    }

    FShapeInstance* Shape = ResolveShape(ShapeIt->second);
    if (!Shape ||
        Shape->SourceComponentId != Object.ComponentId ||
        Shape->SourceComponentGeneration != Object.ComponentGeneration)
    {
        return;
    }

    const FPhysicsBodyHandle BodyHandle = BodyIt->second;
    DetachShape(ShapeIt->second);
    ComponentToBody.erase(Object.ComponentId);

    FActorCompoundBody* Compound = FindCompoundByActorId(Object.ActorId);
    if (!Compound)
    {
        DestroyRigidBody_Internal(BodyHandle);
        return;
    }

    Compound->Components.erase(
        std::remove(Compound->Components.begin(), Compound->Components.end(), Object.ComponentId),
        Compound->Components.end()
    );

    if (Compound->Components.empty())
    {
        DestroyRigidBody_Internal(BodyHandle);
        ActorCompounds.erase(Object.ActorId);
    }
}

void FPhysXPhysicsRuntime::RebuildBody_Internal(const TArray<FPhysicsBodyCreatePayload>& Payloads)
{
    for (const FPhysicsBodyCreatePayload& Payload : Payloads)
    {
        UnregisterComponent_Internal(Payload.ShapeOwner);
        RegisterComponent_Internal(Payload);
    }
}

FPhysicsBodyHandle FPhysXPhysicsRuntime::FindBodyByComponentId(uint32 ComponentId) const
{
    std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
    auto                        It = ComponentToBody.find(ComponentId);
    return It != ComponentToBody.end() ? It->second : FPhysicsBodyHandle{};
}

FPhysicsShapeHandle FPhysXPhysicsRuntime::FindShapeByComponentId(uint32 ComponentId) const
{
    std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
    auto                        It = ComponentToShape.find(ComponentId);
    return It != ComponentToShape.end() ? It->second : FPhysicsShapeHandle{};
}

FPhysicsBodyHandle FPhysXPhysicsRuntime::CreateRigidBody_Internal(const FBodyCreationDesc& Desc)
{
    if (!Physics || !Scene)
    {
        return {};
    }

    FPhysicsBodyHandle BodyHandle = Desc.ReservedBody.IsValid() ? Desc.ReservedBody : AllocateBody();
    FBodyInstance*     Body       = ResolveBody(BodyHandle);
    if (!Body && !Desc.ReservedBody.IsValid())
    {
        BodyHandle = AllocateBody();
        Body       = ResolveBody(BodyHandle);
    }
    if (!Body)
    {
        return {};
    }

    PxRigidActor* Actor = FPhysXBodyBuilder::CreateRigidActor(Physics, Desc);
    if (!Actor)
    {
        FreeBody(BodyHandle);
        return {};
    }

    Body->OwnerActorId             = Desc.OwnerActorId;
    Body->OwnerComponentId         = Desc.OwnerComponentId;
    Body->OwnerComponentGeneration = Desc.OwnerComponentGeneration;
    Body->BoneName                 = Desc.BoneName;
    Body->Actor                    = Actor;
    Body->BodyType                 = Desc.BodyType;
    Body->SyncMode                 = Desc.SyncMode;
    Body->PreviousTransform        = Desc.WorldTransform;
    Body->CurrentTransform         = Desc.WorldTransform;
    Body->bGenerateHitEvents       = Desc.bGenerateHitEvents;
    Body->bGenerateOverlapEvents   = Desc.bGenerateOverlapEvents;

    Body->State       = EPhysicsRuntimeObjectState::Alive;
    Body->Domain      = Desc.Domain;
    Body->CreatedStep = StepIndex;

    // mutable 속성 단일 소스 초기화 — 이후 SetMass/SetLock 등이 여기서 갱신되고,
    // RebuildBody 후에도 desc 를 통해 동일 값으로 재생성된다.
    Body->Properties.Mass                         = Desc.Mass;
    Body->Properties.CenterOfMassLocalOffset      = Desc.CenterOfMassLocalOffset;
    Body->Properties.LinearDamping                = Desc.LinearDamping;
    Body->Properties.AngularDamping               = Desc.AngularDamping;
    Body->Properties.MaxAngularVelocity           = Desc.MaxAngularVelocity;
    Body->Properties.PositionSolverIterationCount = Desc.PositionSolverIterationCount;
    Body->Properties.VelocitySolverIterationCount = Desc.VelocitySolverIterationCount;
    Body->Properties.bEnableCCD                   = Desc.bEnableCCD;
    Body->Properties.bEnableGravity               = Desc.bEnableGravity;
    Body->Properties.bLockLinearX                 = Desc.bLockLinearX;
    Body->Properties.bLockLinearY                 = Desc.bLockLinearY;
    Body->Properties.bLockLinearZ                 = Desc.bLockLinearZ;
    Body->Properties.bLockAngularX                = Desc.bLockAngularX;
    Body->Properties.bLockAngularY                = Desc.bLockAngularY;
    Body->Properties.bLockAngularZ                = Desc.bLockAngularZ;

    Actor->userData = Body;

    for (const FPhysicsShapeDesc& ShapeDesc : Desc.Shapes)
    {
        AddShapeToBody(BodyHandle, Desc.OwnerComponentId, Desc.OwnerActorId, Desc.OwnerComponentGeneration, ShapeDesc, Desc.ReservedShape);
    }

    if (Actor->is<PxRigidDynamic>())
    {
        FPhysXBodyBuilder::UpdateMassAndInertia(Actor, Desc);
    }

    Scene->addActor(*Actor);
    Body->bRegisteredInScene = true;

    return BodyHandle;
}

void FPhysXPhysicsRuntime::DestroyRigidBody_Internal(FPhysicsBodyHandle BodyHandle)
{
    FBodyInstance* Body = ResolveBody(BodyHandle);
    if (!Body)
    {
        return;
    }

    // 이 시점부터 어떤 콜백/쿼리도 이 body 를 살아있다고 보면 안 된다.
    Body->State = EPhysicsRuntimeObjectState::PendingDestroy;
    PurgeContinuousForcesForBody(BodyHandle);

    TArray<FPhysicsConstraintHandle> ConstraintsToDestroy = Body->Constraints;
    for (FPhysicsConstraintHandle Constraint : ConstraintsToDestroy)
    {
        DestroyConstraint_Internal(Constraint);
    }

    TArray<FPhysicsShapeHandle> ShapesToDestroy = Body->Shapes;
    for (FPhysicsShapeHandle Shape : ShapesToDestroy)
    {
        DetachShape(Shape);
    }

    for (auto It = ComponentToBody.begin(); It != ComponentToBody.end();)
    {
        if (It->second == BodyHandle)
        {
            It = ComponentToBody.erase(It);
        }
        else
        {
            ++It;
        }
    }

    if (Body->Actor)
    {
        // release 전에 userData 를 끊어 지연된 callback/query 가 stale FBodyInstance 를
        // 역참조하지 못하게 한다.
        Body->Actor->userData = nullptr;

        if (Scene && Body->bRegisteredInScene)
        {
            Scene->removeActor(*Body->Actor);
        }

        Body->Actor->release();
        Body->Actor = nullptr;
    }

    Body->DestroyedStep = StepIndex;
    Body->State         = EPhysicsRuntimeObjectState::Destroyed;

    ++FrameDeferredDestroys;

    FreeBody(BodyHandle);
}

FPhysicsConstraintHandle FPhysXPhysicsRuntime::CreateConstraint_Internal(
    FPhysicsBodyHandle             Parent,
    FPhysicsBodyHandle             Child,
    const FConstraintCreationDesc& Desc,
    FPhysicsConstraintHandle       ReservedConstraint
)
{
    FBodyInstance* ParentBody = ResolveBody(Parent);
    FBodyInstance* ChildBody = ResolveBody(Child);

    if (!ParentBody || !ChildBody || !ParentBody->Actor || !ChildBody->Actor)
    {
        if (ReservedConstraint.IsValid())
        {
            FreeConstraint(ReservedConstraint);
        }
        return {};
    }

    PxJoint* Joint = FPhysXConstraintBuilder::CreateD6Joint(
        Physics,
        ParentBody->Actor,
        ChildBody->Actor,
        Desc
    );

    if (!Joint)
    {
        if (ReservedConstraint.IsValid())
        {
            FreeConstraint(ReservedConstraint);
        }
        return {};
    }

    FPhysicsConstraintHandle Handle     = ReservedConstraint.IsValid() ? ReservedConstraint : AllocateConstraint();
    FConstraintInstance*     Constraint = ResolveConstraint(Handle);
    if (!Constraint)
    {
        Joint->release();
        if (ReservedConstraint.IsValid())
        {
            FreeConstraint(ReservedConstraint);
        }
        return {};
    }

    Constraint->ParentBody = Parent;
    Constraint->ChildBody = Child;
    Constraint->Joint = Joint;
    Constraint->ParentLocalFrame = Desc.ParentLocalFrame;
    Constraint->ChildLocalFrame = Desc.ChildLocalFrame;
    Constraint->Limits = Desc.Limits;
    Constraint->bDisableCollisionBetweenBodies = Desc.bDisableCollisionBetweenBodies;

    ParentBody->Constraints.push_back(Handle);
    ChildBody->Constraints.push_back(Handle);

    return Handle;
}

void FPhysXPhysicsRuntime::DestroyConstraint_Internal(FPhysicsConstraintHandle Handle)
{
    FConstraintInstance* Constraint = ResolveConstraint(Handle);
    if (!Constraint)
    {
        return;
    }

    if (FBodyInstance* ParentBody = ResolveBody(Constraint->ParentBody))
    {
        ParentBody->Constraints.erase(
            std::remove(ParentBody->Constraints.begin(), ParentBody->Constraints.end(), Handle),
            ParentBody->Constraints.end()
        );
    }

    if (FBodyInstance* ChildBody = ResolveBody(Constraint->ChildBody))
    {
        ChildBody->Constraints.erase(
            std::remove(ChildBody->Constraints.begin(), ChildBody->Constraints.end(), Handle),
            ChildBody->Constraints.end()
        );
    }

    if (Constraint->Joint)
    {
        Constraint->Joint->release();
        Constraint->Joint = nullptr;
    }

    FreeConstraint(Handle);
}

FPhysicsBodyHandle FPhysXPhysicsRuntime::CreateRigidBody(const FBodyCreationDesc& Desc)
{
    if (!Scene)
    {
        return {};
    }

    FBodyCreationDesc LocalDesc = Desc;
    if (!LocalDesc.ReservedBody.IsValid())
    {
        std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
        LocalDesc.ReservedBody = AllocateBody();
    }

    if (!LocalDesc.ReservedBody.IsValid())
    {
        return {};
    }

    FPhysicsBodyCreatePayload Payload;
    Payload.ReservedBody                  = LocalDesc.ReservedBody;
    Payload.ReservedShape                 = LocalDesc.ReservedShape;
    Payload.BodyOwner.ActorId             = LocalDesc.OwnerActorId;
    Payload.BodyOwner.ComponentId         = LocalDesc.OwnerComponentId;
    Payload.BodyOwner.ComponentGeneration = LocalDesc.OwnerComponentGeneration;
    Payload.BodyOwner.BoneName            = LocalDesc.BoneName;
    Payload.BodyOwner.Domain              = LocalDesc.Domain;
    Payload.ShapeOwner                    = Payload.BodyOwner;
    Payload.BodyType                      = LocalDesc.BodyType;
    Payload.SyncMode                      = LocalDesc.SyncMode;
    Payload.WorldTransform                = LocalDesc.WorldTransform;
    Payload.Shapes                        = LocalDesc.Shapes;
    Payload.Mass                          = LocalDesc.Mass;
    Payload.CenterOfMassLocalOffset       = LocalDesc.CenterOfMassLocalOffset;
    Payload.LinearDamping                 = LocalDesc.LinearDamping;
    Payload.AngularDamping                = LocalDesc.AngularDamping;
    Payload.MaxAngularVelocity            = LocalDesc.MaxAngularVelocity;
    Payload.PositionSolverIterationCount  = LocalDesc.PositionSolverIterationCount;
    Payload.VelocitySolverIterationCount  = LocalDesc.VelocitySolverIterationCount;
    Payload.bLockLinearX                  = LocalDesc.bLockLinearX;
    Payload.bLockLinearY                  = LocalDesc.bLockLinearY;
    Payload.bLockLinearZ                  = LocalDesc.bLockLinearZ;
    Payload.bLockAngularX                 = LocalDesc.bLockAngularX;
    Payload.bLockAngularY                 = LocalDesc.bLockAngularY;
    Payload.bLockAngularZ                 = LocalDesc.bLockAngularZ;
    Payload.bEnableGravity                = LocalDesc.bEnableGravity;
    Payload.bEnableCCD                    = LocalDesc.bEnableCCD;
    Payload.bGenerateHitEvents            = LocalDesc.bGenerateHitEvents;
    Payload.bGenerateOverlapEvents        = LocalDesc.bGenerateOverlapEvents;

    FPhysicsCommand Cmd;
    Cmd.Type       = EPhysicsCommandType::CreateBody;
    Cmd.Object     = Payload.BodyOwner;
    Cmd.Body       = Payload.ReservedBody;
    Cmd.Shape      = Payload.ReservedShape;
    Cmd.CreateBody = Payload;
    EnqueueCommand(Cmd);
    return LocalDesc.ReservedBody;
}

void FPhysXPhysicsRuntime::DestroyRigidBody(FPhysicsBodyHandle BodyHandle)
{
    FPhysicsCommand Cmd;
    Cmd.Type = EPhysicsCommandType::DestroyBody;
    Cmd.Body = BodyHandle;
    EnqueueCommand(Cmd);
}

FPhysicsConstraintHandle FPhysXPhysicsRuntime::CreateConstraint(
    FPhysicsBodyHandle             Parent,
    FPhysicsBodyHandle             Child,
    const FConstraintCreationDesc& Desc
)
{
    if (!Scene || !Parent.IsValid() || !Child.IsValid())
    {
        return {};
    }

    FPhysicsConstraintHandle ReservedConstraint;
    {
        std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
        ReservedConstraint = AllocateConstraint();
    }

    if (!ReservedConstraint.IsValid())
    {
        return {};
    }

    FPhysicsCommand Cmd;
    Cmd.Type           = EPhysicsCommandType::CreateConstraint;
    Cmd.Body           = Parent;
    Cmd.ChildBody      = Child;
    Cmd.Constraint     = ReservedConstraint;
    Cmd.ConstraintDesc = Desc;
    EnqueueCommand(Cmd);
    return ReservedConstraint;
}

FPhysicsConstraintHandle FPhysXPhysicsRuntime::CreateFixedJoint(
    FPhysicsBodyHandle Parent,
    const FTransform&  ParentLocalFrame,
    FPhysicsBodyHandle Child,
    const FTransform&  ChildLocalFrame
)
{
    FConstraintCreationDesc Desc;
    Desc.ParentLocalFrame = ParentLocalFrame;
    Desc.ChildLocalFrame  = ChildLocalFrame;

    Desc.Limits.LinearX = EConstraintMotion::Locked;
    Desc.Limits.LinearY = EConstraintMotion::Locked;
    Desc.Limits.LinearZ = EConstraintMotion::Locked;
    Desc.Limits.Twist   = EConstraintMotion::Locked;
    Desc.Limits.Swing1  = EConstraintMotion::Locked;
    Desc.Limits.Swing2  = EConstraintMotion::Locked;

    return CreateConstraint(Parent, Child, Desc);
}

FPhysicsConstraintHandle FPhysXPhysicsRuntime::CreateSphericalJoint(
    FPhysicsBodyHandle          Parent,
    const FTransform&           ParentLocalFrame,
    FPhysicsBodyHandle          Child,
    const FTransform&           ChildLocalFrame,
    const FConstraintLimitDesc& AngularLimits
)
{
    FConstraintCreationDesc Desc;
    Desc.ParentLocalFrame = ParentLocalFrame;
    Desc.ChildLocalFrame  = ChildLocalFrame;
    Desc.Limits           = AngularLimits;

    Desc.Limits.LinearX = EConstraintMotion::Locked;
    Desc.Limits.LinearY = EConstraintMotion::Locked;
    Desc.Limits.LinearZ = EConstraintMotion::Locked;

    return CreateConstraint(Parent, Child, Desc);
}

void FPhysXPhysicsRuntime::DestroyConstraint(FPhysicsConstraintHandle Constraint)
{
    FPhysicsCommand Cmd;
    Cmd.Type       = EPhysicsCommandType::DestroyConstraint;
    Cmd.Constraint = Constraint;
    EnqueueCommand(Cmd);
}

FTransform FPhysXPhysicsRuntime::GetBodyTransform(FPhysicsBodyHandle BodyHandle) const
{
    const std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = AcquireLatestSnapshotRef();
    if (!Snapshot)
    {
        return FTransform();
    }

    const FPhysicsBodySnapshot* Body = Snapshot->FindByBody(BodyHandle);
    return Body ? Body->CurrentTransform : FTransform();
}

std::shared_ptr<const FPhysicsWorldSnapshot> FPhysXPhysicsRuntime::AcquireLatestSnapshotRef() const
{
    std::lock_guard<std::mutex> Lock(WorldSnapshotMutex);
    return PublishedWorldSnapshot;
}

void FPhysXPhysicsRuntime::QueryClothCollisionShapes(
    const FBoundingBox& WorldBounds,
    uint32 ObjectTypeMask,
    TArray<FPhysicsClothCollisionShape>& OutShapes) const
{
    OutShapes.clear();
    if (!WorldBounds.IsValid() || !Scene)
    {
        return;
    }

    std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
    PxSceneReadLock ReadLock(*Scene);
    QueryClothCollisionShapes_Internal(WorldBounds, ObjectTypeMask, OutShapes);
}

void FPhysXPhysicsRuntime::SetBodyTransform(
    FPhysicsBodyHandle   BodyHandle,
    const FTransform&    Transform,
    EPhysicsTeleportMode TeleportMode
)
{
    FPhysicsCommand Cmd;
    Cmd.Type           = EPhysicsCommandType::SetTransform;
    Cmd.Body           = BodyHandle;
    Cmd.TransformValue = Transform;
    Cmd.TeleportMode   = TeleportMode;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplySetBodyTransform_Internal(
    FPhysicsBodyHandle BodyHandle,
    const FTransform&  Transform,
    EPhysicsTeleportMode
)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    Body->Actor->setGlobalPose(ToPxTransform(Transform));
    Body->PreviousTransform = Body->CurrentTransform;
    Body->CurrentTransform = Transform;
}

void FPhysXPhysicsRuntime::SetBodyVelocity(
    FPhysicsBodyHandle BodyHandle,
    const FVector& LinearVelocity,
    const FVector& AngularVelocity
)
{
    FPhysicsCommand Cmd;
    Cmd.Type         = EPhysicsCommandType::SetVelocity;
    Cmd.Body         = BodyHandle;
    Cmd.VectorValue  = LinearVelocity;
    Cmd.VectorValue2 = AngularVelocity;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplySetBodyVelocity_Internal(
    FPhysicsBodyHandle BodyHandle,
    const FVector&     LinearVelocity,
    const FVector&     AngularVelocity
)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    Dynamic->setLinearVelocity(ToPxVec3(LinearVelocity));
    Dynamic->setAngularVelocity(ToPxVec3(AngularVelocity));
    Dynamic->wakeUp();
    Body->LinearVelocity  = LinearVelocity;
    Body->AngularVelocity = AngularVelocity;
}

FVector FPhysXPhysicsRuntime::GetBodyLinearVelocity(FPhysicsBodyHandle BodyHandle) const
{
    const std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = AcquireLatestSnapshotRef();
    if (!Snapshot)
    {
        return FVector::ZeroVector;
    }

    const FPhysicsBodySnapshot* Body = Snapshot->FindByBody(BodyHandle);
    return Body ? Body->LinearVelocity : FVector::ZeroVector;
}

FVector FPhysXPhysicsRuntime::GetBodyAngularVelocity(FPhysicsBodyHandle BodyHandle) const
{
    const std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = AcquireLatestSnapshotRef();
    if (!Snapshot)
    {
        return FVector::ZeroVector;
    }

    const FPhysicsBodySnapshot* Body = Snapshot->FindByBody(BodyHandle);
    return Body ? Body->AngularVelocity : FVector::ZeroVector;
}

void FPhysXPhysicsRuntime::AddForce(FPhysicsBodyHandle BodyHandle, const FVector& Force)
{
    FPhysicsCommand Cmd;
    Cmd.Type        = EPhysicsCommandType::AddForce;
    Cmd.Body        = BodyHandle;
    Cmd.VectorValue = Force;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplyAddForce_Internal(FPhysicsBodyHandle BodyHandle, const FVector& Force)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    Dynamic->addForce(ToPxVec3(Force));
}

void FPhysXPhysicsRuntime::AddForceAtLocation(
    FPhysicsBodyHandle BodyHandle,
    const FVector& Force,
    const FVector& WorldLocation
)
{
    FPhysicsCommand Cmd;
    Cmd.Type         = EPhysicsCommandType::AddForceAtLocation;
    Cmd.Body         = BodyHandle;
    Cmd.VectorValue  = Force;
    Cmd.VectorValue2 = WorldLocation;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplyAddForceAtLocation_Internal(
    FPhysicsBodyHandle BodyHandle,
    const FVector&     Force,
    const FVector&     WorldLocation
)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    PxRigidBodyExt::addForceAtPos(
        *Dynamic,
        ToPxVec3(Force),
        ToPxVec3(WorldLocation)
    );
}

void FPhysXPhysicsRuntime::AddTorque(FPhysicsBodyHandle BodyHandle, const FVector& Torque)
{
    FPhysicsCommand Cmd;
    Cmd.Type        = EPhysicsCommandType::AddTorque;
    Cmd.Body        = BodyHandle;
    Cmd.VectorValue = Torque;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplyAddTorque_Internal(FPhysicsBodyHandle BodyHandle, const FVector& Torque)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    Dynamic->addTorque(ToPxVec3(Torque));
}

void FPhysXPhysicsRuntime::QueueContinuousForceCommand(const FPhysicsCommand& Command, float DurationSeconds)
{
    if (DurationSeconds <= 0.0f || !Command.Body.IsValid())
    {
        return;
    }

    FPendingContinuousForce Pending;
    Pending.Type                 = Command.Type;
    Pending.Body                 = Command.Body;
    Pending.VectorValue          = Command.VectorValue;
    Pending.VectorValue2         = Command.VectorValue2;
    Pending.RemainingTimeSeconds = DurationSeconds;
    PendingContinuousForces.push_back(Pending);
}

void FPhysXPhysicsRuntime::ApplyContinuousForcesForSubstep(float SubstepDt)
{
    if (SubstepDt <= 0.0f || PendingContinuousForces.empty())
    {
        return;
    }

    for (FPendingContinuousForce& Pending : PendingContinuousForces)
    {
        if (Pending.RemainingTimeSeconds <= 0.0f)
        {
            continue;
        }

        const float ConsumeTime = (std::min)(Pending.RemainingTimeSeconds, SubstepDt);
        const float Scale       = ConsumeTime / SubstepDt;

        switch (Pending.Type)
        {
        case EPhysicsCommandType::AddForce:
            ApplyAddForce_Internal(Pending.Body, Pending.VectorValue * Scale);
            break;
        case EPhysicsCommandType::AddForceAtLocation:
            ApplyAddForceAtLocation_Internal(Pending.Body, Pending.VectorValue * Scale, Pending.VectorValue2);
            break;
        case EPhysicsCommandType::AddTorque:
            ApplyAddTorque_Internal(Pending.Body, Pending.VectorValue * Scale);
            break;
        default:
            break;
        }

        Pending.RemainingTimeSeconds -= ConsumeTime;
    }

    PurgeExpiredContinuousForces();
}

void FPhysXPhysicsRuntime::PurgeContinuousForcesForBody(FPhysicsBodyHandle Body)
{
    if (!Body.IsValid() || PendingContinuousForces.empty())
    {
        return;
    }

    PendingContinuousForces.erase(
        std::remove_if(
            PendingContinuousForces.begin(),
            PendingContinuousForces.end(),
            [Body](const FPendingContinuousForce& Pending)
            {
                return Pending.Body == Body;
            }
        ),
        PendingContinuousForces.end()
    );
}

void FPhysXPhysicsRuntime::PurgeExpiredContinuousForces()
{
    PendingContinuousForces.erase(
        std::remove_if(
            PendingContinuousForces.begin(),
            PendingContinuousForces.end(),
            [](const FPendingContinuousForce& Pending)
            {
                return Pending.RemainingTimeSeconds <= 0.0f;
            }
        ),
        PendingContinuousForces.end()
    );
}

void FPhysXPhysicsRuntime::AddImpulse(FPhysicsBodyHandle BodyHandle, const FVector& Impulse)
{
    FPhysicsCommand Cmd;
    Cmd.Type        = EPhysicsCommandType::AddImpulse;
    Cmd.Body        = BodyHandle;
    Cmd.VectorValue = Impulse;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplyAddImpulse_Internal(FPhysicsBodyHandle BodyHandle, const FVector& Impulse)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    Dynamic->addForce(ToPxVec3(Impulse), PxForceMode::eIMPULSE);
}

void FPhysXPhysicsRuntime::SetMass(FPhysicsBodyHandle BodyHandle, float Mass)
{
    FPhysicsCommand Cmd;
    Cmd.Type       = EPhysicsCommandType::SetMass;
    Cmd.Body       = BodyHandle;
    Cmd.FloatValue = Mass;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplySetMass_Internal(FPhysicsBodyHandle BodyHandle, float Mass)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    // 단일 소스(Properties)에 반영하고, 저장된 COM 을 함께 적용해 서로 덮어쓰지 않게 한다.
    Body->Properties.Mass = (Mass > 0.0f) ? Mass : Body->Properties.Mass;

    FBodyCreationDesc Desc;
    Desc.Mass                    = Body->Properties.Mass;
    Desc.CenterOfMassLocalOffset = Body->Properties.CenterOfMassLocalOffset;
    FPhysXBodyBuilder::UpdateMassAndInertia(Body->Actor, Desc);
}

float FPhysXPhysicsRuntime::GetMass(FPhysicsBodyHandle BodyHandle) const
{
    const std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = AcquireLatestSnapshotRef();
    if (!Snapshot)
    {
        return 1.0f;
    }

    const FPhysicsBodySnapshot* Body = Snapshot->FindByBody(BodyHandle);
    return Body ? Body->Mass : 1.0f;
}

void FPhysXPhysicsRuntime::SetCenterOfMass(FPhysicsBodyHandle BodyHandle, const FVector& LocalOffset)
{
    FPhysicsCommand Cmd;
    Cmd.Type        = EPhysicsCommandType::SetCenterOfMass;
    Cmd.Body        = BodyHandle;
    Cmd.VectorValue = LocalOffset;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplySetCenterOfMass_Internal(FPhysicsBodyHandle BodyHandle, const FVector& LocalOffset)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    Body->Properties.CenterOfMassLocalOffset = LocalOffset;

    PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    Dynamic->setCMassLocalPose(PxTransform(ToPxVec3(LocalOffset)));
}

FVector FPhysXPhysicsRuntime::GetCenterOfMass(FPhysicsBodyHandle BodyHandle) const
{
    const std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = AcquireLatestSnapshotRef();
    if (!Snapshot)
    {
        return FVector::ZeroVector;
    }

    const FPhysicsBodySnapshot* Body = Snapshot->FindByBody(BodyHandle);
    return Body ? Body->CenterOfMass : FVector::ZeroVector;
}

void FPhysXPhysicsRuntime::SetLinearLock(FPhysicsBodyHandle BodyHandle, bool bX, bool bY, bool bZ)
{
    FPhysicsCommand Cmd;
    Cmd.Type  = EPhysicsCommandType::SetLinearLock;
    Cmd.Body  = BodyHandle;
    Cmd.BoolX = bX;
    Cmd.BoolY = bY;
    Cmd.BoolZ = bZ;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplySetLinearLock_Internal(FPhysicsBodyHandle BodyHandle, bool bX, bool bY, bool bZ)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    Body->Properties.bLockLinearX = bX;
    Body->Properties.bLockLinearY = bY;
    Body->Properties.bLockLinearZ = bZ;

    PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_X, bX);
    Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_Y, bY);
    Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_Z, bZ);
}

void FPhysXPhysicsRuntime::SetAngularLock(FPhysicsBodyHandle BodyHandle, bool bX, bool bY, bool bZ)
{
    FPhysicsCommand Cmd;
    Cmd.Type  = EPhysicsCommandType::SetAngularLock;
    Cmd.Body  = BodyHandle;
    Cmd.BoolX = bX;
    Cmd.BoolY = bY;
    Cmd.BoolZ = bZ;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ApplySetAngularLock_Internal(FPhysicsBodyHandle BodyHandle, bool bX, bool bY, bool bZ)
{
    FBodyInstance* Body = ResolveAliveBody(BodyHandle);
    if (!Body || !Body->Actor)
    {
        return;
    }

    Body->Properties.bLockAngularX = bX;
    Body->Properties.bLockAngularY = bY;
    Body->Properties.bLockAngularZ = bZ;

    PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, bX);
    Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, bY);
    Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, bZ);
}

void FPhysXPhysicsRuntime::BuildBodySnapshots_Internal(TArray<FPhysicsBodySnapshot>& OutBodies) const
{
    OutBodies.clear();

    for (const auto& BodyPtr : Bodies)
    {
        if (!BodyPtr || !BodyPtr->IsAlive() || !BodyPtr->Actor)
        {
            continue;
        }

        const FBodyInstance& Body = *BodyPtr;

        FPhysicsBodySnapshot Snapshot;
        Snapshot.Body = Body.Handle;

        Snapshot.OwnerActorId             = Body.OwnerActorId;
        Snapshot.OwnerComponentId         = Body.OwnerComponentId;
        Snapshot.OwnerComponentGeneration = Body.OwnerComponentGeneration;

        Snapshot.BoneName = Body.BoneName;
        Snapshot.Domain = Body.Domain;
        Snapshot.SyncMode = Body.SyncMode;
        Snapshot.BodyType = Body.BodyType;

        Snapshot.PreviousTransform = Body.PreviousTransform;
        Snapshot.CurrentTransform  = Body.CurrentTransform;
        Snapshot.LinearVelocity    = Body.LinearVelocity;
        Snapshot.AngularVelocity   = Body.AngularVelocity;
        Snapshot.Mass              = Body.Properties.Mass;
        Snapshot.CenterOfMass      = Body.Properties.CenterOfMassLocalOffset;
        Snapshot.bIsSleeping       = Body.bIsSleeping;

        Snapshot.bIsStatic = Body.BodyType == EPhysicsBodyType::Static;
        Snapshot.bIsDynamic = Body.BodyType == EPhysicsBodyType::Dynamic;
        Snapshot.bIsKinematic = Body.BodyType == EPhysicsBodyType::Kinematic;

        OutBodies.push_back(Snapshot);
    }
}

void FPhysXPhysicsRuntime::BuildDebugBodies_Internal(TArray<FPhysicsDebugBody>& OutBodies) const
{
    OutBodies.clear();

    for (const auto& BodyPtr : Bodies)
    {
        if (!BodyPtr || !BodyPtr->Actor)
        {
            continue;
        }

        const FBodyInstance& Body = *BodyPtr;

        FPhysicsDebugBody DebugBody;
        DebugBody.Body               = Body.Handle;
        DebugBody.BodyWorldTransform = ToFTransform(Body.Actor->getGlobalPose());
        DebugBody.OwnerActorId       = Body.OwnerActorId;
        DebugBody.OwnerComponentId   = Body.OwnerComponentId;
        DebugBody.BoneName           = Body.BoneName;

        DebugBody.bIsStatic = Body.BodyType == EPhysicsBodyType::Static;
        DebugBody.bIsDynamic = Body.BodyType == EPhysicsBodyType::Dynamic;
        DebugBody.bIsKinematic = Body.BodyType == EPhysicsBodyType::Kinematic;

        if (PxRigidDynamic* Dynamic = Body.Actor->is<PxRigidDynamic>())
        {
            DebugBody.bIsSleeping = Dynamic->isSleeping();
        }

        for (FPhysicsShapeHandle ShapeHandle : Body.Shapes)
        {
            const FShapeInstance* ShapeInstance = ResolveShape(ShapeHandle);
            if (!ShapeInstance || ShapeInstance->State != EPhysicsRuntimeObjectState::Alive)
            {
                continue;
            }

            FPhysicsDebugShape DebugShape;
            DebugShape.Shape = ShapeHandle;
            DebugShape.Type = ShapeInstance->Desc.Type;
            DebugShape.BoxHalfExtent = ShapeInstance->Desc.BoxHalfExtent;
            DebugShape.SphereRadius = ShapeInstance->Desc.SphereRadius;
            DebugShape.CapsuleRadius = ShapeInstance->Desc.CapsuleRadius;
            DebugShape.CapsuleHalfHeight = ShapeInstance->Desc.CapsuleHalfHeight;

            // PhysX shape에 실제 적용된 local pose를 body 월드 transform으로 합성한다.
            // Capsule 축 보정 등 PhysX local pose 보정까지 포함되어 debug draw와 실제 충돌이 일치한다.
            DebugShape.WorldTransform = ComposePhysicsTransforms(
                DebugBody.BodyWorldTransform,
                ShapeInstance->PhysXLocalTransform
            );

            DebugBody.Shapes.push_back(DebugShape);
        }

        OutBodies.push_back(DebugBody);
    }
}

void FPhysXPhysicsRuntime::BuildDebugConstraints_Internal(TArray<FPhysicsDebugConstraint>& OutConstraints) const
{
    OutConstraints.clear();

    for (const auto& ConstraintPtr : Constraints)
    {
        if (!ConstraintPtr || !ConstraintPtr->Joint)
        {
            continue;
        }

        const FConstraintInstance& Constraint = *ConstraintPtr;

        const FBodyInstance* Parent = ResolveBody(Constraint.ParentBody);
        const FBodyInstance* Child = ResolveBody(Constraint.ChildBody);

        if (!Parent || !Child || !Parent->Actor || !Child->Actor)
        {
            continue;
        }

        FPhysicsDebugConstraint Debug;
        Debug.Constraint = Constraint.Handle;
        Debug.ParentBody = Constraint.ParentBody;
        Debug.ChildBody = Constraint.ChildBody;

        const FTransform ParentWorld = ToFTransform(Parent->Actor->getGlobalPose());
        const FTransform ChildWorld = ToFTransform(Child->Actor->getGlobalPose());

        // joint frame 의 월드 변환도 동일 규칙 (BodyWorld * LocalFrame) 으로 합성한다.
        Debug.ParentFrameWorld = ComposePhysicsTransforms(ParentWorld, Constraint.ParentLocalFrame);
        Debug.ChildFrameWorld = ComposePhysicsTransforms(ChildWorld, Constraint.ChildLocalFrame);

        Debug.TwistLimitMinDegrees = Constraint.Limits.TwistLimitMinDegrees;
        Debug.TwistLimitMaxDegrees = Constraint.Limits.TwistLimitMaxDegrees;
        Debug.Swing1LimitDegrees = Constraint.Limits.Swing1LimitDegrees;
        Debug.Swing2LimitDegrees = Constraint.Limits.Swing2LimitDegrees;

        OutConstraints.push_back(Debug);
    }
}

void FPhysXPhysicsRuntime::QueryClothCollisionShapes_Internal(
    const FBoundingBox& WorldBounds,
    uint32 ObjectTypeMask,
    TArray<FPhysicsClothCollisionShape>& OutShapes) const
{
    OutShapes.clear();

    for (const auto& BodyPtr : Bodies)
    {
        if (!BodyPtr || !BodyPtr->IsAlive() || !BodyPtr->Actor)
        {
            continue;
        }

        const FBodyInstance& Body = *BodyPtr;
        for (FPhysicsShapeHandle ShapeHandle : Body.Shapes)
        {
            const FShapeInstance* ShapeInstance = ResolveShape(ShapeHandle);
            if (!ShapeInstance || ShapeInstance->State != EPhysicsRuntimeObjectState::Alive)
            {
                continue;
            }

            const FPhysicsShapeDesc& Desc = ShapeInstance->Desc;
            if (Desc.Type != EPhysicsShapeType::Sphere &&
                Desc.Type != EPhysicsShapeType::Capsule &&
                Desc.Type != EPhysicsShapeType::Box)
            {
                continue;
            }

            if (Desc.FilterData.CollisionEnabled == ECollisionEnabled::NoCollision ||
                Desc.FilterData.bIsTrigger ||
                Desc.bIsTrigger)
            {
                continue;
            }

            const uint32 ShapeObjectType = 1u << (Desc.FilterData.ObjectType & PhysicsFilter_ObjectTypeMask);
            if ((ShapeObjectType & ObjectTypeMask) == 0)
            {
                continue;
            }

            const FTransform CurrentShapeWorld =
                ComposePhysicsTransforms(Body.CurrentTransform, ShapeInstance->EngineLocalTransform);
            const FBoundingBox ShapeWorldBounds = BuildClothShapeWorldBounds(
                Desc.Type,
                CurrentShapeWorld,
                Desc.BoxHalfExtent,
                Desc.SphereRadius,
                Desc.CapsuleRadius,
                Desc.CapsuleHalfHeight);
            if (!ShapeWorldBounds.IsValid() || !ShapeWorldBounds.IsIntersected(WorldBounds))
            {
                continue;
            }

            FPhysicsClothCollisionShape ClothShape;
            ClothShape.Type = Desc.Type;
            ClothShape.OwnerActorId = ShapeInstance->SourceActorId;
            ClothShape.OwnerComponentId = ShapeInstance->SourceComponentId;
            ClothShape.OwnerComponentGeneration = ShapeInstance->SourceComponentGeneration;
            ClothShape.Body = Body.Handle;
            ClothShape.Shape = ShapeHandle;
            ClothShape.BodyType = Body.BodyType;
            ClothShape.FilterData = Desc.FilterData;
            ClothShape.PreviousWorldTransform =
                ComposePhysicsTransforms(Body.PreviousTransform, ShapeInstance->EngineLocalTransform);
            ClothShape.CurrentWorldTransform = CurrentShapeWorld;
            ClothShape.WorldBounds = ShapeWorldBounds;
            ClothShape.BoxHalfExtent = Desc.BoxHalfExtent;
            ClothShape.SphereRadius = Desc.SphereRadius;
            ClothShape.CapsuleRadius = Desc.CapsuleRadius;
            ClothShape.CapsuleHalfHeight = Desc.CapsuleHalfHeight;
            OutShapes.push_back(ClothShape);
        }
    }
}

void FPhysXPhysicsRuntime::BuildWorldSnapshot_Internal()
{
    using FClock             = std::chrono::high_resolution_clock;
    const auto SnapshotStart = FClock::now();
    const auto DurationMs    = [](FClock::time_point A, FClock::time_point B)
    {
        return std::chrono::duration<float, std::milli>(B - A).count();
    };

    auto NewWorldSnapshot                = std::make_shared<FPhysicsWorldSnapshot>();
    NewWorldSnapshot->StepIndex          = StepIndex;
    NewWorldSnapshot->FixedDt            = FixedDt;
    NewWorldSnapshot->InterpolationAlpha = Stats.InterpolationAlpha;

    BuildBodySnapshots_Internal(NewWorldSnapshot->Bodies);
    if (VehicleRuntime)
    {
        VehicleRuntime->BuildSnapshots(NewWorldSnapshot->Vehicles);
    }
    else
    {
        NewWorldSnapshot->Vehicles.clear();
    }

    {
        const TArray<FPhysicsBodySnapshot>& Bodies = NewWorldSnapshot->Bodies;
        for (int32 Index = 0; Index < static_cast<int32>(Bodies.size()); ++Index)
        {
            const FPhysicsBodySnapshot& Body                            = Bodies[Index];
            NewWorldSnapshot->BodyToIndex[MakeBodyHandleKey(Body.Body)] = Index;

            if (Body.OwnerComponentId == 0)
            {
                continue;
            }
            if (Body.Domain == EPhysicsBodyDomain::Ragdoll)
            {
                NewWorldSnapshot->ComponentBoneToBodyIndex[
                    MakeComponentBoneKey(Body.OwnerComponentId, Body.BoneName)] = Index;
            }
            else
            {
                NewWorldSnapshot->ComponentToBodyIndex[Body.OwnerComponentId] = Index;
            }
        }
    }

    {
        const TArray<FVehicleSnapshot>& VehicleSnapshots = NewWorldSnapshot->Vehicles;
        for (int32 Index = 0; Index < static_cast<int32>(VehicleSnapshots.size()); ++Index)
        {
            const FVehicleSnapshot& Vehicle = VehicleSnapshots[Index];
            NewWorldSnapshot->VehicleToIndex[MakeVehicleHandleKey(Vehicle.Vehicle)] = Index;

            if (Vehicle.OwnerComponentId != 0)
            {
                NewWorldSnapshot->ComponentToVehicleIndex[Vehicle.OwnerComponentId] = Index;
            }
        }
    }

    if (bDebugSnapshotEnabled)
    {
        BuildDebugBodies_Internal(NewWorldSnapshot->DebugBodies);
        BuildDebugConstraints_Internal(NewWorldSnapshot->DebugConstraints);
    }
    else
    {
        NewWorldSnapshot->DebugBodies.clear();
        NewWorldSnapshot->DebugConstraints.clear();
    }

    FPhysicsStats SnapshotStats   = Stats;
    SnapshotStats.BuildSnapshotMs = DurationMs(SnapshotStart, FClock::now());
    NewWorldSnapshot->Stats       = SnapshotStats;
    Stats.BuildSnapshotMs         = SnapshotStats.BuildSnapshotMs;

    FPhysicsDebugSnapshot NewDebugSnapshot;
    NewDebugSnapshot.Bodies             = NewWorldSnapshot->DebugBodies;
    NewDebugSnapshot.Constraints        = NewWorldSnapshot->DebugConstraints;
    NewDebugSnapshot.Stats              = NewWorldSnapshot->Stats;
    NewDebugSnapshot.InterpolationAlpha = NewWorldSnapshot->InterpolationAlpha;
    NewDebugSnapshot.StepIndex          = NewWorldSnapshot->StepIndex;

    {
        std::lock_guard<std::mutex> Lock(WorldSnapshotMutex);
        PublishedWorldSnapshot = std::move(NewWorldSnapshot);
    }

    {
        std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
        DebugSnapshot = std::move(NewDebugSnapshot);
    }
}

void FPhysXPhysicsRuntime::BuildDebugSnapshot_Internal()
{
    BuildWorldSnapshot_Internal();
}

void FPhysXPhysicsRuntime::GatherDebugBodies(TArray<FPhysicsDebugBody>& OutBodies) const
{
    std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
    OutBodies = DebugSnapshot.Bodies;
}

void FPhysXPhysicsRuntime::GatherDebugConstraints(TArray<FPhysicsDebugConstraint>& OutConstraints) const
{
    std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
    OutConstraints = DebugSnapshot.Constraints;
}

void FPhysXPhysicsRuntime::GetDebugSnapshot(FPhysicsDebugSnapshot& OutSnapshot) const
{
    std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
    OutSnapshot = DebugSnapshot;
}

FVehicleHandle FPhysXPhysicsRuntime::ReserveVehicleHandle_GameThread()
{
    std::lock_guard<std::mutex> StateLock(RuntimeStateMutex);
    if (!VehicleRuntime)
    {
        VehicleRuntime = std::make_unique<FPhysXVehicleRuntime>();
        VehicleRuntime->Initialize(Physics, Scene, DefaultMaterial);
    }
    return VehicleRuntime->ReserveHandle();
}

void FPhysXPhysicsRuntime::CreateVehicle(const FVehicleDesc& Desc)
{
    FPhysicsCommand Cmd;
    Cmd.Type        = EPhysicsCommandType::CreateVehicle;
    Cmd.Object      = Desc.Owner;
    Cmd.Vehicle     = Desc.ReservedVehicle;
    Cmd.VehicleDesc = Desc;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::DestroyVehicle(FVehicleHandle Vehicle)
{
    FPhysicsCommand Cmd;
    Cmd.Type    = EPhysicsCommandType::DestroyVehicle;
    Cmd.Vehicle = Vehicle;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::SetVehicleInput(FVehicleHandle Vehicle, const FVehicleInputState& Input)
{
    FPhysicsCommand Cmd;
    Cmd.Type         = EPhysicsCommandType::SetVehicleInput;
    Cmd.Vehicle      = Vehicle;
    Cmd.VehicleInput = Input;
    EnqueueCommand(Cmd);
}

void FPhysXPhysicsRuntime::ResetVehicle(FVehicleHandle Vehicle, const FTransform& WorldTransform)
{
    FPhysicsCommand Cmd;
    Cmd.Type           = EPhysicsCommandType::ResetVehicle;
    Cmd.Vehicle        = Vehicle;
    Cmd.TransformValue = WorldTransform;
    EnqueueCommand(Cmd);
}

FPhysicsStats FPhysXPhysicsRuntime::GetStats() const
{
    std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
    return DebugSnapshot.Stats;
}

void FPhysXPhysicsRuntime::RecordRaycastQuery()
{
    {
        std::lock_guard<std::mutex> Lock(ExternalStatsMutex);
        ++PendingRaycastQueries;
    }
    {
        std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
        ++DebugSnapshot.Stats.NumRaycasts;
    }
}

void FPhysXPhysicsRuntime::SetEventStats(int32 NumContactPairs, int32 NumTriggerPairs, float DispatchEventMs)
{
    {
        std::lock_guard<std::mutex> Lock(ExternalStatsMutex);
        LastContactPairs    = NumContactPairs;
        LastTriggerPairs    = NumTriggerPairs;
        LastDispatchEventMs = DispatchEventMs;
    }
    {
        std::lock_guard<std::mutex> Lock(DebugSnapshotMutex);
        DebugSnapshot.Stats.NumContactPairs = NumContactPairs;
        DebugSnapshot.Stats.NumTriggerPairs = NumTriggerPairs;
        DebugSnapshot.Stats.DispatchEventMs = DispatchEventMs;
    }
}

FPhysicsBodyHandle FPhysXPhysicsRuntime::AllocateBody()
{
    for (uint32 i = 0; i < static_cast<uint32>(Bodies.size()); ++i)
    {
        if (!Bodies[i])
        {
            Bodies[i] = std::make_unique<FBodyInstance>();
            FPhysicsBodyHandle Handle;
            Handle.Index = i;
            Handle.Generation = BodyGenerations[i];
            Bodies[i]->Handle = Handle;
            return Handle;
        }
    }

    const uint32 NewIndex = static_cast<uint32>(Bodies.size());
    Bodies.push_back(std::make_unique<FBodyInstance>());
    BodyGenerations.push_back(1);

    FPhysicsBodyHandle Handle;
    Handle.Index = NewIndex;
    Handle.Generation = BodyGenerations[NewIndex];
    Bodies[NewIndex]->Handle = Handle;
    return Handle;
}

FPhysicsShapeHandle FPhysXPhysicsRuntime::AllocateShape()
{
    for (uint32 i = 0; i < static_cast<uint32>(Shapes.size()); ++i)
    {
        if (!Shapes[i])
        {
            Shapes[i] = std::make_unique<FShapeInstance>();
            FPhysicsShapeHandle Handle;
            Handle.Index = i;
            Handle.Generation = ShapeGenerations[i];
            Shapes[i]->Handle = Handle;
            return Handle;
        }
    }

    const uint32 NewIndex = static_cast<uint32>(Shapes.size());
    Shapes.push_back(std::make_unique<FShapeInstance>());
    ShapeGenerations.push_back(1);

    FPhysicsShapeHandle Handle;
    Handle.Index = NewIndex;
    Handle.Generation = ShapeGenerations[NewIndex];
    Shapes[NewIndex]->Handle = Handle;
    return Handle;
}

FPhysicsConstraintHandle FPhysXPhysicsRuntime::AllocateConstraint()
{
    for (uint32 i = 0; i < static_cast<uint32>(Constraints.size()); ++i)
    {
        if (!Constraints[i])
        {
            Constraints[i] = std::make_unique<FConstraintInstance>();
            FPhysicsConstraintHandle Handle;
            Handle.Index = i;
            Handle.Generation = ConstraintGenerations[i];
            Constraints[i]->Handle = Handle;
            return Handle;
        }
    }

    const uint32 NewIndex = static_cast<uint32>(Constraints.size());
    Constraints.push_back(std::make_unique<FConstraintInstance>());
    ConstraintGenerations.push_back(1);

    FPhysicsConstraintHandle Handle;
    Handle.Index = NewIndex;
    Handle.Generation = ConstraintGenerations[NewIndex];
    Constraints[NewIndex]->Handle = Handle;
    return Handle;
}

FBodyInstance* FPhysXPhysicsRuntime::ResolveBody(FPhysicsBodyHandle Handle)
{
    if (!IsHandleIndexValid(Handle.Index, Bodies.size()) ||
        BodyGenerations[Handle.Index] != Handle.Generation)
    {
        return nullptr;
    }

    return Bodies[Handle.Index].get();
}

const FBodyInstance* FPhysXPhysicsRuntime::ResolveBody(FPhysicsBodyHandle Handle) const
{
    if (!IsHandleIndexValid(Handle.Index, Bodies.size()) ||
        BodyGenerations[Handle.Index] != Handle.Generation)
    {
        return nullptr;
    }

    return Bodies[Handle.Index].get();
}

FBodyInstance* FPhysXPhysicsRuntime::ResolveAliveBody(FPhysicsBodyHandle Handle)
{
    FBodyInstance* Body = ResolveBody(Handle);
    return (Body && Body->State == EPhysicsRuntimeObjectState::Alive) ? Body : nullptr;
}

const FBodyInstance* FPhysXPhysicsRuntime::ResolveAliveBody(FPhysicsBodyHandle Handle) const
{
    const FBodyInstance* Body = ResolveBody(Handle);
    return (Body && Body->State == EPhysicsRuntimeObjectState::Alive) ? Body : nullptr;
}

FShapeInstance* FPhysXPhysicsRuntime::ResolveShape(FPhysicsShapeHandle Handle)
{
    if (!IsHandleIndexValid(Handle.Index, Shapes.size()) ||
        ShapeGenerations[Handle.Index] != Handle.Generation)
    {
        return nullptr;
    }

    return Shapes[Handle.Index].get();
}

const FShapeInstance* FPhysXPhysicsRuntime::ResolveShape(FPhysicsShapeHandle Handle) const
{
    if (!IsHandleIndexValid(Handle.Index, Shapes.size()) ||
        ShapeGenerations[Handle.Index] != Handle.Generation)
    {
        return nullptr;
    }

    return Shapes[Handle.Index].get();
}

FConstraintInstance* FPhysXPhysicsRuntime::ResolveConstraint(FPhysicsConstraintHandle Handle)
{
    if (!IsHandleIndexValid(Handle.Index, Constraints.size()) ||
        ConstraintGenerations[Handle.Index] != Handle.Generation)
    {
        return nullptr;
    }

    return Constraints[Handle.Index].get();
}

const FConstraintInstance* FPhysXPhysicsRuntime::ResolveConstraint(FPhysicsConstraintHandle Handle) const
{
    if (!IsHandleIndexValid(Handle.Index, Constraints.size()) ||
        ConstraintGenerations[Handle.Index] != Handle.Generation)
    {
        return nullptr;
    }

    return Constraints[Handle.Index].get();
}

void FPhysXPhysicsRuntime::FreeBody(FPhysicsBodyHandle Handle)
{
    if (!IsHandleIndexValid(Handle.Index, Bodies.size()))
    {
        return;
    }

    Bodies[Handle.Index].reset();
    ++BodyGenerations[Handle.Index];
}

void FPhysXPhysicsRuntime::FreeShape(FPhysicsShapeHandle Handle)
{
    if (!IsHandleIndexValid(Handle.Index, Shapes.size()))
    {
        return;
    }

    Shapes[Handle.Index].reset();
    ++ShapeGenerations[Handle.Index];
}

void FPhysXPhysicsRuntime::FreeConstraint(FPhysicsConstraintHandle Handle)
{
    if (!IsHandleIndexValid(Handle.Index, Constraints.size()))
    {
        return;
    }

    Constraints[Handle.Index].reset();
    ++ConstraintGenerations[Handle.Index];
}

FPhysicsShapeHandle FPhysXPhysicsRuntime::AddShapeToBody(
    FPhysicsBodyHandle       BodyHandle,
    uint32                   SourceComponentId,
    uint32                   SourceActorId,
    uint32                   SourceGeneration,
    const FPhysicsShapeDesc& Desc,
    FPhysicsShapeHandle      ReservedShape
)
{
    FBodyInstance* Body = ResolveBody(BodyHandle);
    if (!Body || !Body->Actor || !Physics || !DefaultMaterial)
    {
        return {};
    }

    FPhysicsShapeDesc ShapeDesc = Desc;
    ShapeDesc.FilterData.bEnableCCD = ShapeDesc.FilterData.bEnableCCD || Body->Properties.bEnableCCD;

    if (ShapeDesc.FilterData.bEnableCCD)
    {
        Body->Properties.bEnableCCD = true;
        if (PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>())
        {
            Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        }
    }

    PxShape* Shape = FPhysXBodyBuilder::CreateShape(Physics, DefaultMaterial, ShapeDesc);
    if (!Shape)
    {
        return {};
    }
    ShapeDesc.TriangleMeshData.reset();

    Body->Actor->attachShape(*Shape);

    FPhysicsShapeHandle ShapeHandle   = ReservedShape.IsValid() ? ReservedShape : AllocateShape();
    FShapeInstance*     ShapeInstance = ResolveShape(ShapeHandle);
    if (!ShapeInstance && !ReservedShape.IsValid())
    {
        ShapeHandle   = AllocateShape();
        ShapeInstance = ResolveShape(ShapeHandle);
    }
    if (!ShapeInstance)
    {
        Body->Actor->detachShape(*Shape);
        Shape->release();
        return {};
    }

    ShapeInstance->OwnerBody                 = BodyHandle;
    ShapeInstance->SourceComponentId         = SourceComponentId;
    ShapeInstance->SourceActorId             = SourceActorId;
    ShapeInstance->SourceComponentGeneration = SourceGeneration;
    ShapeInstance->Desc                      = ShapeDesc;
    ShapeInstance->EngineLocalTransform      = ShapeDesc.LocalTransform;
    ShapeInstance->PhysXLocalTransform       = ToFTransform(Shape->getLocalPose());
    ShapeInstance->Shape                     = Shape;
    ShapeInstance->State                     = EPhysicsRuntimeObjectState::Alive;

    Shape->userData = ShapeInstance;
    Body->Shapes.push_back(ShapeHandle);

    // createShape() returns an owning reference. attachShape() adds the actor's
    // reference, so drop the local reference here and let the actor own the
    // shape until detach/release. ShapeInstance keeps a non-owning pointer.
    Shape->release();

    return ShapeHandle;
}

void FPhysXPhysicsRuntime::DetachShape(FPhysicsShapeHandle ShapeHandle)
{
    FShapeInstance* ShapeInstance = ResolveShape(ShapeHandle);
    if (!ShapeInstance)
    {
        return;
    }

    FBodyInstance* Body = ResolveBody(ShapeInstance->OwnerBody);
    if (Body && Body->Actor && ShapeInstance->Shape)
    {
        // detach 하면 actor 의 마지막 ref 가 빠지면서 PxShape 가 즉시 release 될 수 있으므로,
        // 그 전에 userData 를 끊어 지연된 callback/query 의 stale 역참조를 막는다.
        ShapeInstance->Shape->userData = nullptr;

        Body->Actor->detachShape(*ShapeInstance->Shape);
        Body->Shapes.erase(
            std::remove(Body->Shapes.begin(), Body->Shapes.end(), ShapeHandle),
            Body->Shapes.end()
        );

        bool bAnyRemainingShapeNeedsCCD = false;
        for (FPhysicsShapeHandle RemainingShapeHandle : Body->Shapes)
        {
            const FShapeInstance* RemainingShape = ResolveShape(RemainingShapeHandle);
            if (RemainingShape && RemainingShape->Desc.FilterData.bEnableCCD)
            {
                bAnyRemainingShapeNeedsCCD = true;
                break;
            }
        }

        Body->Properties.bEnableCCD = bAnyRemainingShapeNeedsCCD;
        if (PxRigidDynamic* Dynamic = Body->Actor->is<PxRigidDynamic>())
        {
            Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, bAnyRemainingShapeNeedsCCD);
        }
    }

    if (ShapeInstance->SourceComponentId != 0)
    {
        ComponentToShape.erase(ShapeInstance->SourceComponentId);
    }
    ShapeInstance->State = EPhysicsRuntimeObjectState::Destroyed;

    FreeShape(ShapeHandle);
}

void FPhysXPhysicsRuntime::DetachShapeForComponentId(uint32 ComponentId)
{
    auto It = ComponentToShape.find(ComponentId);
    if (It == ComponentToShape.end())
    {
        return;
    }

    DetachShape(It->second);
}

void FPhysXPhysicsRuntime::SyncEngineToPhysics(FBodyInstance& Body)
{
    if (!Body.Actor)
    {
        return;
    }

    const FTransform  Target   = Body.CurrentTransform;
    const PxTransform PxTarget = ToPxTransform(Target);

    if (PxRigidDynamic* Dynamic = Body.Actor->is<PxRigidDynamic>())
    {
        if (Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
        {
            Dynamic->setKinematicTarget(PxTarget);
        }
        else
        {
            Dynamic->setGlobalPose(PxTarget);
        }
    }
    else
    {
        Body.Actor->setGlobalPose(PxTarget);
    }

    Body.PreviousTransform = Body.CurrentTransform;
    Body.CurrentTransform = Target;
}

void FPhysXPhysicsRuntime::CachePhysicsResult(FBodyInstance& Body)
{
    if (!Body.Actor)
    {
        return;
    }

    Body.PreviousTransform = Body.CurrentTransform;
    Body.CurrentTransform  = ToFTransform(Body.Actor->getGlobalPose());

    if (PxRigidDynamic* Dynamic = Body.Actor->is<PxRigidDynamic>())
    {
        Body.LinearVelocity  = ToFVector(Dynamic->getLinearVelocity());
        Body.AngularVelocity = ToFVector(Dynamic->getAngularVelocity());
        Body.bIsSleeping     = Dynamic->isSleeping();
    }
    else
    {
        Body.LinearVelocity  = FVector::ZeroVector;
        Body.AngularVelocity = FVector::ZeroVector;
        Body.bIsSleeping     = false;
    }
}

void FPhysXPhysicsRuntime::EnqueueCommand(const FPhysicsCommand& Command)
{
    CommandQueue.Enqueue(Command);
}

int32 FPhysXPhysicsRuntime::ApplyPendingCommands()
{
    TArray<FPhysicsCommand> Commands;
    CommandQueue.Drain(Commands);
    return ApplyCommands(Commands, 0.0f);
}

int32 FPhysXPhysicsRuntime::ApplyCommands(const TArray<FPhysicsCommand>& Commands, float ContinuousForceDurationSeconds)
{
    // Commands are sealed by the Game Thread at SubmitPhysicsFrame().
    // Do not touch CommandQueue here; otherwise async physics can consume commands for a later game frame.
    for (const FPhysicsCommand& Command : Commands)
    {
        switch (Command.Type)
        {
        case EPhysicsCommandType::CreateBody:
        {
            bool bSuccess = false;
            if (Command.CreateBody.BodyOwner.Domain == EPhysicsBodyDomain::ActorComponent &&
                Command.CreateBody.ShapeOwner.Domain == EPhysicsBodyDomain::ActorComponent)
            {
                RegisterComponent_Internal(Command.CreateBody);
                const FShapeInstance* Shape = ResolveShape(Command.Shape);
                bSuccess                    = Shape && Shape->SourceComponentId == Command.CreateBody.ShapeOwner.ComponentId &&
                        Shape->SourceComponentGeneration == Command.CreateBody.ShapeOwner.ComponentGeneration;
            }
            else
            {
                const FPhysicsBodyHandle CreatedBody = CreateRigidBody_Internal(ToBodyCreationDesc(Command.CreateBody));
                bSuccess                             = CreatedBody.IsValid();
            }

            FPhysicsCreationResult Result;
            Result.Type     = EPhysicsCreationResultType::Body;
            Result.Owner    = Command.CreateBody.ShapeOwner.ComponentId ? Command.CreateBody.ShapeOwner : Command.CreateBody.BodyOwner;
            Result.Body     = Command.Body;
            Result.Shape    = Command.Shape;
            Result.bSuccess = bSuccess;
            QueueCreationResult(Result);
            break;
        }
        case EPhysicsCommandType::CreateConstraint:
        {
            const FPhysicsConstraintHandle CreatedConstraint = CreateConstraint_Internal(Command.Body, Command.ChildBody, Command.ConstraintDesc, Command.Constraint);
            FPhysicsCreationResult         Result;
            Result.Type       = EPhysicsCreationResultType::Constraint;
            Result.Body       = Command.Body;
            Result.Constraint = Command.Constraint;
            Result.bSuccess   = CreatedConstraint.IsValid();
            QueueCreationResult(Result);
            break;
        }
        case EPhysicsCommandType::UnregisterComponent:
            UnregisterComponent_Internal(Command.Object);
            break;
        case EPhysicsCommandType::RebuildComponentBody:
            // Rebuild is expanded into ordered unregister/create commands by the Game Thread façade.
            break;
        case EPhysicsCommandType::DestroyBody:
            DestroyRigidBody_Internal(Command.Body);
            break;
        case EPhysicsCommandType::DestroyConstraint:
            DestroyConstraint_Internal(Command.Constraint);
            break;
        case EPhysicsCommandType::SetTransform:
        case EPhysicsCommandType::SetKinematicTarget:
            ApplySetBodyTransform_Internal(Command.Body, Command.TransformValue, Command.TeleportMode);
            break;
        case EPhysicsCommandType::AddForce:
            QueueContinuousForceCommand(Command, Command.DurationSeconds > 0.0f ? Command.DurationSeconds : ContinuousForceDurationSeconds);
            break;
        case EPhysicsCommandType::AddForceAtLocation:
            QueueContinuousForceCommand(Command, Command.DurationSeconds > 0.0f ? Command.DurationSeconds : ContinuousForceDurationSeconds);
            break;
        case EPhysicsCommandType::AddTorque:
            QueueContinuousForceCommand(Command, Command.DurationSeconds > 0.0f ? Command.DurationSeconds : ContinuousForceDurationSeconds);
            break;
        case EPhysicsCommandType::AddImpulse:
            ApplyAddImpulse_Internal(Command.Body, Command.VectorValue);
            break;
        case EPhysicsCommandType::SetVelocity:
            ApplySetBodyVelocity_Internal(Command.Body, Command.VectorValue, Command.VectorValue2);
            break;
        case EPhysicsCommandType::SetLinearVelocity:
        {
            const FBodyInstance* Body           = ResolveAliveBody(Command.Body);
            const FVector        CurrentAngular = Body ? Body->AngularVelocity : FVector::ZeroVector;
            ApplySetBodyVelocity_Internal(Command.Body, Command.VectorValue, CurrentAngular);
            break;
        }
        case EPhysicsCommandType::SetAngularVelocity:
        {
            const FBodyInstance* Body          = ResolveAliveBody(Command.Body);
            const FVector        CurrentLinear = Body ? Body->LinearVelocity : FVector::ZeroVector;
            ApplySetBodyVelocity_Internal(Command.Body, CurrentLinear, Command.VectorValue);
            break;
        }
        case EPhysicsCommandType::SetMass:
            ApplySetMass_Internal(Command.Body, Command.FloatValue);
            break;
        case EPhysicsCommandType::SetCenterOfMass:
            ApplySetCenterOfMass_Internal(Command.Body, Command.VectorValue);
            break;
        case EPhysicsCommandType::SetLinearLock:
            ApplySetLinearLock_Internal(Command.Body, Command.BoolX, Command.BoolY, Command.BoolZ);
            break;
        case EPhysicsCommandType::SetAngularLock:
            ApplySetAngularLock_Internal(Command.Body, Command.BoolX, Command.BoolY, Command.BoolZ);
            break;
        case EPhysicsCommandType::CreateVehicle:
            if (VehicleRuntime) { VehicleRuntime->CreateVehicle(Command.VehicleDesc); }
            break;
        case EPhysicsCommandType::DestroyVehicle:
            if (VehicleRuntime) { VehicleRuntime->DestroyVehicle(Command.Vehicle); }
            break;
        case EPhysicsCommandType::SetVehicleInput:
            if (VehicleRuntime) { VehicleRuntime->SetVehicleInput(Command.Vehicle, Command.VehicleInput); }
            break;
        case EPhysicsCommandType::ResetVehicle:
            if (VehicleRuntime) { VehicleRuntime->ResetVehicle(Command.Vehicle, Command.TransformValue); }
            break;
        default:
            break;
        }
    }

    return static_cast<int32>(Commands.size());
}

void FPhysXPhysicsRuntime::UpdateStats()
{
    const int32 NumSubsteps = Stats.NumSubsteps;
    Stats = FPhysicsStats();
    Stats.NumSubsteps = NumSubsteps;
    Stats.NumDeferredDestroys = FrameDeferredDestroys;

    {
        std::lock_guard<std::mutex> Lock(ExternalStatsMutex);
        Stats.NumRaycasts     = PendingRaycastQueries;
        Stats.NumContactPairs = LastContactPairs;
        Stats.NumTriggerPairs = LastTriggerPairs;
        Stats.DispatchEventMs = LastDispatchEventMs;

        PendingRaycastQueries = 0;
    }

    for (const auto& BodyPtr : Bodies)
    {
        if (!BodyPtr || !BodyPtr->Actor)
        {
            continue;
        }

        ++Stats.NumBodies;

        if (BodyPtr->BodyType == EPhysicsBodyType::Static)
        {
            ++Stats.NumStaticBodies;
        }
        else if (BodyPtr->BodyType == EPhysicsBodyType::Dynamic)
        {
            ++Stats.NumDynamicBodies;
        }
        else if (BodyPtr->BodyType == EPhysicsBodyType::Kinematic)
        {
            ++Stats.NumKinematicBodies;
        }

        Stats.NumShapes += static_cast<int32>(BodyPtr->Shapes.size());

        if (PxRigidDynamic* Dynamic = BodyPtr->Actor->is<PxRigidDynamic>())
        {
            if (Dynamic->isSleeping())
            {
                ++Stats.NumSleepingBodies;
            }
            else
            {
                ++Stats.NumActiveBodies;
            }
        }
    }

    for (const auto& ConstraintPtr : Constraints)
    {
        if (ConstraintPtr && ConstraintPtr->Joint)
        {
            ++Stats.NumConstraints;
        }
    }

    if (VehicleRuntime)
    {
        VehicleRuntime->GatherStats(Stats);
    }

}

FActorCompoundBody* FPhysXPhysicsRuntime::FindCompoundByActorId(uint32 ActorId)
{
    if (ActorId == 0)
    {
        return nullptr;
    }

    auto It = ActorCompounds.find(ActorId);
    return It != ActorCompounds.end() ? &It->second : nullptr;
}

const FActorCompoundBody* FPhysXPhysicsRuntime::FindCompoundByActorId(uint32 ActorId) const
{
    if (ActorId == 0)
    {
        return nullptr;
    }

    auto It = ActorCompounds.find(ActorId);
    return It != ActorCompounds.end() ? &It->second : nullptr;
}

