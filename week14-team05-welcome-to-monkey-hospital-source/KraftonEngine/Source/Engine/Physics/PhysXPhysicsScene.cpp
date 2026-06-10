#include "Physics/PhysXPhysicsScene.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Core/ProjectSettings.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Object/Object.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Physics/PhysicsBodyInstance.h"
#include "Physics/PhysicsShapeInstance.h"
#include "Physics/PhysXConversion.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleSDK.h>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace physx;

// ============================================================
// PhysX Error Callback
// ============================================================
class FPhysXErrorCallback : public PxErrorCallback
{
public:
    void reportError(PxErrorCode::Enum code, const char* message,
        const char* file, int line) override
    {
        const char* severity = "Info";
        if (code == PxErrorCode::eABORT || code == PxErrorCode::eOUT_OF_MEMORY)
        {
            severity = "Fatal";
        }
        else if (code == PxErrorCode::eINTERNAL_ERROR || code == PxErrorCode::eINVALID_OPERATION)
        {
            severity = "Error";
        }
        else if (code == PxErrorCode::eINVALID_PARAMETER || code == PxErrorCode::ePERF_WARNING)
        {
            severity = "Warning";
        }
        else if (code == PxErrorCode::eDEBUG_WARNING)
        {
            severity = "Warning";
        }

        UE_LOG("[PhysX %s] %s (%s:%d)", severity, message, file, line);
    }
};

static FPhysXErrorCallback GPhysXErrorCallback;
static PxDefaultAllocator GPhysXAllocator;

// ============================================================
// PhysX Foundation/Physics singleton
// ============================================================
static PxFoundation* GSharedFoundation            = nullptr;
static PxPhysics*    GSharedPhysics               = nullptr;
static bool          GSharedExtensionsInitialized = false;
static int32         GSharedRefCount              = 0;
static bool          GSharedVehicleSDKInitialized = false;

static void AcquireSharedPhysX(PxFoundation*& OutFoundation, PxPhysics*& OutPhysics)
{
    if (GSharedRefCount == 0)
    {
        GSharedFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GPhysXAllocator, GPhysXErrorCallback);
        if (GSharedFoundation)
        {
            GSharedPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *GSharedFoundation, PxTolerancesScale());
            if (GSharedPhysics)
            {
                GSharedExtensionsInitialized = PxInitExtensions(*GSharedPhysics, nullptr);
                if (!GSharedExtensionsInitialized)
                {
                    UE_LOG("[PhysX] Failed to initialize PhysX extensions");
                    GSharedPhysics->release();
                    GSharedPhysics = nullptr;
                    GSharedFoundation->release();
                    GSharedFoundation = nullptr;
                }
                else if (!PxInitVehicleSDK(*GSharedPhysics))
                {
                    UE_LOG("[PhysX] Failed to initialize PhysX Vehicle SDK");
                    PxCloseExtensions();
                    GSharedExtensionsInitialized = false;
                    GSharedPhysics->release();
                    GSharedPhysics = nullptr;
                    GSharedFoundation->release();
                    GSharedFoundation = nullptr;
                }
                else
                {
                    PxVehicleSetBasisVectors(PxVec3(0.0f, 0.0f, 1.0f), PxVec3(1.0f, 0.0f, 0.0f));
                    PxVehicleSetUpdateMode(PxVehicleUpdateMode::eVELOCITY_CHANGE);
                    GSharedVehicleSDKInitialized = true;
                }
            }
        }
    }

    if (!GSharedFoundation || !GSharedPhysics)
    {
        OutFoundation = nullptr;
        OutPhysics    = nullptr;
        return;
    }

    ++GSharedRefCount;
    OutFoundation = GSharedFoundation;
    OutPhysics    = GSharedPhysics;
}

static void ReleaseSharedPhysX()
{
    if (GSharedRefCount <= 0)
    {
        return;
    }

    --GSharedRefCount;
    if (GSharedRefCount == 0)
    {
        if (GSharedVehicleSDKInitialized)
        {
            PxCloseVehicleSDK();
            GSharedVehicleSDKInitialized = false;
        }
        if (GSharedExtensionsInitialized)
        {
            PxCloseExtensions();
            GSharedExtensionsInitialized = false;
        }
        if (GSharedPhysics)
        {
            GSharedPhysics->release();
            GSharedPhysics = nullptr;
        }
        if (GSharedFoundation)
        {
            GSharedFoundation->release();
            GSharedFoundation = nullptr;
        }
    }
}

namespace
{
    FShapeInstance* GetShapeInstance(const PxShape* Shape)
    {
        return Shape && Shape->userData
            ? static_cast<FShapeInstance*>(Shape->userData)
            : nullptr;
    }

    FBodyInstance* GetBodyInstance(const PxRigidActor* Actor)
    {
        return Actor && Actor->userData
            ? static_cast<FBodyInstance*>(Actor->userData)
            : nullptr;
    }

    uint32 GetComponentIdFromShape(const PxShape* Shape)
    {
        FShapeInstance* ShapeInstance = GetShapeInstance(Shape);
        return (ShapeInstance && ShapeInstance->State == EPhysicsRuntimeObjectState::Alive)
                ? ShapeInstance->SourceComponentId
                : 0;
    }

    uint32 GetComponentGenerationFromShape(const PxShape* Shape)
    {
        FShapeInstance* ShapeInstance = GetShapeInstance(Shape);
        return (ShapeInstance && ShapeInstance->State == EPhysicsRuntimeObjectState::Alive)
                ? ShapeInstance->SourceComponentGeneration
                : 0;
    }

    uint32 GetActorIdFromShape(const PxShape* Shape)
    {
        FShapeInstance* ShapeInstance = GetShapeInstance(Shape);
        return (ShapeInstance && ShapeInstance->State == EPhysicsRuntimeObjectState::Alive)
                ? ShapeInstance->SourceActorId
                : 0;
    }

    uint32 GetActorIdFromActor(const PxRigidActor* Actor)
    {
        FBodyInstance* Body = GetBodyInstance(Actor);
        return Body ? Body->OwnerActorId : 0;
    }

    void FillRaycastPhysicsHit(const PxRaycastHit& Block, FPhysicsRaycastResult& OutResult)
    {
        if (Block.shape)
        {
            OutResult.HitComponentId = GetComponentIdFromShape(Block.shape);
            OutResult.HitActorId     = GetActorIdFromShape(Block.shape);
            OutResult.HitGeneration  = GetComponentGenerationFromShape(Block.shape);
        }

        FBodyInstance* Body = GetBodyInstance(Block.actor);
        if (Body && Body->State == EPhysicsRuntimeObjectState::Alive)
        {
            OutResult.HitBody     = Body->Handle;
            OutResult.HitBoneName = Body->BoneName;
            OutResult.HitDomain   = Body->Domain;
            if (OutResult.HitActorId == 0)
            {
                OutResult.HitActorId = Body->OwnerActorId;
            }
        }
        else if (OutResult.HitActorId == 0 && Block.actor)
        {
            OutResult.HitActorId = GetActorIdFromActor(Block.actor);
        }
    }

    ECollisionResponse GetPackedMinResponse(const PxFilterData& A, const PxFilterData& B);
    EPhysicsCollisionRole GetPackedCollisionRole(const PxFilterData& Data);
    EPhysicsGameplayOverlapOwnership GetPackedGameplayOverlapOwnership(const PxFilterData& Data);

    bool IsSceneSocketAttachmentNameSet_GameThread(const FName& SocketName)
    {
        return SocketName.IsValid() && SocketName != FName::None;
    }

    FQuat GetSceneComponentWorldRotationNoScale_GameThread(const USceneComponent* Comp)
    {
        if (!Comp)
        {
            return FQuat::Identity;
        }

        const USceneComponent* Parent = Comp->GetParent();
        const FName AttachSocketName = Comp->GetAttachSocketName();
        if (Parent &&
            IsSceneSocketAttachmentNameSet_GameThread(AttachSocketName) &&
            Parent->HasSocket(AttachSocketName))
        {
            return Comp->GetWorldRotation().ToQuaternion().GetNormalized();
        }

        const FQuat ParentRotation = GetSceneComponentWorldRotationNoScale_GameThread(Parent);
        return (ParentRotation * Comp->GetRelativeQuat()).GetNormalized();
    }

    FQuat GetComponentWorldRotationNoScale_GameThread(const UPrimitiveComponent* Comp)
    {
        return GetSceneComponentWorldRotationNoScale_GameThread(Comp);
    }

    FTransform GetComponentWorldTransform_GameThread(UPrimitiveComponent* Comp)
    {
        FTransform Result;
        if (!Comp)
        {
            return Result;
        }

        Result.Location = Comp->GetWorldLocation();
        Result.Rotation = GetComponentWorldRotationNoScale_GameThread(Comp);
        Result.Scale    = FVector::OneVector;
        return Result;
    }

    FTransform MakeRelativeTransformFromWorld_GameThread(
        const FVector&       ChildWorldLocation,
        const FQuat&         ChildWorldRotation,
        UPrimitiveComponent* Root
    )
    {
        FTransform Result;

        if (!Root)
        {
            Result.Location = ChildWorldLocation;
            Result.Rotation = ChildWorldRotation;
            Result.Scale    = FVector::OneVector;
            return Result;
        }

        const FVector RootPos    = Root->GetWorldLocation();
        const FQuat   RootRot    = GetComponentWorldRotationNoScale_GameThread(Root);
        const FQuat   InvRootRot = RootRot.Inverse();

        Result.Location = InvRootRot.RotateVector(ChildWorldLocation - RootPos);
        Result.Rotation = (InvRootRot * ChildWorldRotation).GetNormalized();
        Result.Scale    = FVector::OneVector;
        return Result;
    }

    FTransform MakeShapeLocalTransform_GameThread(UPrimitiveComponent* Comp, UPrimitiveComponent* Root)
    {
        if (!Comp || !Root || Comp == Root)
        {
            return FTransform();
        }

        return MakeRelativeTransformFromWorld_GameThread(
            Comp->GetWorldLocation(),
            GetComponentWorldRotationNoScale_GameThread(Comp),
            Root
        );
    }

    bool HasAnyBlockResponse_GameThread(UPrimitiveComponent* Comp)
    {
        if (!Comp)
        {
            return false;
        }

        for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
        {
            if (Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Ch)) == ECollisionResponse::Block)
            {
                return true;
            }
        }
        return false;
    }

    bool HasAnyOverlapResponse_GameThread(UPrimitiveComponent* Comp)
    {
        if (!Comp)
        {
            return false;
        }

        for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
        {
            if (Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Ch)) == ECollisionResponse::Overlap)
            {
                return true;
            }
        }
        return false;
    }

    bool ShouldBeTriggerShape_GameThread(UPrimitiveComponent* Comp, bool bOwnerBodyIsDynamic)
    {
        if (!Comp || Comp->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
        {
            return false;
        }

        const bool bHasAnyBlock     = HasAnyBlockResponse_GameThread(Comp);
        bool       bShouldBeTrigger =
                (Comp->GetCollisionObjectType() == ECollisionChannel::Trigger && !bHasAnyBlock) ||
                (Comp->GetCollisionEnabled() == ECollisionEnabled::QueryOnly &&
                    !bHasAnyBlock &&
                    HasAnyOverlapResponse_GameThread(Comp));

        if (bShouldBeTrigger && bOwnerBodyIsDynamic)
        {
            bShouldBeTrigger = false;
        }

        return bShouldBeTrigger;
    }

    EPhysicsCollisionRole ResolveComponentCollisionRole_GameThread(
        UPrimitiveComponent* Comp,
        bool bIsTriggerShape)
    {
        if (!Comp)
        {
            return EPhysicsCollisionRole::None;
        }

        if (bIsTriggerShape)
        {
            return EPhysicsCollisionRole::TriggerVolume;
        }

        if (Cast<UCapsuleComponent>(Comp))
        {
            return Comp->GetCollisionEnabled() == ECollisionEnabled::QueryOnly
                ? EPhysicsCollisionRole::CharacterQueryProxy
                : EPhysicsCollisionRole::CharacterLocomotionProxy;
        }

        if (Cast<USkeletalMeshComponent>(Comp))
        {
            return EPhysicsCollisionRole::CharacterMeshPrimitive;
        }

        switch (Comp->GetCollisionObjectType())
        {
        case ECollisionChannel::WorldStatic:
            return EPhysicsCollisionRole::WorldStatic;
        case ECollisionChannel::WorldDynamic:
            return EPhysicsCollisionRole::WorldDynamic;
        default:
            return EPhysicsCollisionRole::CharacterMeshPrimitive;
        }
    }

    void FillFilterDataFromComponent_GameThread(
        FPhysicsFilterData&  Out,
        UPrimitiveComponent* Comp,
        bool                 bIsTriggerShape,
        bool                 bEnableCCD
    )
    {
        if (!Comp)
        {
            return;
        }

        Out.ObjectType             = static_cast<uint32>(Comp->GetCollisionObjectType());
        Out.BlockMask              = 0;
        Out.OverlapMask            = 0;
        Out.IgnoreGroup            = Comp->GetOwner() ? Comp->GetOwner()->GetUUID() : 0;
        Out.CollisionEnabled       = Comp->GetCollisionEnabled();
        Out.bIsTrigger             = bIsTriggerShape;
        Out.bGenerateHitEvents     = true;
        Out.bGenerateOverlapEvents = Comp->GetGenerateOverlapEvents();
        Out.bEnableCCD             = bEnableCCD;
        Out.bIsComponentPrimitive  = true;
        Out.bIgnoreSameActor       = true;
        Out.CollisionRole          = ResolveComponentCollisionRole_GameThread(Comp, bIsTriggerShape);
        Out.GameplayOverlapOwnership = GetDefaultGameplayOverlapOwnershipForRole(Out.CollisionRole);

        for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
        {
            const ECollisionResponse R =
                    Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Ch));

            if (R == ECollisionResponse::Block)
            {
                Out.BlockMask |= (1u << Ch);
            }
            else if (R == ECollisionResponse::Overlap)
            {
                Out.OverlapMask |= (1u << Ch);
            }
        }
    }

    bool BuildPxSweepGeometry(const FCollisionShape& Shape, PxGeometryHolder& OutGeometry, PxQuat& OutShapeAxisRotation)
    {
        OutShapeAxisRotation = PxQuat(PxIdentity);

        if (Shape.ShapeType == ECollisionShape::Sphere)
        {
            const float Radius = Shape.GetSphereRadius();
            if (Radius <= 0.0f)
            {
                return false;
            }

            OutGeometry = PxSphereGeometry(Radius);
            return true;
        }

        if (Shape.ShapeType == ECollisionShape::Capsule)
        {
            const float Radius     = Shape.GetCapsuleRadius();
            const float HalfHeight = Shape.GetCapsuleHalfHeight();
            if (Radius <= 0.0f || HalfHeight <= 0.0f)
            {
                return false;
            }

            const float PhysXHalfHeight = (std::max)(0.0f, HalfHeight - Radius);
            OutGeometry = PxCapsuleGeometry(Radius, PhysXHalfHeight);
            // 엔진 capsule은 Z축 기준, PhysX capsule은 X축 기준이라 body 생성과 동일한 보정 회전을 쓴다.
            OutShapeAxisRotation = PxQuat(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f));
            return true;
        }

        if (Shape.ShapeType == ECollisionShape::Box)
        {
            const FVector Extent = Shape.GetExtent();
            if (Extent.X <= 0.0f || Extent.Y <= 0.0f || Extent.Z <= 0.0f)
            {
                return false;
            }

            OutGeometry = PxBoxGeometry(Extent.X, Extent.Y, Extent.Z);
            return true;
        }

        return false;
    }

    bool IsContinuousForceCommand(EPhysicsCommandType Type)
    {
        return Type == EPhysicsCommandType::AddForce ||
               Type == EPhysicsCommandType::AddForceAtLocation ||
               Type == EPhysicsCommandType::AddTorque;
    }

    void StampContinuousForceCommandDurations(TArray<FPhysicsCommand>& Commands, float DeltaTime)
    {
        const float SafeDeltaTime = (std::max)(0.0f, DeltaTime);
        if (SafeDeltaTime <= 0.0f)
        {
            return;
        }

        for (FPhysicsCommand& Command : Commands)
        {
            if (IsContinuousForceCommand(Command.Type) && Command.DurationSeconds <= 0.0f)
            {
                Command.DurationSeconds = SafeDeltaTime;
            }
        }
    }
}

