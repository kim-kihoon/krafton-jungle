#pragma once

#include "Animation/Skeleton/SkeletonTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Physics/PhysicsTypes.h"
#include "Serialization/Archive.h"

enum class EPhysicsAssetShapeType : uint8
{
    Box,
    Sphere,
    Capsule
};

struct FPhysicsAssetShapeSetup
{
    EPhysicsAssetShapeType Type = EPhysicsAssetShapeType::Box;

    FTransform LocalTransform;

    FVector BoxHalfExtent = FVector(0.8f, 0.8f, 0.8f);

    float SphereRadius = 0.8f;

    float CapsuleRadius     = 0.4f;
    float CapsuleHalfHeight = 1.2f;

    friend FArchive& operator<<(FArchive& Ar, FPhysicsAssetShapeSetup& Setup)
    {
        uint8 TypeRaw = static_cast<uint8>(Setup.Type);
        Ar << TypeRaw;
        Setup.Type = static_cast<EPhysicsAssetShapeType>(TypeRaw);

        Ar << Setup.LocalTransform;
        Ar << Setup.BoxHalfExtent;
        Ar << Setup.SphereRadius;
        Ar << Setup.CapsuleRadius;
        Ar << Setup.CapsuleHalfHeight;
        return Ar;
    }
};

struct FPhysicsAssetBodySetup
{
    FName BoneName = FName::None;

    FTransform BodyLocalFrame;

    TArray<FPhysicsAssetShapeSetup> Shapes;

    float   Mass                    = 1.0f;
    FVector CenterOfMassLocalOffset = FVector::ZeroVector;

    float LinearDamping  = 0.0f;
    float AngularDamping = 0.0f;

    float MaxAngularVelocity = 100.0f;

    int32 PositionSolverIterationCount = 8;
    int32 VelocitySolverIterationCount = 2;

    bool bEnableCCD     = false;
    bool bEnableGravity = true;

    bool bLockLinearX  = false;
    bool bLockLinearY  = false;
    bool bLockLinearZ  = false;
    bool bLockAngularX = false;
    bool bLockAngularY = false;
    bool bLockAngularZ = false;

    friend FArchive& operator<<(FArchive& Ar, FPhysicsAssetBodySetup& Setup)
    {
        Ar << Setup.BoneName;
        Ar << Setup.BodyLocalFrame;
        Ar << Setup.Shapes;
        Ar << Setup.Mass;
        Ar << Setup.CenterOfMassLocalOffset;
        Ar << Setup.LinearDamping;
        Ar << Setup.AngularDamping;
        Ar << Setup.MaxAngularVelocity;
        Ar << Setup.PositionSolverIterationCount;
        Ar << Setup.VelocitySolverIterationCount;
        Ar << Setup.bEnableCCD;
        Ar << Setup.bEnableGravity;
        Ar << Setup.bLockLinearX;
        Ar << Setup.bLockLinearY;
        Ar << Setup.bLockLinearZ;
        Ar << Setup.bLockAngularX;
        Ar << Setup.bLockAngularY;
        Ar << Setup.bLockAngularZ;
        return Ar;
    }
};

struct FPhysicsAssetConstraintSetup
{
    FName ParentBoneName = FName::None;
    FName ChildBoneName  = FName::None;

    FTransform ParentLocalFrame;
    FTransform ChildLocalFrame;

    FConstraintLimitDesc Limits;

    bool bDisableCollisionBetweenBodies = true;

    friend FArchive& operator<<(FArchive& Ar, FPhysicsAssetConstraintSetup& Setup)
    {
        Ar << Setup.ParentBoneName;
        Ar << Setup.ChildBoneName;
        Ar << Setup.ParentLocalFrame;
        Ar << Setup.ChildLocalFrame;

        uint8 LinearX = static_cast<uint8>(Setup.Limits.LinearX);
        uint8 LinearY = static_cast<uint8>(Setup.Limits.LinearY);
        uint8 LinearZ = static_cast<uint8>(Setup.Limits.LinearZ);
        uint8 Twist   = static_cast<uint8>(Setup.Limits.Twist);
        uint8 Swing1  = static_cast<uint8>(Setup.Limits.Swing1);
        uint8 Swing2  = static_cast<uint8>(Setup.Limits.Swing2);

        Ar << LinearX;
        Ar << LinearY;
        Ar << LinearZ;
        Ar << Twist;
        Ar << Swing1;
        Ar << Swing2;

        if (Ar.IsLoading())
        {
            Setup.Limits.LinearX = static_cast<EConstraintMotion>(LinearX);
            Setup.Limits.LinearY = static_cast<EConstraintMotion>(LinearY);
            Setup.Limits.LinearZ = static_cast<EConstraintMotion>(LinearZ);
            Setup.Limits.Twist   = static_cast<EConstraintMotion>(Twist);
            Setup.Limits.Swing1  = static_cast<EConstraintMotion>(Swing1);
            Setup.Limits.Swing2  = static_cast<EConstraintMotion>(Swing2);
        }

        Ar << Setup.Limits.TwistLimitMinDegrees;
        Ar << Setup.Limits.TwistLimitMaxDegrees;
        Ar << Setup.Limits.Swing1LimitDegrees;
        Ar << Setup.Limits.Swing2LimitDegrees;
        Ar << Setup.Limits.bEnableProjection;
        Ar << Setup.bDisableCollisionBetweenBodies;
        return Ar;
    }
};
