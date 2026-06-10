#pragma once

#include "Core/Types/CoreTypes.h"

namespace physx
{
	class PxShape;
	class PxQuat;
}

class UPrimitiveComponent;

namespace PhysXShapeUtils
{
	// word2 reserved bit — vehicle suspension raycasts treat shapes with this bit as drivable ground.
	// Does not affect KraftonFilterShader (only channels 0..ActiveCount-1 are checked).
	inline constexpr uint32 VehicleDrivableQueryBit = (1u << 16);

	// PxCapsuleGeometry long axis is +X; UCapsuleComponent / debug wire use +Z.
	physx::PxQuat GetCapsuleAxisCorrectionQuat();

	void SetupFilterData(physx::PxShape* Shape, UPrimitiveComponent* Comp);
	bool ShouldUseTriggerShape(UPrimitiveComponent* Comp);
	void FinalizeShape(physx::PxShape* Shape, UPrimitiveComponent* Comp);
}