// ============================================================
// PhysX Simulation Event Callback
// ============================================================
class FPhysXSimulationCallback : public PxSimulationEventCallback
{
public:
    struct FShapeEventInfo
    {
        uint32 ComponentId   = 0;
        uint32 ActorId       = 0;
        uint32 Generation    = 0;
        bool   bWantsOverlap = false;
        EPhysicsCollisionRole CollisionRole = EPhysicsCollisionRole::None;
        EPhysicsGameplayOverlapOwnership GameplayOverlapOwnership = EPhysicsGameplayOverlapOwnership::None;
        bool   bValid        = false;
    };

    struct FQueuedHit
    {
        uint32  SelfComponentId  = 0;
        uint32  SelfActorId      = 0;
        uint32  SelfGeneration   = 0;
        uint32  OtherComponentId = 0;
        uint32  OtherActorId     = 0;
        uint32  OtherGeneration  = 0;
        FVector NormalImpulse { 0, 0, 0 };
        FVector WorldLocation { 0, 0, 0 };
        FVector WorldNormal { 0, 0, 1 };
        float   PenetrationDepth = 0.0f;
        bool    bBegin           = true;
    };

    struct FQueuedTrigger
    {
        uint32 SelfComponentId  = 0;
        uint32 SelfActorId      = 0;
        uint32 SelfGeneration   = 0;
        uint32 OtherComponentId = 0;
        uint32 OtherActorId     = 0;
        uint32 OtherGeneration  = 0;
        bool   bBegin           = true;
    };

    struct FPairKey
    {
        uint32 A = 0;
        uint32 B = 0;

        FPairKey() = default;

        FPairKey(uint32 InA, uint32 InB)
        {
            A = InA;
            B = InB;
            if (A > B)
            {
                std::swap(A, B);
            }
        }

        bool IsValid() const
        {
            return A != 0 && B != 0 && A != B;
        }

        bool operator==(const FPairKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    struct FPairRecord
    {
        FShapeEventInfo A;
        FShapeEventInfo B;
    };

    struct FPairKeyHash
    {
        size_t operator()(const FPairKey& Key) const
        {
            size_t H = std::hash<uint32>()(Key.A);
            H        ^= std::hash<uint32>()(Key.B) + 0x9e3779b9 + (H << 6) + (H >> 2);
            return H;
        }
    };

    static FShapeEventInfo GetShapeEventInfo(const PxShape* Shape)
    {
        FShapeEventInfo Info;
        FShapeInstance* ShapeInstance = GetShapeInstance(Shape);
        if (!ShapeInstance || ShapeInstance->State != EPhysicsRuntimeObjectState::Alive)
        {
            return Info;
        }
        Info.ComponentId = ShapeInstance->SourceComponentId;
        Info.ActorId     = ShapeInstance->SourceActorId;
        Info.Generation  = ShapeInstance->SourceComponentGeneration;
        Info.bValid      = (Info.ComponentId != 0);
        if (Shape)
        {
            const PxFilterData FilterData = Shape->getSimulationFilterData();
            Info.bWantsOverlap            = HasPhysicsFilterFlag(FilterData.word0, PhysicsFilter_GenerateOverlapEvents);
            Info.CollisionRole            = GetPackedCollisionRole(FilterData);
            Info.GameplayOverlapOwnership = GetPackedGameplayOverlapOwnership(FilterData);
        }
        return Info;
    }

    void onContact(const PxContactPairHeader& PairHeader,
        const PxContactPair* Pairs, PxU32 Count) override
    {
        if (PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_0 ||
            PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_1)
        {
            return;
        }

        for (PxU32 i = 0; i < Count; ++i)
        {
            const PxContactPair& CP = Pairs[i];
            const bool bBegin = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
            const bool bEnd = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST);
            if (!bBegin && !bEnd)
            {
                continue;
            }

            const FShapeEventInfo InfoA = GetShapeEventInfo(CP.shapes[0]);
            const FShapeEventInfo InfoB = GetShapeEventInfo(CP.shapes[1]);
            if (!InfoA.bValid || !InfoB.bValid)
            {
                continue;
            }

            const ECollisionResponse MinResponse = GetPackedMinResponse(
                CP.shapes[0]->getSimulationFilterData(),
                CP.shapes[1]->getSimulationFilterData()
            );
            if (MinResponse == ECollisionResponse::Ignore)
            {
                continue;
            }

            if (MinResponse == ECollisionResponse::Overlap)
            {
                if (bBegin)
                {
                    QueueOverlapPair(InfoA, InfoB, true);
                }
                if (bEnd)
                {
                    QueueOverlapPair(InfoA, InfoB, false);
                }
                continue;
            }

            PxContactPairPoint ContactPoints[1];
            PxU32 NumPoints = CP.extractContacts(ContactPoints, 1);

            FVector ContactPos(0, 0, 0);
            FVector ContactNormal(0, 0, 1);
            float Penetration = 0.0f;

            if (NumPoints > 0)
            {
                ContactPos = ToFVector(ContactPoints[0].position);
                ContactNormal = ToFVector(ContactPoints[0].normal);
                Penetration = ContactPoints[0].separation;
            }

            // PhysX contact separation is not an impulse. Until contact impulse extraction is
            // wired, expose zero impulse rather than a misleading penetration-derived value.
            const FVector NormalImpulse = FVector::ZeroVector;

            if (bBegin)
            {
                QueueHitPair(InfoA, InfoB, true, ContactPos, ContactNormal, NormalImpulse, -Penetration);
            }
            if (bEnd)
            {
                QueueHitPair(InfoA, InfoB, false, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, 0.0f);
            }
        }
    }

