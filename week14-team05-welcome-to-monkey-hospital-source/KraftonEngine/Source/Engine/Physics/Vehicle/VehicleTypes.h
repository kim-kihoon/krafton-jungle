#pragma once

#include "Core/Types/CollisionTypes.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Physics/PhysicsTypes.h"

#include <cfloat>

// Backend-neutral vehicle handle. Gameplay, components, World snapshots and IPhysicsScene
// must use this type instead of PhysX-specific names. The PhysX backend may still use
// the same handle internally, but that detail is hidden below the physics adapter layer.
struct FVehicleHandle
{
    uint32 Index      = UINT32_MAX;
    uint32 Generation = 0;

    bool IsValid() const
    {
        return Index != UINT32_MAX && Generation != 0;
    }

    bool operator==(const FVehicleHandle& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }

    bool operator!=(const FVehicleHandle& Other) const
    {
        return !(*this == Other);
    }
};

inline uint64 MakeVehicleHandleKey(FVehicleHandle Vehicle)
{
    return (static_cast<uint64>(Vehicle.Index) << 32) | static_cast<uint64>(Vehicle.Generation);
}

struct FVehicleInputState
{
    float Throttle  = 0.0f;
    float Brake     = 0.0f;
    float Steering  = 0.0f;
    float Handbrake = 0.0f;
};

struct FVehicleWheelDesc
{
    FName   WheelName     = FName::None;
    FName   BoneName      = FName::None;
    FVector LocalPosition = FVector::ZeroVector;

    float Radius = 0.35f;
    float Width  = 0.25f;
    float Mass   = 20.0f;
    float MOI    = 1.225f; // 0.5 * Mass * Radius^2 근사.

    float MaxBrakeTorque     = 1500.0f;
    float MaxHandbrakeTorque = 0.0f;
    float MaxSteerRadians    = 0.0f;

    float SuspensionMaxCompression   = 0.30f;
    float SuspensionMaxDroop         = 0.10f;
    float SuspensionSpringStrength   = 35000.0f;
    float SuspensionSpringDamperRate = 4500.0f;

    uint32 TireType = 0;
};

struct FVehicleDesc
{
    FPhysicsObjectKey Owner;

    FVehicleHandle ReservedVehicle;
    FTransform     WorldTransform;

    FQuat VisualToSimulationRotation = FQuat::Identity;

    FVector ChassisHalfExtents      = FVector(1.25f, 0.6f, 0.35f);
    float   ChassisMass             = 1200.0f;

    FVector ChassisShapeLocalOffset = FVector::ZeroVector;

    FVector ChassisCMOffset         = FVector(0.0f, 0.0f, -0.25f);

    TArray<FVehicleWheelDesc> Wheels;

    float EnginePeakTorque = 500.0f;
    float EngineMaxOmega   = 600.0f;
    float ClutchStrength   = 10.0f;

    bool  bEnableReverseGear       = true;
    float ReverseGearSwitchSpeed   = 0.5f;

    bool bFrontWheelDrive = false;
    bool bRearWheelDrive  = true;

    float TireFriction = 1.5f;

    uint32 DrivableSurfaceMask = ObjectTypeBit(ECollisionChannel::WorldStatic) |
                                 ObjectTypeBit(ECollisionChannel::WorldDynamic) |
                                 ObjectTypeBit(ECollisionChannel::Pawn);

};

struct FVehicleWheelSnapshot
{
    FName      WheelName = FName::None;
    FTransform WorldTransform;

    FVector RestLocalPosition    = FVector::ZeroVector;
    FVector CurrentLocalPosition = FVector::ZeroVector;

    float SteerAngle       = 0.0f;
    float RotationAngle    = 0.0f;
    float SuspensionJounce = 0.0f;

    bool bInAir = true;

    FVector ContactPoint  = FVector::ZeroVector;
    FVector ContactNormal = FVector::UpVector;
};

struct FVehicleSnapshot
{
    FVehicleHandle Vehicle;

    uint32 OwnerActorId             = 0;
    uint32 OwnerComponentId         = 0;
    uint32 OwnerComponentGeneration = 0;

    FTransform ChassisWorldTransform;

    FVector LinearVelocity  = FVector::ZeroVector;
    FVector AngularVelocity = FVector::ZeroVector;

    TArray<FVehicleWheelSnapshot> Wheels;
};
