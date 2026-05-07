#include "Engine/Camera/Modifier/LetterBoxCameraModifier.h"
#include "Engine/Math/Utils.h"
#include <algorithm>

DEFINE_CLASS(ULetterBoxCameraModifier, UCameraModifier)

void ULetterBoxCameraModifier::StartLetterBox(float InTargetRatio, float Duration)
{
	TargetRatio = MathUtil::Clamp(InTargetRatio, 0.0f, 0.5f);
	StartRatio = CurrentRatio;

	TransitionTime = std::max(Duration, 0.0f);
	TransitionRemainingTime = TransitionTime;

	if (TransitionTime <= 0.0f)
	{
		CurrentRatio = TargetRatio;
		bTransitioning = false;
	}
	else
	{
		bTransitioning = true;
	}
	
	EnableModifier();
}

void ULetterBoxCameraModifier::SetLetterBox(float InRatio)
{
	CurrentRatio = MathUtil::Clamp(InRatio, 0.0f, 0.5f);

	StartRatio = CurrentRatio;
	TargetRatio = CurrentRatio;

	TransitionTime = 0.0f;
	TransitionRemainingTime = 0.0f;
	bTransitioning = false;

	EnableModifier();
}

void ULetterBoxCameraModifier::ClearLetterBox()
{
	StartRatio = 0.0f;
	TargetRatio = 0.0f;
	CurrentRatio = 0.0f;

	TransitionTime = 0.0f;
	TransitionRemainingTime = 0.0f;
	bTransitioning = false;
}

// Tick마다 호출되며, FCameraOverlaySettings의 값을 덮어씌우고 성공 여부를 반환합니다.
bool ULetterBoxCameraModifier::ModifyOverlay(float DeltaTime, FCameraOverlaySettings& InOutOverlay)
{
	if (bTransitioning)
	{
		TransitionRemainingTime = std::max(TransitionRemainingTime - DeltaTime, 0.0f);

		const float ElapsedTime = TransitionTime - TransitionRemainingTime;
		const float TransitionAlpha = TransitionTime > 0.0f ? MathUtil::Clamp(ElapsedTime / TransitionTime, 0.0f, 1.0f) : 1.0f;

		CurrentRatio = MathUtil::Lerp(StartRatio, TargetRatio, TransitionAlpha);

		if (TransitionRemainingTime <= 0.0f)
		{
			CurrentRatio = TargetRatio;
			bTransitioning = false;
		}
	}

	InOutOverlay.LetterBoxRatio = MathUtil::Clamp(CurrentRatio, 0.0f, 0.5f);

	return InOutOverlay.LetterBoxRatio > 0.0f || bTransitioning;
}