    void onTrigger(PxTriggerPair* Pairs, PxU32 Count) override
    {
        for (PxU32 i = 0; i < Count; ++i)
        {
            const PxTriggerPair& TP = Pairs[i];

            if (TP.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
            {
                // The component unregister path emits synthetic end events from ActiveOverlapPairs.
                continue;
            }

            const FShapeEventInfo TriggerInfo = GetShapeEventInfo(TP.triggerShape);
            const FShapeEventInfo OtherInfo   = GetShapeEventInfo(TP.otherShape);
            if (!TriggerInfo.bValid || !OtherInfo.bValid)
            {
                continue;
            }

            const ECollisionResponse MinResponse = GetPackedMinResponse(
                TP.triggerShape->getSimulationFilterData(),
                TP.otherShape->getSimulationFilterData()
            );
            if (MinResponse == ECollisionResponse::Ignore)
            {
                continue;
            }

            const bool bBegin = TP.status == PxPairFlag::eNOTIFY_TOUCH_FOUND;
            const bool bEnd = TP.status == PxPairFlag::eNOTIFY_TOUCH_LOST;
            if (bBegin)
            {
                QueueOverlapPair(TriggerInfo, OtherInfo, true);
            }
            if (bEnd)
            {
                QueueOverlapPair(TriggerInfo, OtherInfo, false);
            }
        }
    }

    void NotifyComponentUnregistered(UPrimitiveComponent* Component)
    {
        if (!Component)
        {
            return;
        }

        const uint32                ComponentId = Component->GetUUID();
        std::lock_guard<std::mutex> Lock(EventMutex);
        QueueSyntheticEndEventsForComponent_NoLock(ComponentId, ActiveOverlapPairs, PendingTriggers, true);
        QueueSyntheticEndEventsForComponent_NoLock(ComponentId, ActiveHitPairs, PendingHits, false);
    }

    void DispatchPendingEvents(
        bool                                       bDispatchHitEvents,
        bool                                       bDispatchTriggerEvents,
        const std::function<bool(uint32, uint32)>& IsCurrentGeneration
    )
    {
        std::vector<FQueuedHit>     HitsToDispatch;
        std::vector<FQueuedTrigger> TriggersToDispatch;
        {
            std::lock_guard<std::mutex> Lock(EventMutex);
            HitsToDispatch.swap(PendingHits);
            TriggersToDispatch.swap(PendingTriggers);
        }

        if (bDispatchHitEvents)
        {
            for (FQueuedHit& E : HitsToDispatch)
            {
                if (E.bBegin && IsCurrentGeneration &&
                    (!IsCurrentGeneration(E.SelfComponentId, E.SelfGeneration) ||
                        !IsCurrentGeneration(E.OtherComponentId, E.OtherGeneration)))
                {
                    continue;
                }

                UPrimitiveComponent* Self  = Cast<UPrimitiveComponent>(UObjectManager::Get().FindByUUID(E.SelfComponentId));
                UPrimitiveComponent* Other = Cast<UPrimitiveComponent>(UObjectManager::Get().FindByUUID(E.OtherComponentId));
                if (!IsValid(Self) || !IsValid(Other))
                {
                    continue;
                }

                AActor* OtherActor = Other->GetOwner();
                if (E.bBegin)
                {
                    FHitResult Hit;
                    Hit.bHit             = true;
                    Hit.HitComponent     = Other;
                    Hit.HitActor         = OtherActor;
                    Hit.WorldHitLocation = E.WorldLocation;
                    Hit.ImpactNormal     = E.WorldNormal;
                    Hit.WorldNormal      = E.WorldNormal;
                    Hit.PenetrationDepth = E.PenetrationDepth;
                    Self->NotifyComponentHit(Self, OtherActor, Other, E.NormalImpulse, Hit);
                }
                else
                {
                    Self->NotifyComponentEndHit(Self, OtherActor, Other);
                }
            }
        }

        if (bDispatchTriggerEvents)
        {
            for (FQueuedTrigger& E : TriggersToDispatch)
            {
                if (E.bBegin && IsCurrentGeneration &&
                    (!IsCurrentGeneration(E.SelfComponentId, E.SelfGeneration) ||
                        !IsCurrentGeneration(E.OtherComponentId, E.OtherGeneration)))
                {
                    continue;
                }

                UPrimitiveComponent* Self  = Cast<UPrimitiveComponent>(UObjectManager::Get().FindByUUID(E.SelfComponentId));
                UPrimitiveComponent* Other = Cast<UPrimitiveComponent>(UObjectManager::Get().FindByUUID(E.OtherComponentId));
                if (!IsValid(Self) || !IsValid(Other))
                {
                    continue;
                }

                AActor* OtherActor = Other->GetOwner();
                if (E.bBegin)
                {
                    FHitResult DummyHit;
                    DummyHit.HitComponent = Other;
                    DummyHit.HitActor     = OtherActor;
                    Self->NotifyComponentBeginOverlap(Self, OtherActor, Other, 0, false, DummyHit);
                }
                else
                {
                    Self->NotifyComponentEndOverlap(Self, OtherActor, Other, 0);
                }
            }
        }
    }

    void GetActivePairCounts(int32& OutContactPairs, int32& OutTriggerPairs)
    {
        std::lock_guard<std::mutex> Lock(EventMutex);
        OutContactPairs = static_cast<int32>(ActiveHitPairs.size());
        OutTriggerPairs = static_cast<int32>(ActiveOverlapPairs.size());
    }

    void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
    void onWake(PxActor**, PxU32) override {}
    void onSleep(PxActor**, PxU32) override {}
    void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

private:
    void QueueOverlapPair(const FShapeEventInfo& A, const FShapeEventInfo& B, bool bBegin)
    {
        if (!A.bValid || !B.bValid)
        {
            return;
        }

        // Overlap-owner semantics are intentionally interpreted above the physics event queue.
        // The queue preserves the raw component-level event, while gameplay systems such as
        // trigger occupancy can now prefer components marked as overlap owners.

        std::lock_guard<std::mutex> Lock(EventMutex);
        FPairKey                    Key(A.ComponentId, B.ComponentId);
        if (!Key.IsValid())
        {
            return;
        }

        if (bBegin)
        {
            if (ActiveOverlapPairs.find(Key) != ActiveOverlapPairs.end())
            {
                return;
            }
            ActiveOverlapPairs[Key] = FPairRecord { A, B };
        }
        else
        {
            auto It = ActiveOverlapPairs.find(Key);
            if (It == ActiveOverlapPairs.end())
            {
                return;
            }
            ActiveOverlapPairs.erase(It);
        }

        if (A.bWantsOverlap)
        {
            PendingTriggers.push_back({ A.ComponentId, A.ActorId, A.Generation, B.ComponentId, B.ActorId, B.Generation, bBegin });
        }
        if (B.bWantsOverlap)
        {
            PendingTriggers.push_back({ B.ComponentId, B.ActorId, B.Generation, A.ComponentId, A.ActorId, A.Generation, bBegin });
        }
    }

    void QueueHitPair(
        const FShapeEventInfo& A,
        const FShapeEventInfo& B,
        bool                   bBegin,
        const FVector&         WorldLocation,
        const FVector&         WorldNormal,
        const FVector&         NormalImpulse,
        float                  PenetrationDepth
    )
    {
        if (!A.bValid || !B.bValid)
        {
            return;
        }

        std::lock_guard<std::mutex> Lock(EventMutex);
        FPairKey                    Key(A.ComponentId, B.ComponentId);
        if (!Key.IsValid())
        {
            return;
        }

        if (bBegin)
        {
            if (ActiveHitPairs.find(Key) != ActiveHitPairs.end())
            {
                return;
            }
            ActiveHitPairs[Key] = FPairRecord { A, B };
        }
        else
        {
            auto It = ActiveHitPairs.find(Key);
            if (It == ActiveHitPairs.end())
            {
                return;
            }
            ActiveHitPairs.erase(It);
        }

        PendingHits.push_back(
            { A.ComponentId, A.ActorId, A.Generation, B.ComponentId, B.ActorId, B.Generation,
              NormalImpulse, WorldLocation, WorldNormal, PenetrationDepth, bBegin
            }
        );
        PendingHits.push_back(
            { B.ComponentId, B.ActorId, B.Generation, A.ComponentId, A.ActorId, A.Generation,
              NormalImpulse * -1.0f, WorldLocation, WorldNormal * -1.0f, PenetrationDepth, bBegin
            }
        );
    }

    void QueueSyntheticEndEventsForComponent_NoLock(
        uint32                                                   ComponentId,
        std::unordered_map<FPairKey, FPairRecord, FPairKeyHash>& ActivePairs,
        std::vector<FQueuedTrigger>&                             OutTriggers,
        bool
    )
    {
        for (auto It = ActivePairs.begin(); It != ActivePairs.end();)
        {
            const FShapeEventInfo& A = It->second.A;
            const FShapeEventInfo& B = It->second.B;
            if (A.ComponentId == ComponentId || B.ComponentId == ComponentId)
            {
                const FShapeEventInfo& Self  = (A.ComponentId == ComponentId) ? A : B;
                const FShapeEventInfo& Other = (A.ComponentId == ComponentId) ? B : A;
                if (Self.bWantsOverlap)
                {
                    OutTriggers.push_back({ Self.ComponentId, Self.ActorId, Self.Generation, Other.ComponentId, Other.ActorId, Other.Generation, false });
                }
                if (Other.bWantsOverlap)
                {
                    OutTriggers.push_back({ Other.ComponentId, Other.ActorId, Other.Generation, Self.ComponentId, Self.ActorId, Self.Generation, false });
                }
                It = ActivePairs.erase(It);
            }
            else
            {
                ++It;
            }
        }
    }

    void QueueSyntheticEndEventsForComponent_NoLock(
        uint32                                                   ComponentId,
        std::unordered_map<FPairKey, FPairRecord, FPairKeyHash>& ActivePairs,
        std::vector<FQueuedHit>&                                 OutHits,
        bool
    )
    {
        for (auto It = ActivePairs.begin(); It != ActivePairs.end();)
        {
            const FShapeEventInfo& A = It->second.A;
            const FShapeEventInfo& B = It->second.B;
            if (A.ComponentId == ComponentId || B.ComponentId == ComponentId)
            {
                const FShapeEventInfo& Self  = (A.ComponentId == ComponentId) ? A : B;
                const FShapeEventInfo& Other = (A.ComponentId == ComponentId) ? B : A;
                OutHits.push_back(
                    { Self.ComponentId, Self.ActorId, Self.Generation, Other.ComponentId, Other.ActorId, Other.Generation,
                      FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, 0.0f, false
                    }
                );
                OutHits.push_back(
                    { Other.ComponentId, Other.ActorId, Other.Generation, Self.ComponentId, Self.ActorId, Self.Generation,
                      FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, 0.0f, false
                    }
                );
                It = ActivePairs.erase(It);
            }
            else
            {
                ++It;
            }
        }
    }

    std::mutex                                              EventMutex;
    std::vector<FQueuedHit>                                 PendingHits;
    std::vector<FQueuedTrigger>                             PendingTriggers;
    std::unordered_map<FPairKey, FPairRecord, FPairKeyHash> ActiveOverlapPairs;
    std::unordered_map<FPairKey, FPairRecord, FPairKeyHash> ActiveHitPairs;
};

// ============================================================
// Collision Filtering
// ============================================================
namespace
{
    ECollisionResponse GetPackedResponseTo(const PxFilterData& From, PxU32 ToChannel)
    {
        const PxU32 ChannelBit = 1u << ToChannel;
        if ((From.word1 & ChannelBit) != 0)
        {
            return ECollisionResponse::Block;
        }
        if ((From.word2 & ChannelBit) != 0)
        {
            return ECollisionResponse::Overlap;
        }
        return ECollisionResponse::Ignore;
    }

    ECollisionResponse GetPackedMinResponse(const PxFilterData& A, const PxFilterData& B)
    {
        const PxU32              ChannelA = GetPhysicsFilterObjectType(A.word0);
        const PxU32              ChannelB = GetPhysicsFilterObjectType(B.word0);
        const ECollisionResponse AToB     = GetPackedResponseTo(A, ChannelB);
        const ECollisionResponse BToA     = GetPackedResponseTo(B, ChannelA);
        return (AToB < BToA) ? AToB : BToA;
    }

    bool IsPackedQueryOnly(const PxFilterData& Data)
    {
        return HasPhysicsFilterFlag(Data.word0, PhysicsFilter_QueryOnly);
    }

    bool IsPackedPhysicsEnabled(const PxFilterData& Data)
    {
        return HasPhysicsFilterFlag(Data.word0, PhysicsFilter_PhysicsOnly) ||
                HasPhysicsFilterFlag(Data.word0, PhysicsFilter_QueryAndPhysics);
    }

    bool IsPackedQueryEnabled(const PxFilterData& Data)
    {
        return HasPhysicsFilterFlag(Data.word0, PhysicsFilter_QueryOnly) ||
                HasPhysicsFilterFlag(Data.word0, PhysicsFilter_QueryAndPhysics);
    }

    bool WantsHitNotify(const PxFilterData& A, const PxFilterData& B)
    {
        return HasPhysicsFilterFlag(A.word0, PhysicsFilter_GenerateHitEvents) ||
                HasPhysicsFilterFlag(B.word0, PhysicsFilter_GenerateHitEvents);
    }

    bool WantsOverlapNotify(const PxFilterData& A, const PxFilterData& B)
    {
        return (IsPackedQueryEnabled(A) || IsPackedQueryEnabled(B)) &&
        (HasPhysicsFilterFlag(A.word0, PhysicsFilter_GenerateOverlapEvents) ||
            HasPhysicsFilterFlag(B.word0, PhysicsFilter_GenerateOverlapEvents));
    }

    bool HasPackedSameActorSelfIgnore(const PxFilterData& Data)
    {
        return HasPhysicsFilterFlag(Data.word0, PhysicsFilter_SameActorSelfIgnore);
    }

    EPhysicsCollisionRole GetPackedCollisionRole(const PxFilterData& Data)
    {
        EPhysicsCollisionRole Role = GetPackedPhysicsCollisionRole(Data.word0);
        if (Role != EPhysicsCollisionRole::None)
        {
            return Role;
        }

        if (HasPhysicsFilterFlag(Data.word0, PhysicsFilter_PartialRagdoll))
        {
            return EPhysicsCollisionRole::PartialReactionBody;
        }

        if (HasPhysicsFilterFlag(Data.word0, PhysicsFilter_IndependentRagdoll))
        {
            return EPhysicsCollisionRole::FullRagdollBody;
        }

        if (HasPhysicsFilterFlag(Data.word0, PhysicsFilter_IsTrigger))
        {
            return EPhysicsCollisionRole::TriggerVolume;
        }

        if (HasPhysicsFilterFlag(Data.word0, PhysicsFilter_ComponentPrimitive))
        {
            return EPhysicsCollisionRole::CharacterMeshPrimitive;
        }

        switch (static_cast<ECollisionChannel>(GetPhysicsFilterObjectType(Data.word0)))
        {
        case ECollisionChannel::WorldStatic:
            return EPhysicsCollisionRole::WorldStatic;
        case ECollisionChannel::WorldDynamic:
            return EPhysicsCollisionRole::WorldDynamic;
        default:
            return EPhysicsCollisionRole::None;
        }
    }

    EPhysicsGameplayOverlapOwnership GetPackedGameplayOverlapOwnership(const PxFilterData& Data)
    {
        if (HasPhysicsFilterFlag(Data.word0, PhysicsFilter_OverlapOwnerPrimary))
        {
            return EPhysicsGameplayOverlapOwnership::PrimaryOwner;
        }
        if (HasPhysicsFilterFlag(Data.word0, PhysicsFilter_OverlapOwnerQueryProxy))
        {
            return EPhysicsGameplayOverlapOwnership::QueryProxyOwner;
        }
        if (HasPhysicsFilterFlag(Data.word0, PhysicsFilter_OverlapNonOwningReaction))
        {
            return EPhysicsGameplayOverlapOwnership::NonOwningReactionBody;
        }

        return GetDefaultGameplayOverlapOwnershipForRole(GetPackedCollisionRole(Data));
    }

    bool SuppressesPackedSameActorPrimitivePairs(const PxFilterData& Data)
    {
        return HasPhysicsFilterFlag(Data.word0, PhysicsFilter_SuppressSameActorPrimitivePairs);
    }

    bool IsCharacterPrimitiveRole(EPhysicsCollisionRole Role)
    {
        return
            Role == EPhysicsCollisionRole::CharacterLocomotionProxy ||
            Role == EPhysicsCollisionRole::CharacterMeshPrimitive ||
            Role == EPhysicsCollisionRole::CharacterQueryProxy;
    }

    bool IsCharacterRagdollBodyRole(EPhysicsCollisionRole Role)
    {
        return
            Role == EPhysicsCollisionRole::PartialReactionBody ||
            Role == EPhysicsCollisionRole::FullRagdollBody;
    }

    bool ShouldSuppressSameActorRolePair(EPhysicsCollisionRole RoleA, EPhysicsCollisionRole RoleB)
    {
        return
            (IsCharacterPrimitiveRole(RoleA) && IsCharacterRagdollBodyRole(RoleB)) ||
            (IsCharacterPrimitiveRole(RoleB) && IsCharacterRagdollBodyRole(RoleA));
    }

    bool IsSameActorSuppressedRolePair(const PxFilterData& A, const PxFilterData& B)
    {
        if (A.word3 == 0 || A.word3 != B.word3)
        {
            return false;
        }

        if (!SuppressesPackedSameActorPrimitivePairs(A) && !SuppressesPackedSameActorPrimitivePairs(B))
        {
            return false;
        }

        return ShouldSuppressSameActorRolePair(GetPackedCollisionRole(A), GetPackedCollisionRole(B));
    }

