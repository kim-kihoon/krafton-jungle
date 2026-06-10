#pragma once

#include "Core/Types/CollisionTypes.h"
#include "Core/Types/CoreTypes.h"
#include "Core/Types/EngineTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Physics/PhysicsBodyHandle.h"

#include <memory>

enum class EPhysicsBodyType : uint8
{
    Static,
    Dynamic,
    Kinematic
};

enum class EPhysicsSyncMode : uint8
{
    EngineToPhysics,
    PhysicsToEngine,
    KinematicTarget,
    Manual
};

enum class EPhysicsShapeType : uint8
{
    Box,
    Sphere,
    Capsule,
    Convex,
    TriangleMesh
};

enum class EPhysicsTeleportMode : uint8
{
    None,
    TeleportPhysics
};

// Runtime object(Body/Shape/Constraint) lifecycle 상태. generation 과 함께 stale access
// 와 simulate/callback 중 release 를 막는다.
enum class EPhysicsRuntimeObjectState : uint8
{
    Free,
    PendingCreate,
    Alive,
    PendingDestroy,
    Destroyed
};

// Body 의 도메인 — Debug/Stat 분리, compound vs ragdoll 정책 분기에 사용.
enum class EPhysicsBodyDomain : uint8
{
    ActorComponent,
    Ragdoll,
    Vehicle,
    ClothCollision,
    EditorPreview
};

enum class EConstraintMotion : uint8
{
    Free,
    Limited,
    Locked
};

enum class EPhysicsGameplayOverlapOwnership : uint8
{
    None,
    PrimaryOwner,
    QueryProxyOwner,
    NonOwningReactionBody
};

enum class EPhysicsCollisionRole : uint8
{
    None,
    CharacterLocomotionProxy,
    CharacterQueryProxy,
    CharacterMeshPrimitive,
    PartialReactionBody,
    FullRagdollBody,
    TriggerVolume,
    WorldStatic,
    WorldDynamic
};

// Thread boundary 의 기본 식별 단위. UObject pointer 대신 모든 command/event/query 가 이 key 를
// 사용한다. ComponentGeneration 은 destroy/recreate 후 도착한 stale command/event 를 폐기하는 데 쓴다.
struct FPhysicsObjectKey
{
    uint32 ActorId             = 0;
    uint32 ComponentId         = 0; // = UObject UUID
    uint32 ComponentGeneration = 0;

    FName              BoneName = FName::None;
    EPhysicsBodyDomain Domain   = EPhysicsBodyDomain::ActorComponent;
};

// Ragdoll bone lookup index 용 합성 key. ComponentId(상위 32bit) + BoneName 해시(하위 32bit).
// 같은 component 내 서로 다른 bone 이 32bit 에서 충돌할 확률은 무시 가능.
inline uint64 MakeComponentBoneKey(uint32 ComponentId, const FName& BoneName)
{
    const uint64 NameHash = static_cast<uint32>(FName::Hash {}(BoneName));
    return (static_cast<uint64>(ComponentId) << 32) | NameHash;
}

constexpr uint32 PhysicsFilter_ObjectTypeMask        = 0x000000FFu;
constexpr uint32 PhysicsFilter_QueryOnly             = 1u << 8;
constexpr uint32 PhysicsFilter_PhysicsOnly           = 1u << 9;
constexpr uint32 PhysicsFilter_QueryAndPhysics       = 1u << 10;
constexpr uint32 PhysicsFilter_IsTrigger             = 1u << 11;
constexpr uint32 PhysicsFilter_GenerateHitEvents     = 1u << 12;
constexpr uint32 PhysicsFilter_GenerateOverlapEvents = 1u << 13;
constexpr uint32 PhysicsFilter_EnableCCD             = 1u << 14;
constexpr uint32 PhysicsFilter_ComponentPrimitive    = 1u << 15;
constexpr uint32 PhysicsFilter_SameActorSelfIgnore   = 1u << 16;
constexpr uint32 PhysicsFilter_IndependentRagdoll    = 1u << 17;
constexpr uint32 PhysicsFilter_PartialRagdoll        = 1u << 18;
constexpr uint32 PhysicsFilter_SuppressSameActorPrimitivePairs = 1u << 19;
constexpr uint32 PhysicsFilter_OverlapOwnerPrimary   = 1u << 20;
constexpr uint32 PhysicsFilter_OverlapOwnerQueryProxy = 1u << 21;
constexpr uint32 PhysicsFilter_OverlapNonOwningReaction = 1u << 22;
constexpr uint32 PhysicsFilter_CollisionRoleShift    = 23u;
constexpr uint32 PhysicsFilter_CollisionRoleMask     = 0xFu << PhysicsFilter_CollisionRoleShift;

