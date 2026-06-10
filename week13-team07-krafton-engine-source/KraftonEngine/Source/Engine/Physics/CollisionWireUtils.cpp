#include "Physics/CollisionWireUtils.h"

#include "Math/MathUtils.h"

#include <cmath>

namespace
{
	void AddWireCircle(TArray<FWireLine>& Lines, const FVector& Center,
		const FVector& AxisA, const FVector& AxisB, float Radius, int32 Segments)
	{
		if (Radius <= 0.0f || Segments < 3)
		{
			return;
		}

		const float Step = 2.0f * FMath::Pi / static_cast<float>(Segments);
		FVector Prev = Center + AxisA * Radius;

		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Angle = Step * static_cast<float>(i);
			FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
			Lines.push_back({ Prev, Next });
			Prev = Next;
		}
	}

	void AddWireHalfCircle(TArray<FWireLine>& Lines, const FVector& Center,
		const FVector& AxisA, const FVector& AxisB, float Radius, int32 Segments, float StartAngle)
	{
		if (Radius <= 0.0f || Segments < 3)
		{
			return;
		}

		const float Step = FMath::Pi / static_cast<float>(Segments);
		FVector Prev = Center + (AxisA * cosf(StartAngle) + AxisB * sinf(StartAngle)) * Radius;

		for (int32 i = 1; i <= Segments; ++i)
		{
			const float Angle = StartAngle + Step * static_cast<float>(i);
			FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
			Lines.push_back({ Prev, Next });
			Prev = Next;
		}
	}

	void BuildCapsuleLinesInternal(TArray<FWireLine>& Lines, const FVector& Center, float Radius, float HalfHeight,
		const FQuat& Rotation, int32 LongAxisIndex)
	{
		const float CylinderHalf = (std::max)(0.0f, HalfHeight - Radius);
		constexpr int32 Segments = 24;
		constexpr int32 HalfSegments = 12;

		FVector AxisLong = FVector::ZeroVector;
		FVector AxisA = FVector::ZeroVector;
		FVector AxisB = FVector::ZeroVector;
		switch (LongAxisIndex)
		{
		case 0:
			AxisLong = FVector(1.0f, 0.0f, 0.0f);
			AxisA = FVector(0.0f, 1.0f, 0.0f);
			AxisB = FVector(0.0f, 0.0f, 1.0f);
			break;
		case 1:
			AxisLong = FVector(0.0f, 1.0f, 0.0f);
			AxisA = FVector(0.0f, 0.0f, 1.0f);
			AxisB = FVector(1.0f, 0.0f, 0.0f);
			break;
		default:
			AxisLong = FVector(0.0f, 0.0f, 1.0f);
			AxisA = FVector(1.0f, 0.0f, 0.0f);
			AxisB = FVector(0.0f, 1.0f, 0.0f);
			break;
		}

		const FVector WorldLong = Rotation.RotateVector(AxisLong);
		const FVector WorldA = Rotation.RotateVector(AxisA);
		const FVector WorldB = Rotation.RotateVector(AxisB);

		const FVector TopCenter = Center + WorldLong * CylinderHalf;
		const FVector BotCenter = Center - WorldLong * CylinderHalf;

		AddWireCircle(Lines, TopCenter, WorldA, WorldB, Radius, Segments);
		AddWireCircle(Lines, BotCenter, WorldA, WorldB, Radius, Segments);

		Lines.push_back({ TopCenter + WorldA * Radius, BotCenter + WorldA * Radius });
		Lines.push_back({ TopCenter - WorldA * Radius, BotCenter - WorldA * Radius });
		Lines.push_back({ TopCenter + WorldB * Radius, BotCenter + WorldB * Radius });
		Lines.push_back({ TopCenter - WorldB * Radius, BotCenter - WorldB * Radius });

		AddWireHalfCircle(Lines, TopCenter, WorldA, WorldLong, Radius, HalfSegments, 0.0f);
		AddWireHalfCircle(Lines, TopCenter, WorldB, WorldLong, Radius, HalfSegments, 0.0f);
		AddWireHalfCircle(Lines, BotCenter, WorldA, WorldLong, Radius, HalfSegments, FMath::Pi);
		AddWireHalfCircle(Lines, BotCenter, WorldB, WorldLong, Radius, HalfSegments, FMath::Pi);
	}
}

namespace CollisionWireUtils
{
	void BuildBoxLines(TArray<FWireLine>& Lines, const FVector& Center, const FVector& HalfExtents, const FQuat& Rotation)
	{
		FVector Corners[8];
		for (int32 i = 0; i < 8; ++i)
		{
			FVector LocalOffset(
				(i & 1) ? HalfExtents.X : -HalfExtents.X,
				(i & 2) ? HalfExtents.Y : -HalfExtents.Y,
				(i & 4) ? HalfExtents.Z : -HalfExtents.Z);
			Corners[i] = Center + Rotation.RotateVector(LocalOffset);
		}

		Lines.push_back({ Corners[0], Corners[1] });
		Lines.push_back({ Corners[1], Corners[3] });
		Lines.push_back({ Corners[3], Corners[2] });
		Lines.push_back({ Corners[2], Corners[0] });
		Lines.push_back({ Corners[4], Corners[5] });
		Lines.push_back({ Corners[5], Corners[7] });
		Lines.push_back({ Corners[7], Corners[6] });
		Lines.push_back({ Corners[6], Corners[4] });
		Lines.push_back({ Corners[0], Corners[4] });
		Lines.push_back({ Corners[1], Corners[5] });
		Lines.push_back({ Corners[2], Corners[6] });
		Lines.push_back({ Corners[3], Corners[7] });
	}

	void BuildSphereLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius)
	{
		constexpr int32 Segments = 24;
		AddWireCircle(Lines, Center, FVector(1, 0, 0), FVector(0, 1, 0), Radius, Segments);
		AddWireCircle(Lines, Center, FVector(1, 0, 0), FVector(0, 0, 1), Radius, Segments);
		AddWireCircle(Lines, Center, FVector(0, 1, 0), FVector(0, 0, 1), Radius, Segments);
	}

	void BuildCapsuleLinesZ(TArray<FWireLine>& Lines, const FVector& Center, float Radius, float HalfHeight, const FQuat& Rotation)
	{
		BuildCapsuleLinesInternal(Lines, Center, Radius, HalfHeight, Rotation, 2);
	}

	void BuildCapsuleLinesX(TArray<FWireLine>& Lines, const FVector& Center, float Radius, float HalfHeight, const FQuat& Rotation)
	{
		BuildCapsuleLinesInternal(Lines, Center, Radius, HalfHeight, Rotation, 0);
	}
}
