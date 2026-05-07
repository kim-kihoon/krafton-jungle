#pragma once

#include "Engine/Camera/CameraModifier.h"
#include "Engine/Camera/PlayerCameraManager.h"
#include "Math/Color.h"

// Animated Letterbox Camera Modifier, CameraComponent의 고정 Letterbox와 별개로 스르륵 내려오는 효과를 구현
class ULetterBoxCameraModifier : public UCameraModifier
{
public:
	DECLARE_CLASS(ULetterBoxCameraModifier, UCameraModifier)

	void StartLetterBox(float InTargetRatio, float Duration);
	void SetLetterBox(float InRatio);
	void ClearLetterBox();

	bool ModifyOverlay(float DeltaTime, FCameraOverlaySettings& InOutOverlay) override;

	bool IsTransitioning() const { return bTransitioning; }

	float GetCurrentRatio() const { return CurrentRatio; }

private:
	float StartRatio = 0.0f;
	float TargetRatio = 0.0f;
	float CurrentRatio = 0.0f;

	float TransitionTime = 0.0f;
	float TransitionRemainingTime = 0.0f;

	bool bTransitioning = false;
};
