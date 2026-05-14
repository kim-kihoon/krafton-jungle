#pragma once

#include "Game/UI/GameUISystem.h"

class EndingPanel
{
public:
	static void Render(EUIRenderMode Mode);
	static void Tick(float DeltaTime);
	static void Reset();
	static bool ShouldShowTheEnd();
	static bool ShouldShowCredits();
	static bool ShouldShowBlackFade();
	static bool ShouldShowEndingVisual();
	static float GetFadeAlpha();
	static float GetCreditAlpha();
	static float GetBlackFadeAlpha();
	static const char* GetImagePath();

private:
	static void StartDialogue();
	static void StartFinalScene();

	enum class EEndingSequencePhase
	{
		Dialogue,
		FadeToBlack,
		Credits,
		CreditFadeOut,
		Final
	};

	static float FadeTimer;
	static float SequenceTimer;
	static bool  bShowTheEnd;
	static bool  bExitCalled;
	static EEndingSequencePhase SequencePhase;
	static EEndingType ActiveEndingType;
};
