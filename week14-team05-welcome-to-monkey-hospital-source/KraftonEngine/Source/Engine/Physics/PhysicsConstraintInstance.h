#pragma once

#include "Physics/PhysicsTypes.h"

namespace physx
{
    class PxJoint;
}

struct FConstraintInstance
{
    FPhysicsConstraintHandle Handle;

    FPhysicsBodyHandle ParentBody;
    FPhysicsBodyHandle ChildBody;

    physx::PxJoint* Joint = nullptr;

    FTransform ParentLocalFrame;
    FTransform ChildLocalFrame;

    FConstraintLimitDesc Limits;

    bool bDisableCollisionBetweenBodies = true;
};