inline uint32 GetPhysicsFilterObjectType(uint32 PackedWord0)
{
    return PackedWord0 & PhysicsFilter_ObjectTypeMask;
}

inline bool HasPhysicsFilterFlag(uint32 PackedWord0, uint32 Flag)
{
    return (PackedWord0 & Flag) != 0;
}

inline uint32 PackPhysicsCollisionRole(EPhysicsCollisionRole Role)
{
    return (static_cast<uint32>(Role) << PhysicsFilter_CollisionRoleShift) & PhysicsFilter_CollisionRoleMask;
}

inline EPhysicsCollisionRole GetPackedPhysicsCollisionRole(uint32 PackedWord0)
{
    return static_cast<EPhysicsCollisionRole>(
        (PackedWord0 & PhysicsFilter_CollisionRoleMask) >> PhysicsFilter_CollisionRoleShift);
}

inline EPhysicsGameplayOverlapOwnership GetDefaultGameplayOverlapOwnershipForRole(EPhysicsCollisionRole Role)
{
    switch (Role)
    {
    case EPhysicsCollisionRole::CharacterLocomotionProxy:
        return EPhysicsGameplayOverlapOwnership::PrimaryOwner;
    case EPhysicsCollisionRole::CharacterQueryProxy:
        return EPhysicsGameplayOverlapOwnership::QueryProxyOwner;
    case EPhysicsCollisionRole::PartialReactionBody:
        return EPhysicsGameplayOverlapOwnership::NonOwningReactionBody;
    default:
        return EPhysicsGameplayOverlapOwnership::None;
    }
}

inline bool IsGameplayOverlapOwnerRole(EPhysicsCollisionRole Role)
{
    return
        Role == EPhysicsCollisionRole::CharacterLocomotionProxy ||
        Role == EPhysicsCollisionRole::CharacterQueryProxy;
}

struct FPhysicsFilterData
{
    uint32 ObjectType  = 0;
    uint32 BlockMask   = 0;
    uint32 OverlapMask = 0;

    uint32 IgnoreGroup = 0;

    ECollisionEnabled CollisionEnabled       = ECollisionEnabled::NoCollision;
    bool              bIsTrigger             = false;
    bool              bGenerateHitEvents     = false;
    bool              bGenerateOverlapEvents = false;
    bool              bEnableCCD             = false;
    bool              bIsComponentPrimitive  = false;
    bool              bIgnoreSameActor       = false;
    bool              bIsIndependentRagdoll  = false;
    bool              bIsPartialRagdoll      = false;
    bool              bSuppressSameActorPrimitivePairs = false;
    EPhysicsCollisionRole CollisionRole = EPhysicsCollisionRole::None;
    EPhysicsGameplayOverlapOwnership GameplayOverlapOwnership = EPhysicsGameplayOverlapOwnership::None;
};

struct FPhysicsShapeDesc
{
    EPhysicsShapeType Type = EPhysicsShapeType::Box;

    FTransform LocalTransform;

    FVector BoxHalfExtent = FVector(50.0f, 50.0f, 50.0f);

    float SphereRadius = 50.0f;

    float CapsuleRadius     = 50.0f;
    float CapsuleHalfHeight = 100.0f;

    struct FTriangleMeshData
    {
        TArray<FVector> Vertices;
        TArray<uint32> Indices;
    };
    std::shared_ptr<const FTriangleMeshData> TriangleMeshData;

    ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;

    FPhysicsFilterData FilterData;

    uint32 QueryIgnoreGroup = 0;

    bool bIsTrigger = false;
};

