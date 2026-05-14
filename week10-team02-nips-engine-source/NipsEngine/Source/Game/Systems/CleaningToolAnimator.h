#pragma once

#include "Game/Systems/CleaningGameTypes.h"
#include "Math/Vector.h"

class FCleaningToolAnimator
{
public:
	static FCleaningToolAnimator& Get();

	void Reset();
	void SetActiveTool(const FCleaningToolData& ToolData);
	void BeginUse(const FCleaningToolData& ToolData);
	void EndUse();
	void Tick(float DeltaTime);

	bool IsUsing() const { return bIsUsing; }
	const FString& GetToolId() const { return ActiveToolId; }
	const FVector& GetCameraLocalOffset() const { return CameraLocalOffset; }
	const FVector& GetHoldCameraLocalOffset() const { return HoldCameraLocalOffset; }
	int GetUseStrokeCycleIndex() const;

private:
	FCleaningToolAnimator() = default;

	FString ActiveToolId;
	FVector CameraLocalOffset = FVector::ZeroVector;
	FVector HoldCameraLocalOffset = FVector::ZeroVector;
	FVector StrokeCameraLocalDirection = FVector(0.0f, 0.0f, 1.0f);
	float ElapsedTime = 0.0f;
	float BobAmplitude = 0.0f;
	float BobSpeed = 0.0f;
	float ReturnSpeed = 14.0f;
	bool bIsUsing = false;
};