    bool WantsCCDContact(const PxFilterData& A, const PxFilterData& B)
    {
        return HasPhysicsFilterFlag(A.word0, PhysicsFilter_EnableCCD) ||
                HasPhysicsFilterFlag(B.word0, PhysicsFilter_EnableCCD);
    }
}

static PxFilterFlags KraftonFilterShader(
    PxFilterObjectAttributes attributes0, PxFilterData filterData0,
    PxFilterObjectAttributes attributes1, PxFilterData filterData1,
    PxPairFlags& pairFlags, const void*, PxU32)
{
    const bool bSameActor = filterData0.word3 != 0 && filterData0.word3 == filterData1.word3;
    if (bSameActor &&
        HasPackedSameActorSelfIgnore(filterData0) &&
        HasPackedSameActorSelfIgnore(filterData1))
    {
        return PxFilterFlag::eKILL;
    }

    if (IsSameActorSuppressedRolePair(filterData0, filterData1))
    {
        return PxFilterFlag::eKILL;
    }

    const ECollisionResponse MinResponse = GetPackedMinResponse(filterData0, filterData1);
    if (MinResponse == ECollisionResponse::Ignore)
    {
        return PxFilterFlag::eKILL;
    }

    if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
    {
        if (!WantsOverlapNotify(filterData0, filterData1))
        {
            return PxFilterFlag::eKILL;
        }

        pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
        return PxFilterFlag::eDEFAULT;
    }

    if (MinResponse == ECollisionResponse::Block)
    {
        const bool bBothCanPhysicallySolve =
                IsPackedPhysicsEnabled(filterData0) && IsPackedPhysicsEnabled(filterData1) &&
                !IsPackedQueryOnly(filterData0) && !IsPackedQueryOnly(filterData1);

        pairFlags = bBothCanPhysicallySolve
                ? PxPairFlag::eCONTACT_DEFAULT
                : PxPairFlag::eDETECT_DISCRETE_CONTACT;

        if (bBothCanPhysicallySolve && WantsCCDContact(filterData0, filterData1))
        {
            pairFlags |= PxPairFlag::eDETECT_CCD_CONTACT;
        }

        if (WantsHitNotify(filterData0, filterData1))
        {
            pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND
                    | PxPairFlag::eNOTIFY_TOUCH_LOST
                    | PxPairFlag::eNOTIFY_CONTACT_POINTS;
        }

        return PxFilterFlag::eDEFAULT;
    }

    if (MinResponse == ECollisionResponse::Overlap)
    {
        if (!WantsOverlapNotify(filterData0, filterData1))
        {
            return PxFilterFlag::eKILL;
        }

        pairFlags = PxPairFlag::eDETECT_DISCRETE_CONTACT
            | PxPairFlag::eNOTIFY_TOUCH_FOUND
            | PxPairFlag::eNOTIFY_TOUCH_LOST;
        return PxFilterFlag::eDEFAULT;
    }

    return PxFilterFlag::eKILL;
}

void FPhysXPhysicsScene::Initialize(UWorld* InWorld)
{
    World = InWorld;

    AcquireSharedPhysX(Foundation, Physics);
    if (!Foundation || !Physics)
    {
        UE_LOG("[PhysX] Failed to create Foundation or Physics");
        Shutdown();
        return;
    }

    const auto& PhysicsSettings   = FProjectSettings::Get().Physics;
    int32       WorkerThreadCount = PhysicsSettings.WorkerThreadCount;
    if (WorkerThreadCount <= 0)
    {
        const unsigned int HardwareThreads = std::thread::hardware_concurrency();
        WorkerThreadCount                  = static_cast<int32>((std::max)(1u, HardwareThreads > 1 ? HardwareThreads - 1 : 1u));
    }

    Dispatcher = PxDefaultCpuDispatcherCreate(WorkerThreadCount);
    if (!Dispatcher)
    {
        UE_LOG("[PhysX] Failed to create CPU dispatcher");
        Shutdown();
        return;
    }

    EventCallback = new FPhysXSimulationCallback();
    if (!EventCallback)
    {
        UE_LOG("[PhysX] Failed to create simulation callback");
        Shutdown();
        return;
    }

    PxSceneDesc SceneDesc(Physics->getTolerancesScale());
    SceneDesc.gravity = PxVec3(0.0f, 0.0f, -9.81f);
    SceneDesc.cpuDispatcher = Dispatcher;
    SceneDesc.filterShader = KraftonFilterShader;
    SceneDesc.simulationEventCallback = EventCallback;
    SceneDesc.staticKineFilteringMode = PxPairFilteringMode::eKEEP;
    SceneDesc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;

    if (PhysicsSettings.bEnableCCD)
    {
        SceneDesc.flags |= PxSceneFlag::eENABLE_CCD;
    }
    if (PhysicsSettings.bEnablePCM)
    {
        SceneDesc.flags |= PxSceneFlag::eENABLE_PCM;
    }
    if (PhysicsSettings.bEnableActiveActors)
    {
        SceneDesc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;
    }
    if (PhysicsSettings.bRequireSceneReadWriteLock)
    {
        SceneDesc.flags |= PxSceneFlag::eREQUIRE_RW_LOCK;
    }

    Scene = Physics->createScene(SceneDesc);
    if (!Scene)
    {
        UE_LOG("[PhysX] Failed to create Scene");
        Shutdown();
        return;
    }

    DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.3f);
    if (!DefaultMaterial)
    {
        UE_LOG("[PhysX] Failed to create DefaultMaterial");
        Shutdown();
        return;
    }

    Runtime.Initialize(World, Physics, Scene, DefaultMaterial);
    StartPhysicsThread();

    UE_LOG("[PhysX] Initialized successfully (Scene=%p)", Scene);
}

void FPhysXPhysicsScene::Shutdown()
{
    StopPhysicsThreadAndJoin();
    Runtime.Shutdown();

    GameThreadBindings.clear();
    GameThreadActorBodies.clear();

    if (DefaultMaterial)
    {
        DefaultMaterial->release();
        DefaultMaterial = nullptr;
    }
    if (Scene)
    {
        Scene->release();
        Scene = nullptr;
    }
    if (EventCallback)
    {
        delete EventCallback;
        EventCallback = nullptr;
    }
    if (Dispatcher)
    {
        Dispatcher->release();
        Dispatcher = nullptr;
    }

    Foundation = nullptr;
    Physics = nullptr;
    ReleaseSharedPhysX();

    World = nullptr;
}

void FPhysXPhysicsScene::RegisterComponent(UPrimitiveComponent* Comp)
{
    if (!IsAliveObject(Comp))
    {
        return;
    }

    FPhysicsComponentBinding& Binding = TouchBinding_GameThread(Comp);
    Binding.bPendingDestroy           = false;
    Binding.bPendingCreate            = true;

    FPhysicsBodyCreatePayload Payload = BuildRegisterPayload_GameThread(Comp, Binding);
    if (!Payload.ShapeOwner.ComponentId || !Payload.ReservedBody.IsValid() || !Payload.ReservedShape.IsValid())
    {
        Binding.bPendingCreate  = false;
        Binding.bPendingDestroy = true;
        Binding.Body            = FPhysicsBodyHandle {};
        Binding.Shape           = FPhysicsShapeHandle {};
        return;
    }

    Binding.Body     = Payload.ReservedBody;
    Binding.Shape    = Payload.ReservedShape;
    Binding.SyncMode = Payload.SyncMode;

    Runtime.RegisterComponent(Payload);
}

void FPhysXPhysicsScene::UnregisterComponent(UPrimitiveComponent* Comp)
{
    if (!Comp)
    {
        return;
    }

    if (EventCallback)
    {
        EventCallback->NotifyComponentUnregistered(Comp);
    }

    const uint32 ComponentId = Comp->GetUUID();
    auto         It          = GameThreadBindings.find(ComponentId);
    if (It == GameThreadBindings.end())
    {
        return;
    }

    FPhysicsComponentBinding& Binding = It->second;

    FPhysicsObjectKey Object;
    Object.ActorId             = Binding.ActorId;
    Object.ComponentId         = Binding.ComponentId;
    Object.ComponentGeneration = Binding.Generation;
    Object.Domain              = EPhysicsBodyDomain::ActorComponent;

    Runtime.UnregisterComponent(Object);

    ++Binding.Generation;
    Binding.bPendingDestroy = true;
    Binding.bPendingCreate  = false;

    bool bActorStillHasLiveBinding = false;
    for (const auto& Pair : GameThreadBindings)
    {
        const FPhysicsComponentBinding& Other = Pair.second;
        if (Other.ActorId == Binding.ActorId && !Other.bPendingDestroy)
        {
            bActorStillHasLiveBinding = true;
            break;
        }
    }
    if (!bActorStillHasLiveBinding)
    {
        GameThreadActorBodies.erase(Binding.ActorId);
    }
}

void FPhysXPhysicsScene::SyncComponentTransform(UPrimitiveComponent* Comp)
{
	if (!IsAliveObject(Comp) || !Comp->IsCollisionEnabled())
	{
		return;
	}

	FPhysicsComponentBinding* Binding = FindBinding_GameThread(Comp->GetUUID());
	if (!Binding || Binding->bPendingDestroy || !Binding->Body.IsValid())
	{
		return;
	}

	if (Binding->SyncMode != EPhysicsSyncMode::EngineToPhysics &&
		Binding->SyncMode != EPhysicsSyncMode::KinematicTarget)
	{
		return;
	}

	Runtime.SetBodyTransform(
		Binding->Body,
		GetComponentWorldTransform_GameThread(Comp),
		EPhysicsTeleportMode::TeleportPhysics);
}

void FPhysXPhysicsScene::RebuildBody(UPrimitiveComponent* Comp)
{
	if (!IsAliveObject(Comp))
	{
		return;
	}

    AActor*      OwnerActor = Comp->GetOwner();
    const uint32 ActorId    = OwnerActor ? OwnerActor->GetUUID() : 0;

    TArray<UPrimitiveComponent*> ComponentsToRebuild;
    if (ActorId != 0)
    {
        for (const auto& Pair : GameThreadBindings)
        {
            const FPhysicsComponentBinding& Binding = Pair.second;
            if (Binding.ActorId != ActorId || Binding.bPendingDestroy)
            {
                continue;
            }

            UPrimitiveComponent* BoundComponent = Cast<UPrimitiveComponent>(UObjectManager::Get().FindByUUID(Binding.ComponentId));
            if (IsValid(BoundComponent) && BoundComponent->IsCollisionEnabled())
            {
                ComponentsToRebuild.push_back(BoundComponent);
            }
        }
    }

    if (ComponentsToRebuild.empty())
    {
        ComponentsToRebuild.push_back(Comp);
    }

    for (UPrimitiveComponent* RebuildComp : ComponentsToRebuild)
    {
        if (EventCallback)
        {
            EventCallback->NotifyComponentUnregistered(RebuildComp);
        }

        FPhysicsComponentBinding& Binding = TouchBinding_GameThread(RebuildComp);

        FPhysicsObjectKey Object;
        Object.ActorId             = Binding.ActorId;
        Object.ComponentId         = Binding.ComponentId;
        Object.ComponentGeneration = Binding.Generation;
        Object.Domain              = EPhysicsBodyDomain::ActorComponent;
        Runtime.UnregisterComponent(Object);

        ++Binding.Generation;
        Binding.bPendingDestroy = true;
        Binding.bPendingCreate  = false;
    }

    if (ActorId != 0)
    {
        GameThreadActorBodies.erase(ActorId);
    }

    for (UPrimitiveComponent* RebuildComp : ComponentsToRebuild)
    {
        if (IsValid(RebuildComp) && RebuildComp->IsCollisionEnabled())
        {
            RegisterComponent(RebuildComp);
        }
    }
}

FPhysicsComponentBinding& FPhysXPhysicsScene::TouchBinding_GameThread(UPrimitiveComponent* Comp)
{
    const uint32              ComponentId = Comp->GetUUID();
    FPhysicsComponentBinding& Binding     = GameThreadBindings[ComponentId];
    Binding.ComponentId                   = ComponentId;
    Binding.ActorId                       = Comp->GetOwner() ? Comp->GetOwner()->GetUUID() : 0;
    if (Binding.Generation == 0)
    {
        Binding.Generation = 1;
    }
    return Binding;
}

const FPhysicsComponentBinding* FPhysXPhysicsScene::FindBinding_GameThread(uint32 ComponentId) const
{
    const auto It = GameThreadBindings.find(ComponentId);
    return It != GameThreadBindings.end() ? &It->second : nullptr;
}

FPhysicsComponentBinding* FPhysXPhysicsScene::FindBinding_GameThread(uint32 ComponentId)
{
    auto It = GameThreadBindings.find(ComponentId);
    return It != GameThreadBindings.end() ? &It->second : nullptr;
}

uint32 FPhysXPhysicsScene::GetComponentGeneration_GameThread(uint32 ComponentId) const
{
    const FPhysicsComponentBinding* Binding = FindBinding_GameThread(ComponentId);
    return Binding ? Binding->Generation : 0;
}

FVehicleHandle FPhysXPhysicsScene::CreateVehicle(const FVehicleDesc& Desc)
{
    FVehicleDesc LocalDesc = Desc;
    LocalDesc.ReservedVehicle   = Runtime.ReserveVehicleHandle_GameThread();

    Runtime.CreateVehicle(LocalDesc);
    return LocalDesc.ReservedVehicle;
}

void FPhysXPhysicsScene::DestroyVehicle(FVehicleHandle Vehicle)
{
    if (Vehicle.IsValid())
    {
        Runtime.DestroyVehicle(Vehicle);
    }
}

void FPhysXPhysicsScene::SetVehicleInput(FVehicleHandle Vehicle, const FVehicleInputState& Input)
{
    if (Vehicle.IsValid())
    {
        Runtime.SetVehicleInput(Vehicle, Input);
    }
}

void FPhysXPhysicsScene::ResetVehicle(FVehicleHandle Vehicle, const FTransform& WorldTransform)
{
    if (Vehicle.IsValid())
    {
        Runtime.ResetVehicle(Vehicle, WorldTransform);
    }
}

FBodyCreationDesc FPhysXPhysicsScene::BuildBodyDescFromComponent_GameThread(UPrimitiveComponent* Comp, uint32 Generation) const
{
    FBodyCreationDesc Desc;
    if (!Comp)
    {
        return Desc;
    }

    Desc.OwnerActorId             = Comp->GetOwner() ? Comp->GetOwner()->GetUUID() : 0;
    Desc.OwnerComponentId         = Comp->GetUUID();
    Desc.OwnerComponentGeneration = Generation;
    Desc.WorldTransform           = GetComponentWorldTransform_GameThread(Comp);

    const bool bSimulate  = Comp->GetSimulatePhysics();
    const bool bKinematic = !bSimulate && Comp->IsKinematic();

    if (bSimulate)
    {
        Desc.BodyType = EPhysicsBodyType::Dynamic;
        Desc.SyncMode = EPhysicsSyncMode::PhysicsToEngine;
    }
    else if (bKinematic)
    {
        Desc.BodyType = EPhysicsBodyType::Kinematic;
        Desc.SyncMode = EPhysicsSyncMode::KinematicTarget;
    }
    else
    {
        Desc.BodyType = EPhysicsBodyType::Static;
        Desc.SyncMode = EPhysicsSyncMode::EngineToPhysics;
    }

    Desc.Mass                    = Comp->GetMass();
    Desc.CenterOfMassLocalOffset = Comp->GetCenterOfMass();
    Desc.bEnableCCD              = Comp->GetEnableCCD();
    Desc.bGenerateHitEvents      = true;
    Desc.bGenerateOverlapEvents  = Comp->GetGenerateOverlapEvents();

    Desc.bLockLinearX  = Comp->IsLinearLockX();
    Desc.bLockLinearY  = Comp->IsLinearLockY();
    Desc.bLockLinearZ  = Comp->IsLinearLockZ();
    Desc.bLockAngularX = Comp->IsAngularLockX();
    Desc.bLockAngularY = Comp->IsAngularLockY();
    Desc.bLockAngularZ = Comp->IsAngularLockZ();

    return Desc;
}

FPhysicsShapeDesc FPhysXPhysicsScene::BuildShapeDescFromComponent_GameThread(
    UPrimitiveComponent* Comp,
    UPrimitiveComponent* RootComponent
) const
{
    FPhysicsShapeDesc Desc;
    if (!Comp)
    {
        return Desc;
    }

    Desc.LocalTransform   = MakeShapeLocalTransform_GameThread(Comp, RootComponent);
    Desc.CollisionEnabled = Comp->GetCollisionEnabled();

    const bool bOwnerBodyIsDynamic =
            RootComponent && (RootComponent->GetSimulatePhysics() || RootComponent->IsKinematic());

    Desc.bIsTrigger = ShouldBeTriggerShape_GameThread(Comp, bOwnerBodyIsDynamic);

    const bool bEnableCCDForShape =
            Comp->GetEnableCCD() || (RootComponent && RootComponent->GetEnableCCD());
    FillFilterDataFromComponent_GameThread(Desc.FilterData, Comp, Desc.bIsTrigger, bEnableCCDForShape);

    if (auto* Box = Cast<UBoxComponent>(Comp))
    {
        Desc.Type          = EPhysicsShapeType::Box;
        Desc.BoxHalfExtent = Box->GetScaledBoxExtent();
    }
    else if (auto* Sphere = Cast<USphereComponent>(Comp))
    {
        Desc.Type         = EPhysicsShapeType::Sphere;
        Desc.SphereRadius = Sphere->GetScaledSphereRadius();
    }
    else if (auto* Capsule = Cast<UCapsuleComponent>(Comp))
    {
        Desc.Type              = EPhysicsShapeType::Capsule;
        Desc.CapsuleRadius     = Capsule->GetScaledCapsuleRadius();
        Desc.CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    }
    else if (auto* StaticMeshComponent = Cast<UStaticMeshComponent>(Comp))
    {
        const bool bRootBodyIsStatic = RootComponent &&
            !RootComponent->GetSimulatePhysics() &&
            !RootComponent->IsKinematic();
        UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
        FStaticMesh* MeshAsset = StaticMesh ? StaticMesh->GetStaticMeshAsset() : nullptr;

        if (bRootBodyIsStatic && MeshAsset && MeshAsset->Vertices.size() >= 3 && MeshAsset->Indices.size() >= 3)
        {
            const FVector ComponentScale = StaticMeshComponent->GetWorldScale();
            auto MeshData = std::make_shared<FPhysicsShapeDesc::FTriangleMeshData>();
            MeshData->Vertices.reserve(MeshAsset->Vertices.size());
            for (const FNormalVertex& Vertex : MeshAsset->Vertices)
            {
                MeshData->Vertices.push_back(FVector(
                    Vertex.pos.X * ComponentScale.X,
                    Vertex.pos.Y * ComponentScale.Y,
                    Vertex.pos.Z * ComponentScale.Z
                ));
            }
            MeshData->Indices = MeshAsset->Indices;

            Desc.Type = EPhysicsShapeType::TriangleMesh;
            Desc.TriangleMeshData = std::move(MeshData);
            Desc.LocalTransform = MakeShapeLocalTransform_GameThread(Comp, RootComponent);
            UE_LOG("Physics: Static mesh triangle collision registered. Component=%s Vertices=%zu Triangles=%zu",
                Comp->GetName().c_str(),
                MeshAsset->Vertices.size(),
                MeshAsset->Indices.size() / 3);
        }
        else
        {
            const FBoundingBox Bounds      = Comp->GetWorldBoundingBox();
            const FVector      WorldCenter = Bounds.GetCenter();
            FVector            WorldExtent = Bounds.GetExtent();

            if (WorldExtent.X <= 0.0f || WorldExtent.Y <= 0.0f || WorldExtent.Z <= 0.0f)
            {
                WorldExtent = FVector(0.5f, 0.5f, 0.5f);
            }

            Desc.Type           = EPhysicsShapeType::Box;
            Desc.BoxHalfExtent  = WorldExtent;
            Desc.LocalTransform = MakeRelativeTransformFromWorld_GameThread(
                WorldCenter,
                GetComponentWorldRotationNoScale_GameThread(Comp),
                RootComponent
            );
        }
    }
    else
    {
        const FBoundingBox Bounds      = Comp->GetWorldBoundingBox();
        const FVector      WorldCenter = Bounds.GetCenter();
        FVector            WorldExtent = Bounds.GetExtent();

        if (WorldExtent.X <= 0.0f || WorldExtent.Y <= 0.0f || WorldExtent.Z <= 0.0f)
        {
            WorldExtent = FVector(0.5f, 0.5f, 0.5f);
        }

        Desc.Type           = EPhysicsShapeType::Box;
        Desc.BoxHalfExtent  = WorldExtent;
        Desc.LocalTransform = MakeRelativeTransformFromWorld_GameThread(
            WorldCenter,
            GetComponentWorldRotationNoScale_GameThread(Comp),
            RootComponent
        );
    }

    return Desc;
}

FPhysicsBodyCreatePayload FPhysXPhysicsScene::BuildRegisterPayload_GameThread(
    UPrimitiveComponent*      Comp,
    FPhysicsComponentBinding& Binding
)
{
    FPhysicsBodyCreatePayload Payload;
    if (!IsValid(Comp))
    {
        return Payload;
    }

    AActor* OwnerActor = Comp->GetOwner();
    if (!IsValid(OwnerActor))
    {
        return Payload;
    }

    UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent());
    if (!RootPrim)
    {
        RootPrim = Comp;
    }

