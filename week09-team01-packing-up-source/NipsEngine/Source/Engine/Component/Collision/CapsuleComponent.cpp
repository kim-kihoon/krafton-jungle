#include "CapsuleComponent.h"

#include <algorithm>
#include <cmath>

#include "Core/PropertyTypes.h"
#include "Engine/Serialization/Archive.h"

DEFINE_CLASS(UCapsuleComponent, UShapeComponent)
REGISTER_FACTORY(UCapsuleComponent)

UCapsuleComponent::UCapsuleComponent()
{
	 CollisionType = ECollisionType::Capsule;
}

void UCapsuleComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UShapeComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "Capsule Half Height", EPropertyType::Float, &CapsuleHalfHeight, 0.0f, FLT_MAX, 0.05f });
    OutProps.push_back({ "Capsule Radius", EPropertyType::Float, &CapsuleRadius, 0.0f, FLT_MAX, 0.05f });
}

void UCapsuleComponent::PostEditProperty(const char* PropertyName)
{
    UShapeComponent::PostEditProperty(PropertyName);

    CapsuleHalfHeight = std::max(0.0f, CapsuleHalfHeight);
    CapsuleRadius = std::max(0.0f, CapsuleRadius);
    CapsuleHalfHeight = std::max(CapsuleHalfHeight, CapsuleRadius);

    UpdateBodySetup();
}

void UCapsuleComponent::Serialize(FArchive& Ar)
{
    UShapeComponent::Serialize(Ar);
    Ar << "CapsuleHalfHeight" << CapsuleHalfHeight;
    Ar << "CapsuleRadius" << CapsuleRadius;
}

void UCapsuleComponent::UpdateWorldAABB() const
{
    const FVector Scale = GetWorldScale();
    const float RadiusScale = std::max(std::fabs(Scale.X), std::fabs(Scale.Y));
    const float HeightScale = std::fabs(Scale.Z);
    const float SafeHalfHeight = std::fabs(CapsuleHalfHeight) * HeightScale;
    const float SafeRadius = std::fabs(CapsuleRadius) * RadiusScale;

    const FVector LocalExtent(SafeRadius, SafeRadius, SafeHalfHeight);
    const FAABB LocalAABB(-LocalExtent, LocalExtent);
    FMatrix ShapeWorldMatrix = GetWorldMatrix().GetRotationMatrix();
    ShapeWorldMatrix.SetOrigin(GetWorldLocation());
    WorldAABB = FAABB::TransformAABB(LocalAABB, ShapeWorldMatrix);
}

// RayCasting 전용 AABB
bool UCapsuleComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
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

void UCapsuleComponent::SetCapsuleSize(float InCapsuleHalfHeight, float InCapsuleRadius)
{
    CapsuleHalfHeight = std::max(0.0f, InCapsuleHalfHeight);
    CapsuleRadius = std::max(0.0f, InCapsuleRadius);
    CapsuleHalfHeight = std::max(CapsuleHalfHeight, CapsuleRadius);
    UpdateBodySetup();
}
