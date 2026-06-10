#include "Physics/PhysXConversion.h"

#include <PxPhysicsAPI.h>

using namespace physx;

PxVec3 ToPxVec3(const FVector& V)
{
    return PxVec3(V.X, V.Y, V.Z);
}

PxQuat ToPxQuat(const FQuat& Q)
{
    return PxQuat(Q.X, Q.Y, Q.Z, Q.W);
}

PxTransform ToPxTransform(const FTransform& T)
{
    return PxTransform(ToPxVec3(T.Location), ToPxQuat(T.Rotation));
}

FVector ToFVector(const PxVec3& V)
{
    return FVector(V.x, V.y, V.z);
}

FQuat ToFQuat(const PxQuat& Q)
{
    return FQuat(Q.x, Q.y, Q.z, Q.w);
}

FTransform ToFTransform(const PxTransform& T)
{
    return FTransform(ToFVector(T.p), ToFQuat(T.q), FVector::OneVector);
}