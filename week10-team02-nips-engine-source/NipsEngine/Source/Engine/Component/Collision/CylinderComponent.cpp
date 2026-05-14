#include "CylinderComponent.h"

#include "Core/PropertyTypes.h"
#include "Engine/Serialization/Archive.h"
#include "Math/Quat.h"
#include "Math/Utils.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

DEFINE_CLASS(UCylinderComponent, UShapeComponent)
REGISTER_FACTORY(UCylinderComponent)

UCylinderComponent::UCylinderComponent()
{
	CollisionType = ECollisionType::Cylinder;
}

void UCylinderComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UShapeComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Cylinder Half Height", EPropertyType::Float, &CylinderHalfHeight, 0.0f, FLT_MAX, 0.05f });
	OutProps.push_back({ "Cylinder Radius", EPropertyType::Float, &CylinderRadius, 0.0f, FLT_MAX, 0.05f });
}

void UCylinderComponent::PostEditProperty(const char* PropertyName)
{
	UShapeComponent::PostEditProperty(PropertyName);

	CylinderHalfHeight = std::max(0.0f, CylinderHalfHeight);
	CylinderRadius = std::max(0.0f, CylinderRadius);

	UpdateBodySetup();
}

void UCylinderComponent::Serialize(FArchive& Ar)
{
	UShapeComponent::Serialize(Ar);
	Ar << "CylinderHalfHeight" << CylinderHalfHeight;
	Ar << "CylinderRadius" << CylinderRadius;
}

void UCylinderComponent::UpdateWorldAABB() const
{
	const FVector Scale = GetWorldScale();
	const float RadiusScale = std::max(std::fabs(Scale.X), std::fabs(Scale.Y));
	const float HeightScale = std::fabs(Scale.Z);
	const float SafeHalfHeight = std::fabs(CylinderHalfHeight) * HeightScale;
	const float SafeRadius = std::fabs(CylinderRadius) * RadiusScale;
	const FVector LocalExtent(SafeRadius, SafeRadius, SafeHalfHeight);
	const FAABB LocalAABB(-LocalExtent, LocalExtent);

	FMatrix ShapeWorldMatrix = GetWorldMatrix().GetRotationMatrix();
	ShapeWorldMatrix.SetOrigin(GetWorldLocation());
	WorldAABB = FAABB::TransformAABB(LocalAABB, ShapeWorldMatrix);
}

bool UCylinderComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	const FVector Scale = GetWorldScale();
	const float RadiusScale = std::max(std::fabs(Scale.X), std::fabs(Scale.Y));
	const float HeightScale = std::fabs(Scale.Z);
	const float SafeHalfHeight = std::fabs(CylinderHalfHeight) * HeightScale;
	const float SafeRadius = std::fabs(CylinderRadius) * RadiusScale;
	if (SafeHalfHeight <= MathUtil::Epsilon || SafeRadius <= MathUtil::Epsilon)
	{
		return false;
	}

	const FQuat WorldRotation(GetWorldMatrix().GetRotationMatrix());
	const FQuat InverseRotation = WorldRotation.GetNormalized().Inverse();
	const FVector LocalOrigin = InverseRotation * (Ray.Origin - GetWorldLocation());
	const FVector LocalDirection = InverseRotation * Ray.Direction;

	float BestT = std::numeric_limits<float>::max();
	const float RadiusSq = SafeRadius * SafeRadius;
	const float A = LocalDirection.X * LocalDirection.X + LocalDirection.Y * LocalDirection.Y;
	const float B = 2.0f * (LocalOrigin.X * LocalDirection.X + LocalOrigin.Y * LocalDirection.Y);
	const float C = LocalOrigin.X * LocalOrigin.X + LocalOrigin.Y * LocalOrigin.Y - RadiusSq;

	if (std::fabs(A) > MathUtil::Epsilon)
	{
		const float Discriminant = B * B - 4.0f * A * C;
		if (Discriminant >= 0.0f)
		{
			const float SqrtD = std::sqrt(Discriminant);
			const float InvDenom = 0.5f / A;
			const float T0 = (-B - SqrtD) * InvDenom;
			const float T1 = (-B + SqrtD) * InvDenom;

			auto TestSideT = [&](float T)
			{
				if (T < 0.0f || T >= BestT)
				{
					return;
				}

				const float Z = LocalOrigin.Z + LocalDirection.Z * T;
				if (Z >= -SafeHalfHeight && Z <= SafeHalfHeight)
				{
					BestT = T;
				}
			};

			TestSideT(T0);
			TestSideT(T1);
		}
	}

	if (std::fabs(LocalDirection.Z) > MathUtil::Epsilon)
	{
		auto TestCapZ = [&](float CapZ)
		{
			const float T = (CapZ - LocalOrigin.Z) / LocalDirection.Z;
			if (T < 0.0f || T >= BestT)
			{
				return;
			}

			const FVector Point = LocalOrigin + LocalDirection * T;
			if ((Point.X * Point.X + Point.Y * Point.Y) <= RadiusSq)
			{
				BestT = T;
			}
		};

		TestCapZ(-SafeHalfHeight);
		TestCapZ(SafeHalfHeight);
	}

	if (BestT == std::numeric_limits<float>::max())
	{
		return false;
	}

	OutHitResult.HitComponent = this;
	OutHitResult.Distance = BestT;
	OutHitResult.Location = Ray.Origin + (Ray.Direction * BestT);
	OutHitResult.bHit = true;
	return true;
}

void UCylinderComponent::SetCylinderSize(float InCylinderHalfHeight, float InCylinderRadius)
{
	CylinderHalfHeight = std::max(0.0f, InCylinderHalfHeight);
	CylinderRadius = std::max(0.0f, InCylinderRadius);
	UpdateBodySetup();
}