    FPhysicsComponentBinding& RootBinding = TouchBinding_GameThread(RootPrim);
    const uint32              ActorId     = OwnerActor->GetUUID();

    FPhysicsBodyHandle BodyHandle;
    auto               ActorBodyIt = GameThreadActorBodies.find(ActorId);
    if (ActorBodyIt != GameThreadActorBodies.end() && ActorBodyIt->second.IsValid())
    {
        BodyHandle = ActorBodyIt->second;
    }
    else
    {
        BodyHandle                     = Runtime.ReserveBodyHandle_GameThread();
        GameThreadActorBodies[ActorId] = BodyHandle;
    }

    const FPhysicsShapeHandle ShapeHandle = Runtime.ReserveShapeHandle_GameThread();

    FBodyCreationDesc BodyDesc  = BuildBodyDescFromComponent_GameThread(RootPrim, RootBinding.Generation);
    FPhysicsShapeDesc ShapeDesc = BuildShapeDescFromComponent_GameThread(Comp, RootPrim);

    Payload.ReservedBody  = BodyHandle;
    Payload.ReservedShape = ShapeHandle;

    Payload.BodyOwner.ActorId             = ActorId;
    Payload.BodyOwner.ComponentId         = RootPrim->GetUUID();
    Payload.BodyOwner.ComponentGeneration = RootBinding.Generation;
    Payload.BodyOwner.Domain              = EPhysicsBodyDomain::ActorComponent;

    Payload.ShapeOwner.ActorId             = ActorId;
    Payload.ShapeOwner.ComponentId         = Binding.ComponentId;
    Payload.ShapeOwner.ComponentGeneration = Binding.Generation;
    Payload.ShapeOwner.Domain              = EPhysicsBodyDomain::ActorComponent;

    Payload.BodyType                     = BodyDesc.BodyType;
    Payload.SyncMode                     = BodyDesc.SyncMode;
    Payload.WorldTransform               = BodyDesc.WorldTransform;
    Payload.Mass                         = BodyDesc.Mass;
    Payload.CenterOfMassLocalOffset      = BodyDesc.CenterOfMassLocalOffset;
    Payload.LinearDamping                = BodyDesc.LinearDamping;
    Payload.AngularDamping               = BodyDesc.AngularDamping;
    Payload.MaxAngularVelocity           = BodyDesc.MaxAngularVelocity;
    Payload.PositionSolverIterationCount = BodyDesc.PositionSolverIterationCount;
    Payload.VelocitySolverIterationCount = BodyDesc.VelocitySolverIterationCount;
    Payload.bLockLinearX                 = BodyDesc.bLockLinearX;
    Payload.bLockLinearY                 = BodyDesc.bLockLinearY;
    Payload.bLockLinearZ                 = BodyDesc.bLockLinearZ;
    Payload.bLockAngularX                = BodyDesc.bLockAngularX;
    Payload.bLockAngularY                = BodyDesc.bLockAngularY;
    Payload.bLockAngularZ                = BodyDesc.bLockAngularZ;
    Payload.bEnableGravity               = BodyDesc.bEnableGravity;
    Payload.bEnableCCD                   = BodyDesc.bEnableCCD;
    Payload.bGenerateHitEvents           = BodyDesc.bGenerateHitEvents;
    Payload.bGenerateOverlapEvents       = BodyDesc.bGenerateOverlapEvents;
    Payload.Shapes.push_back(ShapeDesc);

    RootBinding.Body     = BodyHandle;
    RootBinding.SyncMode = Payload.SyncMode;
    Binding.Body         = BodyHandle;
    Binding.Shape        = ShapeHandle;
    Binding.SyncMode     = Payload.SyncMode;

    return Payload;
}

void FPhysXPhysicsScene::Tick(float DeltaTime)
{
    uint64 FrameIndex = 0;
    {
        std::lock_guard<std::mutex> Lock(PhysicsThreadMutex);
        FrameIndex = CompletedPhysicsFrameIndex + 1;
    }
    SubmitPhysicsFrame(FrameIndex, DeltaTime);
    WaitPhysicsFrame(FrameIndex);
}

void FPhysXPhysicsScene::SubmitPhysicsFrame(uint64 FrameIndex, float DeltaTime)
{
    EnqueueEngineTransformSync_GameThread();

    TArray<FPhysicsCommand> FrameCommands;
    Runtime.DrainPendingCommands_GameThread(FrameCommands);
    StampContinuousForceCommandDurations(FrameCommands, DeltaTime);

    std::unique_lock<std::mutex> Lock(PhysicsThreadMutex);
    if (!bPhysicsThreadStarted)
    {
        Lock.unlock();
        RunPhysicsFrame_PhysicsThread(DeltaTime, FrameCommands);
        Lock.lock();
        CompletedPhysicsFrameIndex = (std::max)(CompletedPhysicsFrameIndex, FrameIndex);
        PhysicsThreadDoneCv.notify_all();
        return;
    }

    const bool bAsync = FProjectSettings::Get().Physics.bAsyncPhysics;
    if (bAsync)
    {
        if (bPhysicsFramePending || bPhysicsFrameInProgress)
        {
            PendingPhysicsDeltaTime  += DeltaTime;
            PendingPhysicsFrameIndex = FrameIndex;
            PendingPhysicsFrameCommands.insert(PendingPhysicsFrameCommands.end(), FrameCommands.begin(), FrameCommands.end());
            if (!bPhysicsFramePending)
            {
                bPhysicsFramePending = true;
                PhysicsThreadCv.notify_one();
            }
            return;
        }

        PendingPhysicsFrameIndex    = FrameIndex;
        PendingPhysicsDeltaTime     = DeltaTime;
        PendingPhysicsFrameCommands = std::move(FrameCommands);
        bPhysicsFramePending        = true;
        PhysicsThreadCv.notify_one();
        return;
    }

    PhysicsThreadDoneCv.wait(
        Lock,
        [this]()
        {
            return !bPhysicsFramePending && !bPhysicsFrameInProgress && !bPhysicsQueryPending && !bPhysicsQueryInProgress;
        }
    );

    PendingPhysicsFrameIndex    = FrameIndex;
    PendingPhysicsDeltaTime     = DeltaTime;
    PendingPhysicsFrameCommands = std::move(FrameCommands);
    bPhysicsFramePending        = true;
    PhysicsThreadCv.notify_one();
}

void FPhysXPhysicsScene::WaitPhysicsFrame(uint64 FrameIndex)
{
    std::unique_lock<std::mutex> Lock(PhysicsThreadMutex);
    PhysicsThreadDoneCv.wait(
        Lock,
        [this, FrameIndex]()
        {
            return CompletedPhysicsFrameIndex >= FrameIndex &&
                    !bPhysicsFramePending &&
                    !bPhysicsFrameInProgress;
        }
    );
}

void FPhysXPhysicsScene::RunPhysicsFrame_PhysicsThread(float DeltaTime, const TArray<FPhysicsCommand>& FrameCommands)
{
    Runtime.Tick(DeltaTime, FrameCommands);
}

