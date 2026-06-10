#pragma once

#include "Physics/PhysicsTypes.h"

namespace physx
{
    class PxPhysics;
    class PxRigidActor;
    class PxJoint;
}

class FPhysXConstraintBuilder
{
public:
    static physx::PxJoint* CreateD6Joint(
        physx::PxPhysics*              Physics,
        physx::PxRigidActor*           ParentActor,
        physx::PxRigidActor*           ChildActor,
        const FConstraintCreationDesc& Desc
    );
};