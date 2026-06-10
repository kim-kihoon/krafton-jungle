#pragma once

#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

namespace physx
{
    class PxVec3;
    class PxTransform;
    class PxQuat;
}

physx::PxVec3      ToPxVec3(const FVector& V);
physx::PxQuat      ToPxQuat(const FQuat& Q);
physx::PxTransform ToPxTransform(const FTransform& T);

FVector    ToFVector(const physx::PxVec3& V);
FQuat      ToFQuat(const physx::PxQuat& Q);
FTransform ToFTransform(const physx::PxTransform& T);