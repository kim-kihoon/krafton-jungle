#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

class UPrimitiveComponent;

namespace physx
{
	class PxRigidActor;
}

// PhysX 런타임 바디 디버그 시각화용 스냅샷 (에디터/콘솔 — FBodyInstance 기준).
enum class EPhysicsDebugShapeType : uint8
{
	Unknown,
	Box,
	Sphere,
	Capsule,
};

struct FPhysicsShapeDebugInfo
{
	EPhysicsDebugShapeType ShapeType = EPhysicsDebugShapeType::Unknown;

	FVector LocalPosition = FVector::ZeroVector;
	FQuat LocalRotation = FQuat::Identity;

	// Box: half extents (local)
	FVector BoxHalfExtents = FVector::ZeroVector;
	// Sphere
	float SphereRadius = 0.0f;
	// Capsule (PhysX: radius + half height along local X after axis correction)
	float CapsuleRadius = 0.0f;
	float CapsuleHalfHeight = 0.0f;
};

struct FPhysicsBodyDebugInfo
{
	UPrimitiveComponent* OwnerComponent = nullptr;
	physx::PxRigidActor* PhysXActor = nullptr;

	FVector WorldPosition = FVector::ZeroVector;
	FQuat WorldRotation = FQuat::Identity;

	bool bHasBody = false;
	bool bSimulatePhysics = false;
	bool bIsStatic = false;
	bool bIsDynamic = false;
	bool bIsKinematic = false;

	TArray<FPhysicsShapeDebugInfo> Shapes;
};
