#pragma once

#include "Physics/PhysicsTypes.h"

namespace physx
{
    class PxShape;
}

struct FShapeInstance
{
    FPhysicsShapeHandle Handle;
    FPhysicsBodyHandle  OwnerBody;

    uint32 SourceActorId             = 0;
    uint32 SourceComponentId         = 0;
    uint32 SourceComponentGeneration = 0;

    FPhysicsShapeDesc                   Desc;

    // debug draw는 이 값을 써야 실제 충돌 형상과 일치한다.
    FTransform EngineLocalTransform;
    FTransform PhysXLocalTransform;

    physx::PxShape*      Shape = nullptr;

    EPhysicsRuntimeObjectState State = EPhysicsRuntimeObjectState::Free;
};