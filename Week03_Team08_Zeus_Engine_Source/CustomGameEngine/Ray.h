#pragma once

#include "Math/Vector.h"
#include "Component/PrimitiveComponent.h"

struct Ray
{
	FVector Origin;
	FVector Direction;

	Ray() = default;
	Ray(FVector from, FVector dir);

	bool IntersectsTriangle(const FVector& V0, const FVector& V1, const FVector& V2, float& OutT, const Ray& ray);

	bool IntersectsPlane(const FVector& PlanePoint, const FVector& PlaneNormal, float& OutT) const;

	bool Intersects(UPrimitiveComponent* Comp, float& OutDistance);
};