struct FPhysicsClothCollisionShape
{
    EPhysicsShapeType Type = EPhysicsShapeType::Box;

    uint32 OwnerActorId = 0;
    uint32 OwnerComponentId = 0;
    uint32 OwnerComponentGeneration = 0;

    FPhysicsBodyHandle Body;
    FPhysicsShapeHandle Shape;

    EPhysicsBodyType BodyType = EPhysicsBodyType::Static;
    FPhysicsFilterData FilterData;

    FTransform PreviousWorldTransform;
    FTransform CurrentWorldTransform;

    FBoundingBox WorldBounds;

    FVector BoxHalfExtent = FVector::ZeroVector;
    float SphereRadius = 0.0f;
    float CapsuleRadius = 0.0f;
    float CapsuleHalfHeight = 0.0f;
};

// Runtime 에서 보관하는 body 의 mutable 속성 묶음. SetMass/SetCOM/SetLock 등이 서로의 값을
// 덮어쓰지 않도록 단일 소스로 들고, RebuildBody 후 ApplyBodyRuntimeProperties 로 재적용한다.
struct FBodyRuntimeProperties
{
    float   Mass                    = 1.0f;
    FVector CenterOfMassLocalOffset = FVector::ZeroVector;

    float LinearDamping  = 0.0f;
    float AngularDamping = 0.0f;

    float MaxAngularVelocity = 100.0f;

    int32 PositionSolverIterationCount = 8;
    int32 VelocitySolverIterationCount = 2;

    bool bEnableCCD     = false;
    bool bEnableGravity = true;

    bool bLockLinearX  = false;
    bool bLockLinearY  = false;
    bool bLockLinearZ  = false;
    bool bLockAngularX = false;
    bool bLockAngularY = false;
    bool bLockAngularZ = false;
};

struct FBodyCreationDesc
{
    FPhysicsBodyHandle  ReservedBody;
    FPhysicsShapeHandle ReservedShape;

    uint32 OwnerActorId             = 0;
    uint32 OwnerComponentId         = 0;
    uint32 OwnerComponentGeneration = 0;

    EPhysicsBodyDomain Domain = EPhysicsBodyDomain::ActorComponent;

    // Skeletal/Ragdoll body면 BoneName을 채웁니다.
    FName BoneName = FName::None;

    EPhysicsBodyType BodyType = EPhysicsBodyType::Static;
    EPhysicsSyncMode SyncMode = EPhysicsSyncMode::EngineToPhysics;

    FTransform WorldTransform;

    TArray<FPhysicsShapeDesc> Shapes;

    float   Mass                    = 1.0f;
    FVector CenterOfMassLocalOffset = FVector::ZeroVector;

    float LinearDamping  = 0.0f;
    float AngularDamping = 0.0f;

    bool bLockLinearX = false;
    bool bLockLinearY = false;
    bool bLockLinearZ = false;
    bool bLockAngularX = false;
    bool bLockAngularY = false;
    bool bLockAngularZ = false;

    // Vehicle/Ragdoll 안정성 파라미터 — dynamic body 생성 시 적용.
    float MaxAngularVelocity           = 100.0f;
    int32 PositionSolverIterationCount = 8;
    int32 VelocitySolverIterationCount = 2;
    bool  bEnableGravity               = true;

    bool bEnableCCD             = false;
    bool bGenerateHitEvents     = false;
    bool bGenerateOverlapEvents = false;
};

enum class EPhysicsCreationResultType : uint8
{
    Body,
    Constraint,
};

struct FPhysicsCreationResult
{
    EPhysicsCreationResultType Type = EPhysicsCreationResultType::Body;
    FPhysicsObjectKey          Owner;

    FPhysicsBodyHandle       Body;
    FPhysicsShapeHandle      Shape;
    FPhysicsConstraintHandle Constraint;

    bool bSuccess = false;
};

struct FPhysicsBodyCreatePayload
{
    FPhysicsBodyHandle  ReservedBody;
    FPhysicsShapeHandle ReservedShape;

    FPhysicsObjectKey BodyOwner;
    FPhysicsObjectKey ShapeOwner;

