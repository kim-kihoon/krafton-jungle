#pragma once

#include "Object/Reflection/ObjectMacros.h"

// UE: ECollisionTraceFlag — BodySetupCore.h / BodySetupEnums.h
UENUM()
enum class ECollisionTraceFlag : uint8
{
	UseDefault = 0,
	UseSimpleAndComplex = 1,
	UseSimpleAsComplex = 2,
	UseComplexAsSimple = 3,
	MAX
};

// UE: EPhysicsType
UENUM()
enum class EPhysicsType : uint8
{
	Default = 0,
	Kinematic = 1,
	Simulated = 2,
	MAX
};

// UE: EBodyCollisionResponse::Type
UENUM()
enum class EBodyCollisionResponse : uint8
{
	Enabled = 0,
	Disabled = 1,
	MAX
};
