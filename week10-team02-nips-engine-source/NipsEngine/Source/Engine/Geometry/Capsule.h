#pragma once

#include "Geometry/Edge.h"
#include "Math/Vector.h"

struct FCapsule
{
	FEdge Segment;
	float Radius;

	FCapsule();
	FCapsule(const FEdge& InSegment, float InRadius);
	FCapsule(const FVector& InStart, const FVector& InEnd, float InRadius);

	void Reset();
	bool IsValid() const;

	FVector ClosestPoint(const FVector& Point) const;
	bool IntersectsSphere(const FVector& Center, float SphereRadius, FVector* OutClosestPoint = nullptr) const;
	bool Intersects(const FCapsule& Other, FVector* OutClosestA = nullptr, FVector* OutClosestB = nullptr) const;
};
