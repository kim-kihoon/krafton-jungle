#include "SphereComponent.h"

#include "Core/PropertyTypes.h"
#include "Engine/Serialization/Archive.h"

#include <algorithm>
#include <cmath>

DEFINE_CLASS(USphereComponent, UShapeComponent)
REGISTER_FACTORY(USphereComponent)

USphereComponent::USphereComponent()
{
     CollisionType = ECollisionType::Sphere;
}

void USphereComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UShapeComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "Sphere Radius", EPropertyType::Float, &SphereRadius, 0.0f, FLT_MAX, 0.05f });
}

void USphereComponent::PostEditProperty(const char* PropertyName)
{
    UShapeComponent::PostEditProperty(PropertyName);
    if (SphereRadius < 0.0f)
    {
        SphereRadius = 0.0f;
    }
    UpdateBodySetup();
}

void USphereComponent::Serialize(FArchive& Ar)
{
    UShapeComponent::Serialize(Ar);
    Ar << "SphereRadius" << SphereRadius;
}

// RayCasting 전용 AABB
void USphereComponent::UpdateWorldAABB() const
{
    const FVector Scale = GetWorldScale();
    const float MaxScale = std::max({ std::fabs(Scale.X), std::fabs(Scale.Y), std::fabs(Scale.Z) });
    const float SafeRadius = std::fabs(SphereRadius) * MaxScale;
    const FVector WorldExtent(SafeRadius, SafeRadius, SafeRadius);
    const FVector WorldLocation = GetWorldLocation();
    WorldAABB = FAABB(WorldLocation - WorldExtent, WorldLocation + WorldExtent);
}

bool USphereComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    float HitT = 0.0f;
    if (!GetWorldAABB().IntersectRay(Ray, HitT))
    {
        return false;
    }

    OutHitResult.HitComponent = this;
    OutHitResult.Distance = HitT;
    OutHitResult.Location = Ray.Origin + (Ray.Direction * HitT);
    OutHitResult.bHit = true;
    return true;
}

void USphereComponent::SetSphereRadius(float InSphereRadius)
{
    SphereRadius = std::max(0.0f, InSphereRadius);
    UpdateBodySetup();
}
