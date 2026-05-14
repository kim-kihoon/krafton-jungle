#include "Capsule.h"

#include "Math/Utils.h"

FCapsule::FCapsule()
	: Segment(), Radius(0.0f)
{
}

FCapsule::FCapsule(const FEdge& InSegment, float InRadius)
	: Segment(InSegment), Radius(MathUtil::Abs(InRadius))
{
}

FCapsule::FCapsule(const FVector& InStart, const FVector& InEnd, float InRadius)
	: Segment(InStart, InEnd), Radius(MathUtil::Abs(InRadius))
{
}

void FCapsule::Reset()
{
	Segment = FEdge();
	Radius = 0.0f;
}

bool FCapsule::IsValid() const
{
	return Radius > 0.0f;
}

FVector FCapsule::ClosestPoint(const FVector& Point) const
{
	return Segment.ClosestPoint(Point);
}

bool FCapsule::IntersectsSphere(const FVector& Center, float SphereRadius, FVector* OutClosestPoint) const
{
	const FVector Closest = ClosestPoint(Center);
	if (OutClosestPoint != nullptr)
	{
		*OutClosestPoint = Closest;
	}

	const float RadiusSum = Radius + MathUtil::Abs(SphereRadius);
	return FVector::DistSquared(Center, Closest) <= RadiusSum * RadiusSum;
}

bool FCapsule::Intersects(const FCapsule& Other, FVector* OutClosestA, FVector* OutClosestB) const
{
	FVector ClosestA;
	FVector ClosestB;
	FEdge::ClosestPoints(Segment, Other.Segment, ClosestA, ClosestB);

	if (OutClosestA != nullptr)
	{
		*OutClosestA = ClosestA;
	}
	if (OutClosestB != nullptr)
	{
		*OutClosestB = ClosestB;
	}

	const float RadiusSum = Radius + Other.Radius;
	return FVector::DistSquared(ClosestA, ClosestB) <= RadiusSum * RadiusSum;
}
