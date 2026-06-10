#pragma once

#include "Core/Types/CoreTypes.h"

struct FPhysicsBodyHandle
{
    uint32 Index      = UINT32_MAX;
    uint32 Generation = 0;

    bool IsValid() const
    {
        return Index != UINT32_MAX;
    }

    bool operator==(const FPhysicsBodyHandle& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }

    bool operator!=(const FPhysicsBodyHandle& Other) const
    {
        return !(*this == Other);
    }
};

struct FPhysicsShapeHandle
{
    uint32 Index      = UINT32_MAX;
    uint32 Generation = 0;

    bool IsValid() const
    {
        return Index != UINT32_MAX;
    }

    bool operator==(const FPhysicsShapeHandle& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }

    bool operator!=(const FPhysicsShapeHandle& Other) const
    {
        return !(*this == Other);
    }
};

struct FPhysicsConstraintHandle
{
    uint32 Index      = UINT32_MAX;
    uint32 Generation = 0;

    bool IsValid() const
    {
        return Index != UINT32_MAX;
    }

    bool operator==(const FPhysicsConstraintHandle& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }

    bool operator!=(const FPhysicsConstraintHandle& Other) const
    {
        return !(*this == Other);
    }
};