void FPhysXPhysicsScene::PhysicsThreadMain()
{
    for (;;)
    {
        uint64                  FrameIndex = 0;
        float                   DeltaTime  = 0.0f;
        TArray<FPhysicsCommand> FrameCommands;

        {
            std::unique_lock<std::mutex> Lock(PhysicsThreadMutex);
            PhysicsThreadCv.wait(
                Lock,
                [this]()
                {
                    return bPhysicsThreadStopRequested || bPhysicsFramePending || bPhysicsQueryPending;
                }
            );

            if (bPhysicsThreadStopRequested && !bPhysicsFramePending && !bPhysicsQueryPending)
            {
                break;
            }

            if (bPhysicsQueryPending)
            {
                const bool              bObjectTypes       = bPendingQueryObjectTypes;
                const bool              bSweepQuery        = bPendingQuerySweep;
                const FVector           QueryStart         = PendingQueryStart;
                const FVector           QueryDir           = PendingQueryDir;
                const float             QueryMaxDist       = PendingQueryMaxDist;
                const FQuat             QueryRotation      = PendingQueryRotation;
                const FCollisionShape   QueryShape         = PendingQueryShape;
                const ECollisionChannel QueryChannel       = PendingQueryTraceChannel;
                const uint32            QueryObjectMask    = PendingQueryObjectTypeMask;
                const uint32            QueryIgnoreActorId = PendingQueryIgnoreActorId;

                bPhysicsQueryPending    = false;
                bPhysicsQueryInProgress = true;
                Lock.unlock();

                bool bHit = false;
                FPhysicsRaycastResult RaycastResult;
                FPhysicsSweepResult   SweepResult;
                if (bSweepQuery)
                {
                    bHit = bObjectTypes
                            ? ExecuteSweepByObjectTypes_PhysicsThread(QueryStart, QueryDir, QueryMaxDist, QueryRotation, QueryShape, QueryObjectMask, QueryIgnoreActorId, SweepResult)
                            : ExecuteSweep_PhysicsThread(QueryStart, QueryDir, QueryMaxDist, QueryRotation, QueryShape, QueryChannel, QueryIgnoreActorId, SweepResult);
                }
                else
                {
                    bHit = bObjectTypes
                            ? ExecuteRaycastByObjectTypes_PhysicsThread(QueryStart, QueryDir, QueryMaxDist, QueryObjectMask, QueryIgnoreActorId, RaycastResult)
                            : ExecuteRaycast_PhysicsThread(QueryStart, QueryDir, QueryMaxDist, QueryChannel, QueryIgnoreActorId, RaycastResult);
                }

                Lock.lock();
                PendingQueryResult      = RaycastResult;
                PendingSweepQueryResult = SweepResult;
                bPendingQueryHit        = bHit;
                bPhysicsQueryInProgress = false;
                bPhysicsQueryCompleted  = true;
                PhysicsThreadDoneCv.notify_all();
                continue;
            }

            FrameIndex    = PendingPhysicsFrameIndex;
            DeltaTime     = PendingPhysicsDeltaTime;
            FrameCommands = std::move(PendingPhysicsFrameCommands);
            PendingPhysicsFrameCommands.clear();
            PendingPhysicsDeltaTime = 0.0f;
            bPhysicsFramePending    = false;
            bPhysicsFrameInProgress = true;
        }

        RunPhysicsFrame_PhysicsThread(DeltaTime, FrameCommands);

        {
            std::lock_guard<std::mutex> Lock(PhysicsThreadMutex);
            CompletedPhysicsFrameIndex = (std::max)(CompletedPhysicsFrameIndex, FrameIndex);
            bPhysicsFrameInProgress    = false;
        }
        PhysicsThreadDoneCv.notify_all();
    }

    {
        std::lock_guard<std::mutex> Lock(PhysicsThreadMutex);
        bPhysicsThreadStarted   = false;
        bPhysicsFramePending    = false;
        bPhysicsFrameInProgress = false;
        PendingPhysicsFrameCommands.clear();
        bPhysicsQueryPending    = false;
        bPhysicsQueryInProgress = false;
        bPhysicsQueryCompleted  = true;
        bPendingQuerySweep      = false;
    }
    PhysicsThreadDoneCv.notify_all();
}

void FPhysXPhysicsScene::StartPhysicsThread()
{
    std::lock_guard<std::mutex> Lock(PhysicsThreadMutex);
    if (bPhysicsThreadStarted)
    {
        return;
    }

    bPhysicsThreadStopRequested = false;
    bPhysicsFramePending        = false;
    bPhysicsFrameInProgress     = false;
    bPhysicsQueryPending        = false;
    bPhysicsQueryInProgress     = false;
    bPhysicsQueryCompleted      = false;
    bPendingQuerySweep          = false;
    CompletedPhysicsFrameIndex  = 0;
    PendingPhysicsFrameIndex    = 0;
    PendingPhysicsDeltaTime     = 0.0f;
    PendingPhysicsFrameCommands.clear();
    bPhysicsThreadStarted       = true;
    PhysicsThread               = std::thread(&FPhysXPhysicsScene::PhysicsThreadMain, this);
}

void FPhysXPhysicsScene::StopPhysicsThreadAndJoin()
{
    {
        std::unique_lock<std::mutex> Lock(PhysicsThreadMutex);
        if (!bPhysicsThreadStarted && !PhysicsThread.joinable())
        {
            return;
        }

        bPhysicsThreadStopRequested = true;

        if (bPhysicsQueryPending && !bPhysicsQueryInProgress)
        {
            bPhysicsQueryPending   = false;
            bPhysicsQueryCompleted = true;
            bPendingQueryHit       = false;
            PendingQueryResult     = FPhysicsRaycastResult();
        }

        PhysicsThreadCv.notify_one();
        PhysicsThreadDoneCv.notify_all();

        PhysicsThreadDoneCv.wait(
            Lock,
            [this]()
            {
                return !bPhysicsFramePending && !bPhysicsFrameInProgress && !bPhysicsQueryPending && !bPhysicsQueryInProgress;
            }
        );
    }

    if (PhysicsThread.joinable())
    {
        PhysicsThread.join();
    }

    std::lock_guard<std::mutex> Lock(PhysicsThreadMutex);
    bPhysicsThreadStarted       = false;
    bPhysicsThreadStopRequested = false;
    PendingPhysicsFrameCommands.clear();
}

void FPhysXPhysicsScene::EnqueueEngineTransformSync_GameThread()
{
    for (const auto& Pair : GameThreadBindings)
    {
        const FPhysicsComponentBinding& Binding = Pair.second;
        if (Binding.bPendingDestroy || !Binding.Body.IsValid())
        {
            continue;
        }
        if (Binding.SyncMode != EPhysicsSyncMode::EngineToPhysics &&
            Binding.SyncMode != EPhysicsSyncMode::KinematicTarget)
        {
            continue;
        }

        UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(
            UObjectManager::Get().FindByUUID(Binding.ComponentId)
        );
        if (!IsValid(Component))
        {
            continue;
        }

        Runtime.SetBodyTransform(
            Binding.Body,
            GetComponentWorldTransform_GameThread(Component),
            EPhysicsTeleportMode::None
        );
    }
}

void FPhysXPhysicsScene::ConsumeCreationResults_GameThread()
{
    TArray<FPhysicsCreationResult> Results;
    Runtime.ConsumeCreationResults_GameThread(Results);

    for (const FPhysicsCreationResult& Result : Results)
    {
        if (Result.Type != EPhysicsCreationResultType::Body || Result.Owner.Domain != EPhysicsBodyDomain::ActorComponent || Result.Owner.ComponentId == 0)
        {
            continue;
        }

        FPhysicsComponentBinding* Binding = FindBinding_GameThread(Result.Owner.ComponentId);
        if (!Binding || Binding->Generation != Result.Owner.ComponentGeneration)
        {
            continue;
        }

        Binding->bPendingCreate        = false;
        Binding->bAliveOnPhysicsThread = Result.bSuccess;

        if (!Result.bSuccess)
        {
            Binding->bPendingDestroy = true;
            Binding->Body            = FPhysicsBodyHandle {};
            Binding->Shape           = FPhysicsShapeHandle {};
        }
    }
}

void FPhysXPhysicsScene::DispatchPendingEvents()
{
    ConsumeCreationResults_GameThread();

    if (EventCallback)
    {
        using FClock = std::chrono::high_resolution_clock;
        const auto DispatchStart = FClock::now();

        const auto& PhysicsSettings = FProjectSettings::Get().Physics;
        EventCallback->DispatchPendingEvents(
            PhysicsSettings.bDispatchCollisionEvents,
            PhysicsSettings.bDispatchTriggerEvents,
            [this](uint32 ComponentId, uint32 Generation)
            {
                const FPhysicsComponentBinding* Binding = FindBinding_GameThread(ComponentId);
                return Binding && Binding->Generation == Generation && !Binding->bPendingDestroy;
            }
        );

        int32 ContactPairs = 0;
        int32 TriggerPairs = 0;
        EventCallback->GetActivePairCounts(ContactPairs, TriggerPairs);

        const float DispatchMs = std::chrono::duration<float, std::milli>(FClock::now() - DispatchStart).count();
        Runtime.SetEventStats(ContactPairs, TriggerPairs, DispatchMs);
    }
}

void FPhysXPhysicsScene::AddForce(UPrimitiveComponent* Comp, const FVector& Force)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (Binding && Binding->Body.IsValid() && !Binding->bPendingDestroy)
    {
        Runtime.AddForce(Binding->Body, Force);
    }
}

void FPhysXPhysicsScene::AddForceAtLocation(
    UPrimitiveComponent* Comp,
    const FVector& Force,
    const FVector& WorldLocation
)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (Binding && Binding->Body.IsValid() && !Binding->bPendingDestroy)
    {
        Runtime.AddForceAtLocation(Binding->Body, Force, WorldLocation);
    }
}

void FPhysXPhysicsScene::AddTorque(UPrimitiveComponent* Comp, const FVector& Torque)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (Binding && Binding->Body.IsValid() && !Binding->bPendingDestroy)
    {
        Runtime.AddTorque(Binding->Body, Torque);
    }
}

void FPhysXPhysicsScene::AddImpulse(UPrimitiveComponent* Comp, const FVector& Impulse)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (Binding && Binding->Body.IsValid() && !Binding->bPendingDestroy)
    {
        Runtime.AddImpulse(Binding->Body, Impulse);
    }
}

FVector FPhysXPhysicsScene::GetLinearVelocity(UPrimitiveComponent* Comp) const
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (!Binding || !Binding->Body.IsValid())
        return FVector::ZeroVector;

    std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = Runtime.AcquireLatestSnapshotRef();
    const FPhysicsBodySnapshot*                  Body     = Snapshot ? Snapshot->FindByBody(Binding->Body) : nullptr;
    return Body ? Body->LinearVelocity : FVector::ZeroVector;
}

void FPhysXPhysicsScene::SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (!Binding || !Binding->Body.IsValid() || Binding->bPendingDestroy)
    {
        return;
    }

    const FVector CurrentAngular = GetAngularVelocity(Comp);
    Runtime.SetBodyVelocity(Binding->Body, Vel, CurrentAngular);
}

FVector FPhysXPhysicsScene::GetAngularVelocity(UPrimitiveComponent* Comp) const
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (!Binding || !Binding->Body.IsValid())
        return FVector::ZeroVector;

    std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = Runtime.AcquireLatestSnapshotRef();
    const FPhysicsBodySnapshot*                  Body     = Snapshot ? Snapshot->FindByBody(Binding->Body) : nullptr;
    return Body ? Body->AngularVelocity : FVector::ZeroVector;
}

void FPhysXPhysicsScene::SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (!Binding || !Binding->Body.IsValid() || Binding->bPendingDestroy)
    {
        return;
    }

    const FVector CurrentLinear = GetLinearVelocity(Comp);
    Runtime.SetBodyVelocity(Binding->Body, CurrentLinear, Vel);
}

void FPhysXPhysicsScene::SetMass(UPrimitiveComponent* Comp, float Mass)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (Binding && Binding->Body.IsValid() && !Binding->bPendingDestroy)
    {
        Runtime.SetMass(Binding->Body, Mass);
    }
}

float FPhysXPhysicsScene::GetMass(UPrimitiveComponent* Comp) const
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (!Binding || !Binding->Body.IsValid())
        return 1.0f;

    std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = Runtime.AcquireLatestSnapshotRef();
    const FPhysicsBodySnapshot*                  Body     = Snapshot ? Snapshot->FindByBody(Binding->Body) : nullptr;
    return Body ? Body->Mass : 1.0f;
}

void FPhysXPhysicsScene::SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (Binding && Binding->Body.IsValid() && !Binding->bPendingDestroy)
    {
        Runtime.SetCenterOfMass(Binding->Body, LocalOffset);
    }
}

FVector FPhysXPhysicsScene::GetCenterOfMass(UPrimitiveComponent* Comp) const
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (!Binding || !Binding->Body.IsValid())
        return FVector::ZeroVector;

    std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = Runtime.AcquireLatestSnapshotRef();
    const FPhysicsBodySnapshot*                  Body     = Snapshot ? Snapshot->FindByBody(Binding->Body) : nullptr;
    return Body ? Body->CenterOfMass : FVector::ZeroVector;
}

void FPhysXPhysicsScene::SetLinearLock(UPrimitiveComponent* Comp, bool bX, bool bY, bool bZ)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (Binding && Binding->Body.IsValid() && !Binding->bPendingDestroy)
    {
        Runtime.SetLinearLock(Binding->Body, bX, bY, bZ);
    }
}

void FPhysXPhysicsScene::SetAngularLock(UPrimitiveComponent* Comp, bool bX, bool bY, bool bZ)
{
    const FPhysicsComponentBinding* Binding = Comp ? FindBinding_GameThread(Comp->GetUUID()) : nullptr;
    if (Binding && Binding->Body.IsValid() && !Binding->bPendingDestroy)
    {
        Runtime.SetAngularLock(Binding->Body, bX, bY, bZ);
    }
}

