#include "Game/Systems/CleaningToolAnimator.h"

#include <algorithm>
#include <cmath>

FCleaningToolAnimator& FCleaningToolAnimator::Get()
{
	static FCleaningToolAnimator Instance;
	return Instance;
}

void FCleaningToolAnimator::Reset()
{
	ActiveToolId.clear();
	CameraLocalOffset = FVector::ZeroVector;
	HoldCameraLocalOffset = FVector::ZeroVector;
	StrokeCameraLocalDirection = FVector(0.0f, 0.0f, 1.0f);
	ElapsedTime = 0.0f;
	BobAmplitude = 0.0f;
	BobSpeed = 0.0f;
	ReturnSpeed = 14.0f;
	bIsUsing = false;
}

void FCleaningToolAnimator::SetActiveTool(const FCleaningToolData& ToolData)
{
	ActiveToolId = ToolData.ToolId;
	HoldCameraLocalOffset = ToolData.HoldCameraLocalOffset;
	StrokeCameraLocalDirection = ToolData.UseStrokeCameraLocalDirection.GetSafeNormal();
	if (StrokeCameraLocalDirection.IsNearlyZero())
	{
		StrokeCameraLocalDirection = FVector(0.0f, 0.0f, 1.0f);
	}
	BobAmplitude = std::max(0.0f, ToolData.UseBobAmplitude);
	BobSpeed = std::max(0.0f, ToolData.UseBobSpeed);
	ReturnSpeed = std::max(0.0f, ToolData.UseReturnSpeed);
}

void FCleaningToolAnimator::BeginUse(const FCleaningToolData& ToolData)
{
	SetActiveTool(ToolData);
	ElapsedTime = 0.0f;
	CameraLocalOffset = FVector::ZeroVector;
	bIsUsing = true;
}

void FCleaningToolAnimator::EndUse()
{
	bIsUsing = false;
}

void FCleaningToolAnimator::Tick(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (bIsUsing)
	{
		ElapsedTime += DeltaTime;
		CameraLocalOffset = StrokeCameraLocalDirection * (std::sin(ElapsedTime * BobSpeed) * BobAmplitude);
		return;
	}

	const float Alpha = std::clamp(DeltaTime * ReturnSpeed, 0.0f, 1.0f);
	CameraLocalOffset.X += (0.0f - CameraLocalOffset.X) * Alpha;
	CameraLocalOffset.Y += (0.0f - CameraLocalOffset.Y) * Alpha;
	CameraLocalOffset.Z += (0.0f - CameraLocalOffset.Z) * Alpha;

	if (std::abs(CameraLocalOffset.X) < 0.001f
		&& std::abs(CameraLocalOffset.Y) < 0.001f
		&& std::abs(CameraLocalOffset.Z) < 0.001f)
	{
		CameraLocalOffset = FVector::ZeroVector;
	}
}

int FCleaningToolAnimator::GetUseStrokeCycleIndex() const
{
	if (BobSpeed <= 0.0f)
	{
		return 0;
	}

	constexpr float Pi = 3.14159265358979323846f;
	return static_cast<int>(std::floor((ElapsedTime * BobSpeed) / (2.0f * Pi)));
}
