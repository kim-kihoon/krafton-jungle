#include <directxmath.h>
#include "UPrimitive.h"

void UPrimitive::SetLocation(const FVector& InLocation)
{
    Location = InLocation;
}

FVector UPrimitive::GetLocation() const
{
    return Location;
}

void UPrimitive::SetVelocity(const FVector& InVelocity)
{
    Velocity = InVelocity;
}

FVector UPrimitive::GetVelocity() const
{
    return Velocity;
}

void UPrimitive::SetMass(float InMass)
{
    Mass = InMass;
}

float UPrimitive::GetMass() const
{
    return Mass;
}

void UPrimitive::SetColliding(bool bInColliding)
{
    bIsColliding = bInColliding;
}

bool UPrimitive::GetColliding() const
{
    return bIsColliding;
}