    EPhysicsBodyType BodyType = EPhysicsBodyType::Static;
    EPhysicsSyncMode SyncMode = EPhysicsSyncMode::EngineToPhysics;

    FTransform WorldTransform;

    TArray<FPhysicsShapeDesc> Shapes;

    float   Mass                    = 1.0f;
    FVector CenterOfMassLocalOffset = FVector::ZeroVector;

    float LinearDamping  = 0.0f;
    float AngularDamping = 0.0f;

    float MaxAngularVelocity           = 100.0f;
    int32 PositionSolverIterationCount = 8;
    int32 VelocitySolverIterationCount = 2;

    bool bLockLinearX  = false;
    bool bLockLinearY  = false;
    bool bLockLinearZ  = false;
    bool bLockAngularX = false;
    bool bLockAngularY = false;
    bool bLockAngularZ = false;

    bool bEnableGravity         = true;
    bool bEnableCCD             = false;
    bool bGenerateHitEvents     = false;
    bool bGenerateOverlapEvents = false;
};

inline FBodyCreationDesc ToBodyCreationDesc(const FPhysicsBodyCreatePayload& Payload)
{
    FBodyCreationDesc Desc;
    Desc.ReservedBody                 = Payload.ReservedBody;
    Desc.ReservedShape                = Payload.ReservedShape;
    Desc.OwnerActorId                 = Payload.BodyOwner.ActorId;
    Desc.OwnerComponentId             = Payload.BodyOwner.ComponentId;
    Desc.OwnerComponentGeneration     = Payload.BodyOwner.ComponentGeneration;
    Desc.Domain                       = Payload.BodyOwner.Domain;
    Desc.BoneName                     = Payload.BodyOwner.BoneName;
    Desc.BodyType                     = Payload.BodyType;
    Desc.SyncMode                     = Payload.SyncMode;
    Desc.WorldTransform               = Payload.WorldTransform;
    Desc.Shapes                       = Payload.Shapes;
    Desc.Mass                         = Payload.Mass;
    Desc.CenterOfMassLocalOffset      = Payload.CenterOfMassLocalOffset;
    Desc.LinearDamping                = Payload.LinearDamping;
    Desc.AngularDamping               = Payload.AngularDamping;
    Desc.MaxAngularVelocity           = Payload.MaxAngularVelocity;
    Desc.PositionSolverIterationCount = Payload.PositionSolverIterationCount;
    Desc.VelocitySolverIterationCount = Payload.VelocitySolverIterationCount;
    Desc.bLockLinearX                 = Payload.bLockLinearX;
    Desc.bLockLinearY                 = Payload.bLockLinearY;
    Desc.bLockLinearZ                 = Payload.bLockLinearZ;
    Desc.bLockAngularX                = Payload.bLockAngularX;
    Desc.bLockAngularY                = Payload.bLockAngularY;
    Desc.bLockAngularZ                = Payload.bLockAngularZ;
    Desc.bEnableGravity               = Payload.bEnableGravity;
    Desc.bEnableCCD                   = Payload.bEnableCCD;
    Desc.bGenerateHitEvents           = Payload.bGenerateHitEvents;
    Desc.bGenerateOverlapEvents       = Payload.bGenerateOverlapEvents;
    return Desc;
}

struct FConstraintLimitDesc
{
    EConstraintMotion LinearX = EConstraintMotion::Locked;
    EConstraintMotion LinearY = EConstraintMotion::Locked;
    EConstraintMotion LinearZ = EConstraintMotion::Locked;

    EConstraintMotion Twist  = EConstraintMotion::Limited;
    EConstraintMotion Swing1 = EConstraintMotion::Limited;
    EConstraintMotion Swing2 = EConstraintMotion::Limited;

    float TwistLimitMinDegrees = -45.0f;
    float TwistLimitMaxDegrees = 45.0f;

    float Swing1LimitDegrees = 30.0f;
    float Swing2LimitDegrees = 30.0f;

    bool bEnableProjection = true;
};

struct FConstraintCreationDesc
{
    FTransform ParentLocalFrame;
    FTransform ChildLocalFrame;

    FConstraintLimitDesc Limits;

    bool bDisableCollisionBetweenBodies = true;
};
