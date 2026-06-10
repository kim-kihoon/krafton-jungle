#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

struct FWireLine
{
	FVector Start;
	FVector End;
};

namespace CollisionWireUtils
{
	void BuildBoxLines(TArray<FWireLine>& Lines, const FVector& Center, const FVector& HalfExtents, const FQuat& Rotation);
	void BuildSphereLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius);
	// Engine/visual capsule: long axis +Z (UCapsuleComponent convention).
	void BuildCapsuleLinesZ(TArray<FWireLine>& Lines, const FVector& Center, float Radius, float HalfHeight, const FQuat& Rotation);
	// PhysX capsule: long axis +X in the shape's local frame (after any local rotation).
	void BuildCapsuleLinesX(TArray<FWireLine>& Lines, const FVector& Center, float Radius, float HalfHeight, const FQuat& Rotation);
}