bool FPhysXPhysicsScene::ResolveRaycastResult_GameThread(
    const FPhysicsRaycastResult& PhysicsResult,
    FHitResult&                  OutHit
) const
{
    OutHit = FHitResult();
    if (!PhysicsResult.bBlockingHit || PhysicsResult.HitComponentId == 0)
    {
        return false;
    }

    const FPhysicsComponentBinding* Binding = FindBinding_GameThread(PhysicsResult.HitComponentId);
    if (!Binding || Binding->Generation != PhysicsResult.HitGeneration || Binding->bPendingDestroy)
    {
        return false;
    }

    UPrimitiveComponent* HitComp = Cast<UPrimitiveComponent>(
        UObjectManager::Get().FindByUUID(PhysicsResult.HitComponentId)
    );
    if (!IsValid(HitComp))
    {
        return false;
    }

    OutHit.bHit             = true;
    OutHit.Distance         = PhysicsResult.Distance;
    OutHit.WorldHitLocation = PhysicsResult.Location;
    OutHit.ImpactNormal     = PhysicsResult.ImpactNormal;
    OutHit.WorldNormal      = PhysicsResult.Normal;
    OutHit.HitComponent     = HitComp;
    OutHit.HitActor         = HitComp->GetOwner();
    return true;
}

bool FPhysXPhysicsScene::SubmitRaycastQuery_GameThread(
    bool                   bObjectTypes,
    const FVector&         Start,
    const FVector&         Dir,
    float                  MaxDist,
    ECollisionChannel      TraceChannel,
    uint32                 ObjectTypeMask,
    uint32                 IgnoreActorId,
    FPhysicsRaycastResult& OutResult
)
{
    OutResult = FPhysicsRaycastResult();
    if (!Scene || MaxDist <= 0.0f)
    {
        return false;
    }

    FVector RayDir = Dir;
    if (RayDir.IsNearlyZero())
    {
        return false;
    }
    RayDir.Normalize();

    Runtime.RecordRaycastQuery();

    std::unique_lock<std::mutex> Lock(PhysicsThreadMutex);
    PhysicsThreadDoneCv.wait(
        Lock,
        [this]()
        {
            return bPhysicsThreadStopRequested || (!bPhysicsQueryPending && !bPhysicsQueryInProgress);
        }
    );

    if (bPhysicsThreadStopRequested)
    {
        return false;
    }

    if (!bPhysicsThreadStarted)
    {
        Lock.unlock();
        return bObjectTypes
                ? ExecuteRaycastByObjectTypes_PhysicsThread(Start, RayDir, MaxDist, ObjectTypeMask, IgnoreActorId, OutResult)
                : ExecuteRaycast_PhysicsThread(Start, RayDir, MaxDist, TraceChannel, IgnoreActorId, OutResult);
    }

    PendingQueryStart          = Start;
    PendingQueryDir            = RayDir;
    PendingQueryMaxDist        = MaxDist;
    PendingQueryRotation       = FQuat::Identity;
    PendingQueryShape          = FCollisionShape();
    PendingQueryTraceChannel   = TraceChannel;
    PendingQueryObjectTypeMask = ObjectTypeMask;
    PendingQueryIgnoreActorId  = IgnoreActorId;
    bPendingQueryObjectTypes   = bObjectTypes;
    bPendingQuerySweep         = false;
    bPendingQueryHit           = false;
    PendingQueryResult         = FPhysicsRaycastResult();
    PendingSweepQueryResult    = FPhysicsSweepResult();
    bPhysicsQueryCompleted     = false;
    bPhysicsQueryPending       = true;
    PhysicsThreadCv.notify_one();

    PhysicsThreadDoneCv.wait(
        Lock,
        [this]()
        {
            return bPhysicsQueryCompleted || bPhysicsThreadStopRequested;
        }
    );

    if (bPhysicsThreadStopRequested && !bPhysicsQueryCompleted)
    {
        bPhysicsQueryPending   = false;
        bPhysicsQueryCompleted = false;
        return false;
    }

    OutResult              = PendingQueryResult;
    const bool bHit        = bPendingQueryHit;
    bPhysicsQueryCompleted = false;
    return bHit;
}


bool FPhysXPhysicsScene::ResolveSweepResult_GameThread(
    const FPhysicsSweepResult& PhysicsResult,
    FHitResult&                OutHit
) const
{
    OutHit = FHitResult();
    if (!PhysicsResult.bBlockingHit || PhysicsResult.HitComponentId == 0)
    {
        return false;
    }

    const FPhysicsComponentBinding* Binding = FindBinding_GameThread(PhysicsResult.HitComponentId);
    if (!Binding || Binding->Generation != PhysicsResult.HitGeneration || Binding->bPendingDestroy)
    {
        return false;
    }

    UPrimitiveComponent* HitComp = Cast<UPrimitiveComponent>(
        UObjectManager::Get().FindByUUID(PhysicsResult.HitComponentId)
    );
    if (!IsValid(HitComp))
    {
        return false;
    }

    OutHit.bHit              = true;
    OutHit.bStartPenetrating = PhysicsResult.bStartPenetrating;
    OutHit.Distance          = PhysicsResult.Distance;
    OutHit.PenetrationDepth  = PhysicsResult.PenetrationDepth;
    OutHit.WorldHitLocation  = PhysicsResult.Location;
    OutHit.ImpactNormal      = PhysicsResult.ImpactNormal;
    OutHit.WorldNormal       = PhysicsResult.Normal;
    OutHit.HitComponent      = HitComp;
    OutHit.HitActor          = HitComp->GetOwner();
    return true;
}

bool FPhysXPhysicsScene::SubmitSweepQuery_GameThread(
    bool                         bObjectTypes,
    const FVector&               Start,
    const FVector&               Dir,
    float                        MaxDist,
    const FQuat&                 Rotation,
    const FCollisionShape&       Shape,
    ECollisionChannel            TraceChannel,
    uint32                       ObjectTypeMask,
    uint32                       IgnoreActorId,
    FPhysicsSweepResult&         OutResult
)
{
    OutResult = FPhysicsSweepResult();
    if (!Scene || MaxDist <= 0.0f)
    {
        return false;
    }

    FVector SweepDir = Dir;
    if (SweepDir.IsNearlyZero())
    {
        return false;
    }
    SweepDir.Normalize();

    PxGeometryHolder Geometry;
    PxQuat ShapeAxisRotation(PxIdentity);
    if (!BuildPxSweepGeometry(Shape, Geometry, ShapeAxisRotation))
    {
        return false;
    }

    Runtime.RecordRaycastQuery();

    std::unique_lock<std::mutex> Lock(PhysicsThreadMutex);
    PhysicsThreadDoneCv.wait(
        Lock,
        [this]()
        {
            return bPhysicsThreadStopRequested || (!bPhysicsQueryPending && !bPhysicsQueryInProgress);
        }
    );

    if (bPhysicsThreadStopRequested)
    {
        return false;
    }

    if (!bPhysicsThreadStarted)
    {
        Lock.unlock();
        return bObjectTypes
                ? ExecuteSweepByObjectTypes_PhysicsThread(Start, SweepDir, MaxDist, Rotation, Shape, ObjectTypeMask, IgnoreActorId, OutResult)
                : ExecuteSweep_PhysicsThread(Start, SweepDir, MaxDist, Rotation, Shape, TraceChannel, IgnoreActorId, OutResult);
    }

    PendingQueryStart          = Start;
    PendingQueryDir            = SweepDir;
    PendingQueryMaxDist        = MaxDist;
    PendingQueryRotation       = Rotation;
    PendingQueryShape          = Shape;
    PendingQueryTraceChannel   = TraceChannel;
    PendingQueryObjectTypeMask = ObjectTypeMask;
    PendingQueryIgnoreActorId  = IgnoreActorId;
    bPendingQueryObjectTypes   = bObjectTypes;
    bPendingQuerySweep         = true;
    bPendingQueryHit           = false;
    PendingQueryResult         = FPhysicsRaycastResult();
    PendingSweepQueryResult    = FPhysicsSweepResult();
    bPhysicsQueryCompleted     = false;
    bPhysicsQueryPending       = true;
    PhysicsThreadCv.notify_one();

    PhysicsThreadDoneCv.wait(
        Lock,
        [this]()
        {
            return bPhysicsQueryCompleted || bPhysicsThreadStopRequested;
        }
    );

    if (bPhysicsThreadStopRequested && !bPhysicsQueryCompleted)
    {
        bPhysicsQueryPending   = false;
        bPhysicsQueryCompleted = false;
        return false;
    }

    OutResult              = PendingSweepQueryResult;
    const bool bHit        = bPendingQueryHit;
    bPhysicsQueryCompleted = false;
    return bHit;
}

bool FPhysXPhysicsScene::ExecuteRaycast_PhysicsThread(
    const FVector&         Start,
    const FVector&         Dir,
    float                  MaxDist,
    ECollisionChannel      TraceChannel,
    uint32                 IgnoreActorId,
    FPhysicsRaycastResult& OutResult
) const
{
    OutResult = FPhysicsRaycastResult();
    if (!Scene || MaxDist <= 0.0f)
    {
        return false;
    }

    FVector RayDir = Dir;
    if (RayDir.IsNearlyZero())
    {
        return false;
    }
    RayDir.Normalize();

    struct FChannelRaycastFilter : PxQueryFilterCallback
    {
        uint32 IgnoreActorId = 0;
        PxU32  TraceBit      = 0;

        FChannelRaycastFilter(uint32 InIgnoreActorId, ECollisionChannel InChannel) : IgnoreActorId(InIgnoreActorId)
                                                                                   , TraceBit(1u << static_cast<PxU32>(InChannel))
        {
        }

        PxQueryHitType::Enum preFilter(
            const PxFilterData&,
            const PxShape* Shape,
            const PxRigidActor* Actor,
            PxHitFlags&
        ) override
        {
            const uint32 ShapeActorId = GetActorIdFromShape(Shape);
            const uint32 BodyActorId  = GetActorIdFromActor(Actor);
            if (IgnoreActorId != 0 && (ShapeActorId == IgnoreActorId || BodyActorId == IgnoreActorId))
            {
                return PxQueryHitType::eNONE;
            }

            if (Shape)
            {
                if (Shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE))
                {
                    return PxQueryHitType::eNONE;
                }

                const PxFilterData ShapeData = Shape->getQueryFilterData();
                if ((ShapeData.word1 & TraceBit) == 0)
                {
                    return PxQueryHitType::eNONE;
                }
            }

            return PxQueryHitType::eBLOCK;
        }

        PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
        {
            return PxQueryHitType::eBLOCK;
        }
    };

    FChannelRaycastFilter FilterCallback(IgnoreActorId, TraceChannel);

    PxRaycastBuffer Hit;
    PxQueryFilterData FilterData;
    FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

    PxSceneReadLock ReadLock(*Scene);
    const bool bStatus = Scene->raycast(
        ToPxVec3(Start),
        ToPxVec3(RayDir),
        MaxDist,
        Hit,
        PxHitFlag::eDEFAULT,
        FilterData,
        &FilterCallback
    );

    if (!bStatus || !Hit.hasBlock)
    {
        return false;
    }

    const PxRaycastHit& Block = Hit.block;
    OutResult.bBlockingHit    = true;
    OutResult.Distance        = Block.distance;
    OutResult.Location        = ToFVector(Block.position);
    OutResult.ImpactPoint     = OutResult.Location;
    OutResult.ImpactNormal    = ToFVector(Block.normal);
    OutResult.Normal          = OutResult.ImpactNormal;

    FillRaycastPhysicsHit(Block, OutResult);

    return OutResult.bBlockingHit;
}

bool FPhysXPhysicsScene::ExecuteRaycastByObjectTypes_PhysicsThread(
    const FVector&         Start,
    const FVector&         Dir,
    float                  MaxDist,
    uint32                 ObjectTypeMask,
    uint32                 IgnoreActorId,
    FPhysicsRaycastResult& OutResult
) const
{
    OutResult = FPhysicsRaycastResult();
    if (!Scene || ObjectTypeMask == 0 || MaxDist <= 0.0f)
    {
        return false;
    }

    FVector RayDir = Dir;
    if (RayDir.IsNearlyZero())
    {
        return false;
    }
    RayDir.Normalize();

    struct FObjectTypeRaycastFilter : PxQueryFilterCallback
    {
        uint32 IgnoreActorId  = 0;
        PxU32  ObjectTypeMask = 0;

        FObjectTypeRaycastFilter(uint32 InIgnoreActorId, PxU32 InMask) : IgnoreActorId(InIgnoreActorId)
                                                                       , ObjectTypeMask(InMask)
        {
        }

        PxQueryHitType::Enum preFilter(
            const PxFilterData&,
            const PxShape* Shape,
            const PxRigidActor* Actor,
            PxHitFlags&
        ) override
        {
            const uint32 ShapeActorId = GetActorIdFromShape(Shape);
            const uint32 BodyActorId  = GetActorIdFromActor(Actor);
            if (IgnoreActorId != 0 && (ShapeActorId == IgnoreActorId || BodyActorId == IgnoreActorId))
            {
                return PxQueryHitType::eNONE;
            }

            if (Shape)
            {
                if (Shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE))
                {
                    return PxQueryHitType::eNONE;
                }

                const PxFilterData ShapeData      = Shape->getQueryFilterData();
                const PxU32        ShapeObjectBit = 1u << GetPhysicsFilterObjectType(ShapeData.word0);
                if ((ShapeObjectBit & ObjectTypeMask) == 0)
                {
                    return PxQueryHitType::eNONE;
                }
            }

            return PxQueryHitType::eBLOCK;
        }

        PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
        {
            return PxQueryHitType::eBLOCK;
        }
    };

    FObjectTypeRaycastFilter FilterCallback(IgnoreActorId, ObjectTypeMask);

    PxRaycastBuffer Hit;
    PxQueryFilterData FilterData;
    FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

    PxSceneReadLock ReadLock(*Scene);
    const bool bStatus = Scene->raycast(
        ToPxVec3(Start),
        ToPxVec3(RayDir),
        MaxDist,
        Hit,
        PxHitFlag::eDEFAULT,
        FilterData,
        &FilterCallback
    );

    if (!bStatus || !Hit.hasBlock)
    {
        return false;
    }

    const PxRaycastHit& Block = Hit.block;
    OutResult.bBlockingHit    = true;
    OutResult.Distance        = Block.distance;
    OutResult.Location        = ToFVector(Block.position);
    OutResult.ImpactPoint     = OutResult.Location;
    OutResult.ImpactNormal    = ToFVector(Block.normal);
    OutResult.Normal          = OutResult.ImpactNormal;

    FillRaycastPhysicsHit(Block, OutResult);

    return OutResult.bBlockingHit;
}


