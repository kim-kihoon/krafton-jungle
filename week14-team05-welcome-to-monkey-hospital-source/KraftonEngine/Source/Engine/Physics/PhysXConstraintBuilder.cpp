#include "Physics/PhysXConstraintBuilder.h"
#include "Physics/PhysXConversion.h"

#include <PxPhysicsAPI.h>

namespace
{
    constexpr float Pi = 3.14159265358979323846f;

    float DegreesToRadians(float Degrees)
    {
        return Degrees * Pi / 180.0f;
    }

    physx::PxD6Motion::Enum ToPxD6Motion(EConstraintMotion Motion)
    {
        switch (Motion)
        {
        case EConstraintMotion::Free:
            return physx::PxD6Motion::eFREE;
        case EConstraintMotion::Limited:
            return physx::PxD6Motion::eLIMITED;
        case EConstraintMotion::Locked:
        default:
            return physx::PxD6Motion::eLOCKED;
        }
    }
}

physx::PxJoint* FPhysXConstraintBuilder::CreateD6Joint(
    physx::PxPhysics* Physics,
    physx::PxRigidActor* ParentActor,
    physx::PxRigidActor* ChildActor,
    const FConstraintCreationDesc& Desc
)
{
    if (!Physics || !ParentActor || !ChildActor)
    {
        return nullptr;
    }

    physx::PxD6Joint* Joint = physx::PxD6JointCreate(
        *Physics,
        ParentActor,
        ToPxTransform(Desc.ParentLocalFrame),
        ChildActor,
        ToPxTransform(Desc.ChildLocalFrame)
    );

    if (!Joint)
    {
        return nullptr;
    }

    const FConstraintLimitDesc& L = Desc.Limits;

    Joint->setMotion(physx::PxD6Axis::eX, ToPxD6Motion(L.LinearX));
    Joint->setMotion(physx::PxD6Axis::eY, ToPxD6Motion(L.LinearY));
    Joint->setMotion(physx::PxD6Axis::eZ, ToPxD6Motion(L.LinearZ));

    Joint->setMotion(physx::PxD6Axis::eTWIST, ToPxD6Motion(L.Twist));
    Joint->setMotion(physx::PxD6Axis::eSWING1, ToPxD6Motion(L.Swing1));
    Joint->setMotion(physx::PxD6Axis::eSWING2, ToPxD6Motion(L.Swing2));

    Joint->setTwistLimit(
        physx::PxJointAngularLimitPair(
            DegreesToRadians(L.TwistLimitMinDegrees),
            DegreesToRadians(L.TwistLimitMaxDegrees)
        )
    );

    Joint->setSwingLimit(
        physx::PxJointLimitCone(
            DegreesToRadians(L.Swing1LimitDegrees),
            DegreesToRadians(L.Swing2LimitDegrees)
        )
    );

    Joint->setConstraintFlag(
        physx::PxConstraintFlag::eCOLLISION_ENABLED,
        !Desc.bDisableCollisionBetweenBodies
    );

    Joint->setConstraintFlag(
        physx::PxConstraintFlag::ePROJECTION,
        L.bEnableProjection
    );

    return Joint;
}
