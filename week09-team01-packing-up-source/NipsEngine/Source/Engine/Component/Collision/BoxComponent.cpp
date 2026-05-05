#include "BoxComponent.h"

#include "Core/PropertyTypes.h"
#include "Engine/Serialization/Archive.h"
#include "Math/Quat.h"
#include "Math/Utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

DEFINE_CLASS(UBoxComponent, UShapeComponent)
REGISTER_FACTORY(UBoxComponent)

UBoxComponent::UBoxComponent()
{
     CollisionType = ECollisionType::Box;
}

void UBoxComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UShapeComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "Box Extent", EPropertyType::Vec3, &BoxExtent });
}

void UBoxComponent::PostEditProperty(const char* PropertyName)
{
    UShapeComponent::PostEditProperty(PropertyName);
    UpdateBodySetup();
}

void UBoxComponent::Serialize(FArchive& Ar)
{
    UShapeComponent::Serialize(Ar);
    Ar << "BoxExtent" << BoxExtent;
}

void UBoxComponent::UpdateWorldAABB() const
{
    const FVector Scale(std::fabs(GetWorldScale().X), std::fabs(GetWorldScale().Y), std::fabs(GetWorldScale().Z));
    const FVector SafeExtent(
        std::fabs(BoxExtent.X) * Scale.X,
        std::fabs(BoxExtent.Y) * Scale.Y,
        std::fabs(BoxExtent.Z) * Scale.Z);
    const FAABB LocalAABB(-SafeExtent, SafeExtent);
    FMatrix ShapeWorldMatrix = GetWorldMatrix().GetRotationMatrix();
    ShapeWorldMatrix.SetOrigin(GetWorldLocation());
    WorldAABB = FAABB::TransformAABB(LocalAABB, ShapeWorldMatrix);
}

// RayCasting 전용 OBB
bool UBoxComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    const FVector Scale(std::fabs(GetWorldScale().X), std::fabs(GetWorldScale().Y), std::fabs(GetWorldScale().Z));
    const FVector SafeExtent(
        std::fabs(BoxExtent.X) * Scale.X,
        std::fabs(BoxExtent.Y) * Scale.Y,
        std::fabs(BoxExtent.Z) * Scale.Z);
    const FQuat WorldRotation(GetWorldMatrix().GetRotationMatrix());
    const FQuat InverseRotation = WorldRotation.GetNormalized().Inverse();
    const FVector LocalOrigin = InverseRotation * (Ray.Origin - GetWorldLocation());
    const FVector LocalDirection = InverseRotation * Ray.Direction;

    float TMin = 0.0f;
    float TMax = std::numeric_limits<float>::max();

    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const float Origin = LocalOrigin[Axis];
        const float Direction = LocalDirection[Axis];
        const float Min = -SafeExtent[Axis];
        const float Max = SafeExtent[Axis];

        if (std::fabs(Direction) < MathUtil::Epsilon)
        {
            if (Origin < Min || Origin > Max)
            {
                return false;
            }
            continue;
        }

        const float InvDirection = 1.0f / Direction;
        float T1 = (Min - Origin) * InvDirection;
        float T2 = (Max - Origin) * InvDirection;
        if (T1 > T2)
        {
            std::swap(T1, T2);
        }

        TMin = std::max(TMin, T1);
        TMax = std::min(TMax, T2);
        if (TMin > TMax)
        {
            return false;
        }
    }

    const float HitT = TMin >= 0.0f ? TMin : TMax;
    if (HitT < 0.0f)
    {
        return false;
    }

    OutHitResult.HitComponent = this;
    OutHitResult.Distance = HitT;
    OutHitResult.Location = Ray.Origin + (Ray.Direction * HitT);
    OutHitResult.bHit = true;
    return true;
}

void UBoxComponent::SetBoxExtent(const FVector& InBoxExtent)
{
    BoxExtent = InBoxExtent;
    UpdateBodySetup();
}