bool FPhysXPhysicsScene::ExecuteSweep_PhysicsThread(
    const FVector&         Start,
    const FVector&         Dir,
    float                  MaxDist,
    const FQuat&           Rotation,
    const FCollisionShape& Shape,
    ECollisionChannel      TraceChannel,
    uint32                 IgnoreActorId,
    FPhysicsSweepResult&   OutResult
) const
{
    OutResult = FPhysicsSweepResult();
    if (!Scene || MaxDist <= 0.0f)
    {
        return false;
    }

    FVector SweepDir = Dir;
    if (SweepDir.IsNearlyZero())
    {
        return false;
    }
    SweepDir.Normalize();

    PxGeometryHolder Geometry;
    PxQuat ShapeAxisRotation(PxIdentity);
    if (!BuildPxSweepGeometry(Shape, Geometry, ShapeAxisRotation))
    {
        return false;
    }

    struct FChannelSweepFilter : PxQueryFilterCallback
    {
        uint32 IgnoreActorId = 0;
        PxU32  TraceBit      = 0;

        FChannelSweepFilter(uint32 InIgnoreActorId, ECollisionChannel InChannel) : IgnoreActorId(InIgnoreActorId)
                                                                                 , TraceBit(1u << static_cast<PxU32>(InChannel))
        {
        }

        PxQueryHitType::Enum preFilter(
            const PxFilterData&,
            const PxShape* Shape,
            const PxRigidActor* Actor,
            PxHitFlags&
        ) override
        {
            const uint32 ShapeActorId = GetActorIdFromShape(Shape);
            const uint32 BodyActorId  = GetActorIdFromActor(Actor);
            if (IgnoreActorId != 0 && (ShapeActorId == IgnoreActorId || BodyActorId == IgnoreActorId))
            {
                return PxQueryHitType::eNONE;
            }

            if (Shape)
            {
                if (Shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE))
                {
                    return PxQueryHitType::eNONE;
                }

                const PxFilterData ShapeData = Shape->getQueryFilterData();
                if ((ShapeData.word1 & TraceBit) == 0)
                {
                    return PxQueryHitType::eNONE;
                }
            }

            return PxQueryHitType::eBLOCK;
        }

        PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
        {
            return PxQueryHitType::eBLOCK;
        }
    };

    FChannelSweepFilter FilterCallback(IgnoreActorId, TraceChannel);

    PxSweepBuffer Hit;
    PxQueryFilterData FilterData;
    FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

    const FQuat NormalizedRotation = Rotation.GetNormalized();
    PxTransform StartPose(ToPxVec3(Start), ToPxQuat(NormalizedRotation) * ShapeAxisRotation);

    PxSceneReadLock ReadLock(*Scene);
    const bool bStatus = Scene->sweep(
        Geometry.any(),
        StartPose,
        ToPxVec3(SweepDir),
        MaxDist,
        Hit,
        PxHitFlag::eDEFAULT | PxHitFlag::eMTD,
        FilterData,
        &FilterCallback
    );

    if (!bStatus || !Hit.hasBlock)
    {
        return false;
    }

    const PxSweepHit& Block = Hit.block;
    OutResult.bBlockingHit      = true;
    OutResult.bStartPenetrating = Block.distance <= 0.0f;
    OutResult.Distance          = (std::max)(0.0f, Block.distance);
    OutResult.PenetrationDepth  = OutResult.bStartPenetrating ? (std::max)(0.0f, -Block.distance) : 0.0f;
    OutResult.Location          = ToFVector(Block.position);
    OutResult.ImpactPoint       = OutResult.Location;
    OutResult.ImpactNormal      = ToFVector(Block.normal);
    OutResult.Normal            = OutResult.ImpactNormal;

    if (OutResult.Normal.IsNearlyZero())
    {
        OutResult.Normal       = SweepDir * -1.0f;
        OutResult.ImpactNormal = OutResult.Normal;
    }

    if (Block.shape)
    {
        OutResult.HitComponentId = GetComponentIdFromShape(Block.shape);
        OutResult.HitActorId     = GetActorIdFromShape(Block.shape);
        OutResult.HitGeneration  = GetComponentGenerationFromShape(Block.shape);
    }
    if (OutResult.HitActorId == 0 && Block.actor)
    {
        OutResult.HitActorId = GetActorIdFromActor(Block.actor);
    }

    return OutResult.bBlockingHit;
}

bool FPhysXPhysicsScene::ExecuteSweepByObjectTypes_PhysicsThread(
    const FVector&         Start,
    const FVector&         Dir,
    float                  MaxDist,
    const FQuat&           Rotation,
    const FCollisionShape& Shape,
    uint32                 ObjectTypeMask,
    uint32                 IgnoreActorId,
    FPhysicsSweepResult&   OutResult
) const
{
    OutResult = FPhysicsSweepResult();
    if (!Scene || ObjectTypeMask == 0 || MaxDist <= 0.0f)
    {
        return false;
    }

    FVector SweepDir = Dir;
    if (SweepDir.IsNearlyZero())
    {
        return false;
    }
    SweepDir.Normalize();

    PxGeometryHolder Geometry;
    PxQuat ShapeAxisRotation(PxIdentity);
    if (!BuildPxSweepGeometry(Shape, Geometry, ShapeAxisRotation))
    {
        return false;
    }

    struct FObjectTypeSweepFilter : PxQueryFilterCallback
    {
        uint32 IgnoreActorId  = 0;
        PxU32  ObjectTypeMask = 0;

        FObjectTypeSweepFilter(uint32 InIgnoreActorId, PxU32 InMask) : IgnoreActorId(InIgnoreActorId)
                                                                     , ObjectTypeMask(InMask)
        {
        }

        PxQueryHitType::Enum preFilter(
            const PxFilterData&,
            const PxShape* Shape,
            const PxRigidActor* Actor,
            PxHitFlags&
        ) override
        {
            const uint32 ShapeActorId = GetActorIdFromShape(Shape);
            const uint32 BodyActorId  = GetActorIdFromActor(Actor);
            if (IgnoreActorId != 0 && (ShapeActorId == IgnoreActorId || BodyActorId == IgnoreActorId))
            {
                return PxQueryHitType::eNONE;
            }

            if (Shape)
            {
                if (Shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE))
                {
                    return PxQueryHitType::eNONE;
                }

                const PxFilterData ShapeData      = Shape->getQueryFilterData();
                const PxU32        ShapeObjectBit = 1u << GetPhysicsFilterObjectType(ShapeData.word0);
                if ((ShapeObjectBit & ObjectTypeMask) == 0)
                {
                    return PxQueryHitType::eNONE;
                }
            }

            return PxQueryHitType::eBLOCK;
        }

        PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
        {
            return PxQueryHitType::eBLOCK;
        }
    };

    FObjectTypeSweepFilter FilterCallback(IgnoreActorId, ObjectTypeMask);

    PxSweepBuffer Hit;
    PxQueryFilterData FilterData;
    FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;

    const FQuat NormalizedRotation = Rotation.GetNormalized();
    PxTransform StartPose(ToPxVec3(Start), ToPxQuat(NormalizedRotation) * ShapeAxisRotation);

    PxSceneReadLock ReadLock(*Scene);
    const bool bStatus = Scene->sweep(
        Geometry.any(),
        StartPose,
        ToPxVec3(SweepDir),
        MaxDist,
        Hit,
        PxHitFlag::eDEFAULT | PxHitFlag::eMTD,
        FilterData,
        &FilterCallback
    );

    if (!bStatus || !Hit.hasBlock)
    {
        return false;
    }

    const PxSweepHit& Block = Hit.block;
    OutResult.bBlockingHit      = true;
    OutResult.bStartPenetrating = Block.distance <= 0.0f;
    OutResult.Distance          = (std::max)(0.0f, Block.distance);
    OutResult.PenetrationDepth  = OutResult.bStartPenetrating ? (std::max)(0.0f, -Block.distance) : 0.0f;
    OutResult.Location          = ToFVector(Block.position);
    OutResult.ImpactPoint       = OutResult.Location;
    OutResult.ImpactNormal      = ToFVector(Block.normal);
    OutResult.Normal            = OutResult.ImpactNormal;

    if (OutResult.Normal.IsNearlyZero())
    {
        OutResult.Normal       = SweepDir * -1.0f;
        OutResult.ImpactNormal = OutResult.Normal;
    }

    if (Block.shape)
    {
        OutResult.HitComponentId = GetComponentIdFromShape(Block.shape);
        OutResult.HitActorId     = GetActorIdFromShape(Block.shape);
        OutResult.HitGeneration  = GetComponentGenerationFromShape(Block.shape);
    }
    if (OutResult.HitActorId == 0 && Block.actor)
    {
        OutResult.HitActorId = GetActorIdFromActor(Block.actor);
    }

    return OutResult.bBlockingHit;
}

bool FPhysXPhysicsScene::Raycast(
    const FVector&    Start,
    const FVector&    Dir,
    float             MaxDist,
    FHitResult&       OutHit,
    ECollisionChannel TraceChannel,
    const AActor*     IgnoreActor
)
{
    OutHit                     = FHitResult();
    const uint32 IgnoreActorId = IgnoreActor ? IgnoreActor->GetUUID() : 0;

    FPhysicsRaycastResult PhysicsResult;
    if (!SubmitRaycastQuery_GameThread(false, Start, Dir, MaxDist, TraceChannel, 0, IgnoreActorId, PhysicsResult))
    {
        return false;
    }

    return ResolveRaycastResult_GameThread(PhysicsResult, OutHit);
}

bool FPhysXPhysicsScene::RaycastByObjectTypes(
    const FVector& Start,
    const FVector& Dir,
    float          MaxDist,
    FHitResult&    OutHit,
    uint32         ObjectTypeMask,
    const AActor*  IgnoreActor
)
{
    OutHit                     = FHitResult();
    const uint32 IgnoreActorId = IgnoreActor ? IgnoreActor->GetUUID() : 0;

    FPhysicsRaycastResult PhysicsResult;
    if (!SubmitRaycastQuery_GameThread(true, Start, Dir, MaxDist, ECollisionChannel::WorldStatic, ObjectTypeMask, IgnoreActorId, PhysicsResult))
    {
        return false;
    }

    return ResolveRaycastResult_GameThread(PhysicsResult, OutHit);
}

bool FPhysXPhysicsScene::RaycastPhysicsByObjectTypes(
    const FVector&         Start,
    const FVector&         Dir,
    float                  MaxDist,
    FPhysicsRaycastResult& OutResult,
    uint32                 ObjectTypeMask,
    const AActor*          IgnoreActor
)
{
    const uint32 IgnoreActorId = IgnoreActor ? IgnoreActor->GetUUID() : 0;
    return SubmitRaycastQuery_GameThread(
        true,
        Start,
        Dir,
        MaxDist,
        ECollisionChannel::WorldStatic,
        ObjectTypeMask,
        IgnoreActorId,
        OutResult);
}

bool FPhysXPhysicsScene::Sweep(
    const FVector&         Start,
    const FVector&         End,
    const FQuat&           Rotation,
    const FCollisionShape& Shape,
    FHitResult&            OutHit,
    ECollisionChannel      TraceChannel,
    const AActor*          IgnoreActor
)
{
    OutHit = FHitResult();

    const FVector Delta   = End - Start;
    const float   MaxDist = Delta.Length();
    if (MaxDist <= 1.e-6f)
    {
        return false;
    }

    FVector SweepDir = Delta / MaxDist;
    const uint32 IgnoreActorId = IgnoreActor ? IgnoreActor->GetUUID() : 0;

    FPhysicsSweepResult PhysicsResult;
    if (!SubmitSweepQuery_GameThread(false, Start, SweepDir, MaxDist, Rotation, Shape, TraceChannel, 0, IgnoreActorId, PhysicsResult))
    {
        return false;
    }

    return ResolveSweepResult_GameThread(PhysicsResult, OutHit);
}

bool FPhysXPhysicsScene::SweepByObjectTypes(
    const FVector&         Start,
    const FVector&         End,
    const FQuat&           Rotation,
    const FCollisionShape& Shape,
    FHitResult&            OutHit,
    uint32                 ObjectTypeMask,
    const AActor*          IgnoreActor
)
{
    OutHit = FHitResult();

    const FVector Delta   = End - Start;
    const float   MaxDist = Delta.Length();
    if (MaxDist <= 1.e-6f || ObjectTypeMask == 0)
    {
        return false;
    }

    FVector SweepDir = Delta / MaxDist;
    const uint32 IgnoreActorId = IgnoreActor ? IgnoreActor->GetUUID() : 0;

    FPhysicsSweepResult PhysicsResult;
    if (!SubmitSweepQuery_GameThread(true, Start, SweepDir, MaxDist, Rotation, Shape, ECollisionChannel::WorldStatic, ObjectTypeMask, IgnoreActorId, PhysicsResult))
    {
        return false;
    }

    return ResolveSweepResult_GameThread(PhysicsResult, OutHit);
}
