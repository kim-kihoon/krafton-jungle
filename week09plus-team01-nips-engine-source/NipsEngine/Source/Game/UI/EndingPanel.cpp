#include "Game/UI/EndingPanel.h"

#include "Game/UI/DialoguePanel.h"
#include "Game/UI/GameUISystem.h"

#include <algorithm>
#include <vector>

float EndingPanel::FadeTimer = 0.f;
float EndingPanel::SequenceTimer = 0.f;
bool EndingPanel::bShowTheEnd = false;
bool EndingPanel::bExitCalled = false;
EndingPanel::EEndingSequencePhase EndingPanel::SequencePhase = EndingPanel::EEndingSequencePhase::Dialogue;
EEndingType EndingPanel::ActiveEndingType = EEndingType::None;

namespace
{
constexpr float BlackFadeDuration = 2.0f;
constexpr float CreditFadeInDuration = 2.0f;
constexpr float CreditHoldDuration = 3.5f;
constexpr float CreditFadeOutDuration = 2.0f;

std::vector<const char*> GetEndingLines(EEndingType Type)
{
	switch (Type)
	{
	case EEndingType::Good:
		return {
			"고생하셨어요.",
			"계속 두고만 있었는데...",
			"이제는 정리해야겠죠.",
			"이건...",
			"(소라를 가만히 귀에 가져다 댄다.)",
			"바다 소리가 나는 소라네요", 
			"바다에 가면 늘 주워오곤 했었는데",
			"..아이가 떠나고 나서는 한번도 바다에 간 적이 없어요",
			"아 여기 쪽지가",
			"....",
			"아이가 마지막으로 남겨준 선물인 것 같아요",
			"제가 슬퍼하지 말라고..",
			"이 안에 바다 소리가 담기듯이 이제는 좋은 기억들만 담아갈 수 있을 것 같아요."
		};
	case EEndingType::Bad:
		return {
			"고생하셨어요.",
			"이 물건들은...",
			"잘 모르겠네요.",
			"정리하셔도 될 것 같아요."
		};
	case EEndingType::Normal:
	case EEndingType::None:
	default:
		return {
			"고생하셨어요.",
			"계속 두고만 있었는데…",
			"이제는 정리해야겠죠."
		};
	}
}
}

void EndingPanel::Reset()
{
	FadeTimer = 0.f;
	SequenceTimer = 0.f;
	bShowTheEnd = false;
	bExitCalled = false;
	SequencePhase = EEndingSequencePhase::Dialogue;
	ActiveEndingType = GameUISystem::Get().GetEndingType();
	if (ActiveEndingType == EEndingType::None)
		ActiveEndingType = EEndingType::Normal;

	StartDialogue();
}

void EndingPanel::StartDialogue()
{
	DialoguePanel::Hide();

	const std::vector<const char*> Lines = GetEndingLines(ActiveEndingType);
	if (Lines.empty())
		return;

	DialoguePanel::Show("", Lines.front());
	for (size_t Index = 1; Index < Lines.size(); ++Index)
		DialoguePanel::Enqueue("", Lines[Index]);
}

void EndingPanel::Tick(float DeltaTime)
{
	const float ClampedDeltaTime = std::max(0.0f, DeltaTime);

	if (DialoguePanel::IsActive())
		return;

	if (SequencePhase == EEndingSequencePhase::Dialogue)
	{
		SequencePhase = EEndingSequencePhase::FadeToBlack;
		SequenceTimer = 0.0f;
	}

	switch (SequencePhase)
	{
	case EEndingSequencePhase::FadeToBlack:
		SequenceTimer += ClampedDeltaTime;
		if (SequenceTimer >= BlackFadeDuration)
		{
			SequencePhase = EEndingSequencePhase::Credits;
			SequenceTimer = 0.0f;
		}
		break;
	case EEndingSequencePhase::Credits:
		SequenceTimer += ClampedDeltaTime;
		if (SequenceTimer >= CreditHoldDuration)
		{
			SequencePhase = EEndingSequencePhase::CreditFadeOut;
			SequenceTimer = 0.0f;
		}
		break;
	case EEndingSequencePhase::CreditFadeOut:
		SequenceTimer += ClampedDeltaTime;
		if (SequenceTimer >= CreditFadeOutDuration)
		{
			StartFinalScene();
		}
		break;
	case EEndingSequencePhase::Final:
		FadeTimer += ClampedDeltaTime;
		break;
	case EEndingSequencePhase::Dialogue:
	default:
		break;
	}
}

void EndingPanel::StartFinalScene()
{
	SequencePhase = EEndingSequencePhase::Final;
	SequenceTimer = 0.0f;
	FadeTimer = 0.0f;
	bShowTheEnd = true;
}

bool EndingPanel::ShouldShowTheEnd()
{
	return bShowTheEnd;
}

bool EndingPanel::ShouldShowCredits()
{
	return SequencePhase == EEndingSequencePhase::Credits || SequencePhase == EEndingSequencePhase::CreditFadeOut;
}

bool EndingPanel::ShouldShowBlackFade()
{
	return SequencePhase == EEndingSequencePhase::FadeToBlack
		|| SequencePhase == EEndingSequencePhase::Credits
		|| SequencePhase == EEndingSequencePhase::CreditFadeOut
		|| SequencePhase == EEndingSequencePhase::Final;
}

bool EndingPanel::ShouldShowEndingVisual()
{
	return SequencePhase == EEndingSequencePhase::Dialogue || SequencePhase == EEndingSequencePhase::FadeToBlack;
}

float EndingPanel::GetFadeAlpha()
{
	constexpr float FadeDuration = 2.0f;
	return std::clamp(FadeTimer / FadeDuration, 0.0f, 1.0f);
}

float EndingPanel::GetCreditAlpha()
{
	if (SequencePhase == EEndingSequencePhase::Credits)
	{
		return std::clamp(SequenceTimer / CreditFadeInDuration, 0.0f, 1.0f);
	}
	if (SequencePhase == EEndingSequencePhase::CreditFadeOut)
	{
		return 1.0f - std::clamp(SequenceTimer / CreditFadeOutDuration, 0.0f, 1.0f);
	}
	return 0.0f;
}

float EndingPanel::GetBlackFadeAlpha()
{
	if (SequencePhase == EEndingSequencePhase::FadeToBlack)
	{
		return std::clamp(SequenceTimer / BlackFadeDuration, 0.0f, 1.0f);
	}
	if (ShouldShowBlackFade())
	{
		return 1.0f;
	}
	return 0.0f;
}

const char* EndingPanel::GetImagePath()
{
	switch (ActiveEndingType)
	{
	case EEndingType::Good:
		return "Asset/Texture/GoodEnding.png";
	case EEndingType::Bad:
		return "Asset/Texture/BadEnding.png";
	case EEndingType::Normal:
	case EEndingType::None:
	default:
		return "Asset/Texture/NormalEnding.png";
	}
}

void EndingPanel::Render(EUIRenderMode Mode)
{
	(void)Mode;
}
