#pragma once

#include "Physics/PhysicsStats.h"
#include "Physics/PhysicsTypes.h"

struct FPhysicsDebugShape
{
    FPhysicsShapeHandle Shape;

    EPhysicsShapeType Type = EPhysicsShapeType::Box;

    FTransform WorldTransform;

    FVector BoxHalfExtent = FVector::ZeroVector;

    float SphereRadius = 0.0f;

    float CapsuleRadius     = 0.0f;
    float CapsuleHalfHeight = 0.0f;
};

struct FPhysicsDebugBody
{
    FPhysicsBodyHandle Body;

    FTransform BodyWorldTransform;

    bool bIsStatic    = false;
    bool bIsDynamic   = false;
    bool bIsKinematic = false;
    bool bIsSleeping  = false;

    uint32 OwnerActorId     = 0;
    uint32 OwnerComponentId = 0;

    FName BoneName = FName::None;

    TArray<FPhysicsDebugShape> Shapes;
};

struct FPhysicsDebugConstraint
{
    FPhysicsConstraintHandle Constraint;

    FPhysicsBodyHandle ParentBody;
    FPhysicsBodyHandle ChildBody;

    FTransform ParentFrameWorld;
    FTransform ChildFrameWorld;

    float TwistLimitMinDegrees = 0.0f;
    float TwistLimitMaxDegrees = 0.0f;
    float Swing1LimitDegrees   = 0.0f;
    float Swing2LimitDegrees   = 0.0f;
};

// Physics step 직후 runtime 내부에서 만들어 publish 하는 불변 스냅샷. Debug Render(C),
// Stat UI(D) 등 외부 소비자는 live PhysX 를 직접 읽지 않고 이 스냅샷만 읽는다 (lock 으로 보호).
struct FPhysicsDebugSnapshot
{
    TArray<FPhysicsDebugBody>       Bodies;
    TArray<FPhysicsDebugConstraint> Constraints;

    FPhysicsStats Stats;

    float  InterpolationAlpha = 0.0f;
    uint64 StepIndex          = 0;
};