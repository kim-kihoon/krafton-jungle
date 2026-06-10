// Copyright Epic Games, Inc. All Rights Reserved.
#include "CapsuleComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"
#include "Render/Scene/FScene.h"
#include "Math/MathUtils.h"

#include <cstring>
#include <cmath>
#include <algorithm>

void UCapsuleComponent::SetCapsuleSize(float InRadius, float InHalfHeight)
{
	CapsuleRadius = InRadius;
	CapsuleHalfHeight = (std::max)(InHalfHeight, InRadius);
	LocalExtents = FVector(CapsuleRadius, CapsuleRadius, CapsuleHalfHeight);
	MarkWorldBoundsDirty();
	MarkRenderTransformDirty();
	NotifyPhysicsBodyDirty();
}

float UCapsuleComponent::GetScaledCapsuleRadius() const
{
	FVector Scale = GetWorldScale();
	return CapsuleRadius * std::min(Scale.X, Scale.Y);
}

float UCapsuleComponent::GetScaledCapsuleHalfHeight() const
{
	FVector Scale = GetWorldScale();
	return CapsuleHalfHeight * Scale.Z;
}

namespace
{
	void AddWireCircle(FScene& Scene, const FVector& Center, const FVector& AxisA, const FVector& AxisB, float Radius, int32 Segments, const FColor& Color)
	{
		if (Radius <= 0.0f || Segments < 3)
		{
			return;
		}

		const float Step = 2.0f * FMath::Pi / static_cast<float>(Segments);
		FVector Prev = Center + AxisA * Radius;

		for (int32 Index = 1; Index <= Segments; ++Index)
		{
			const float Angle = Step * static_cast<float>(Index);
			const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
			Scene.AddDebugLine(Prev, Next, Color);
			Prev = Next;
		}
	}

	void AddWireHalfCircle(FScene& Scene, const FVector& Center, const FVector& AxisA, const FVector& AxisB, float Radius, int32 Segments, float StartAngle, const FColor& Color)
	{
		if (Radius <= 0.0f || Segments < 3)
		{
			return;
		}

		const float Step = FMath::Pi / static_cast<float>(Segments);
		FVector Prev = Center + (AxisA * cosf(StartAngle) + AxisB * sinf(StartAngle)) * Radius;

		for (int32 Index = 1; Index <= Segments; ++Index)
		{
			const float Angle = StartAngle + Step * static_cast<float>(Index);
			const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
			Scene.AddDebugLine(Prev, Next, Color);
			Prev = Next;
		}
	}
}

void UCapsuleComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	const FVector Center = GetWorldLocation();
	const float R = GetScaledCapsuleRadius();
	const float HH = GetScaledCapsuleHalfHeight();
	const float CylinderHalf = HH - R;
	constexpr int32 Segments = 24;

	const FColor Color = GetShapeColor();
	const FQuat WorldRot = GetWorldRotation().ToQuaternion().GetNormalized();
	const FVector AxisX = WorldRot.RotateVector(FVector(1.0f, 0.0f, 0.0f));
	const FVector AxisY = WorldRot.RotateVector(FVector(0.0f, 1.0f, 0.0f));
	const FVector AxisZ = WorldRot.RotateVector(FVector(0.0f, 0.0f, 1.0f));
	const FVector TopCenter = Center + AxisZ * CylinderHalf;
	const FVector BotCenter = Center - AxisZ * CylinderHalf;

	// Top and bottom horizontal circles (XY plane)
	AddWireCircle(Scene, TopCenter, AxisX, AxisY, R, Segments, Color);
	AddWireCircle(Scene, BotCenter, AxisX, AxisY, R, Segments, Color);

	// 4 vertical lines connecting top and bottom circles
	Scene.AddDebugLine(TopCenter + AxisX * R, BotCenter + AxisX * R, Color);
	Scene.AddDebugLine(TopCenter - AxisX * R, BotCenter - AxisX * R, Color);
	Scene.AddDebugLine(TopCenter + AxisY * R, BotCenter + AxisY * R, Color);
	Scene.AddDebugLine(TopCenter - AxisY * R, BotCenter - AxisY * R, Color);

	constexpr int32 HalfSegments = 12;

	// Top half-circle caps (upper hemisphere)
	// XZ plane — upward half-circle
	AddWireHalfCircle(Scene, TopCenter, AxisX, AxisZ, R, HalfSegments, 0.0f, Color);
	// YZ plane — upward half-circle
	AddWireHalfCircle(Scene, TopCenter, AxisY, AxisZ, R, HalfSegments, 0.0f, Color);

	// Bottom half-circle caps (lower hemisphere)
	// XZ plane — downward half-circle
	AddWireHalfCircle(Scene, BotCenter, AxisX, AxisZ, R, HalfSegments, FMath::Pi, Color);
	// YZ plane — downward half-circle
	AddWireHalfCircle(Scene, BotCenter, AxisY, AxisZ, R, HalfSegments, FMath::Pi, Color);
}

void UCapsuleComponent::UpdateWorldAABB() const
{
	FVector Center = GetWorldLocation();
	float R = GetScaledCapsuleRadius();
	float HH = GetScaledCapsuleHalfHeight();
	const float CylinderHalf = (std::max)(0.0f, HH - R);
	const FVector Axis = GetWorldRotation().ToQuaternion().GetNormalized().RotateVector(FVector(0.0f, 0.0f, 1.0f));
	const FVector Extent(
		R + std::abs(Axis.X) * CylinderHalf,
		R + std::abs(Axis.Y) * CylinderHalf,
		R + std::abs(Axis.Z) * CylinderHalf
	);
	WorldAABBMinLocation = Center - Extent;
	WorldAABBMaxLocation = Center + Extent;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

void UCapsuleComponent::PostEditProperty(const char* PropertyName)
{
	UShapeComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "CapsuleRadius") == 0 || strcmp(PropertyName, "CapsuleHalfHeight") == 0
		|| strcmp(PropertyName, "Capsule Radius") == 0 || strcmp(PropertyName, "Capsule Half Height") == 0)
	{
		SetCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	}
}
