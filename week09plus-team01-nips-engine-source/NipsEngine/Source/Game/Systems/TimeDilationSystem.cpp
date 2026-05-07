#include "Game/Systems/TimeDilationSystem.h"

#include <algorithm>

namespace
{
	constexpr float MinTimeDilation = 0.0f;
	constexpr float MaxTimeDilation = 4.0f;
}

FTimeDilationSystem& FTimeDilationSystem::Get()
{
	static FTimeDilationSystem Instance;
	return Instance;
}

void FTimeDilationSystem::Reset()
{
	GlobalTimeDilation = 1.0f;

	HitStopRemainingTime = 0.0f;
	HitStopDilation = 0.0f;

	bSlomoActive = false;
	SlomoStartDilation = 1.0f;
	SlomoTargetDilation = 1.0f;
	SlomoElapsedTime = 0.0f;
	SlomoHoldTime = 0.0f;
	SlomoBlendInTime = 0.0f;
	SlomoBlendOutTime = 0.0f;
}

void FTimeDilationSystem::Tick(float RealDeltaTime)
{
	const float ClampedRealDeltaTime = ClampTime(RealDeltaTime);

	if (HitStopRemainingTime > 0.0f)
	{
		TickHitStop(ClampedRealDeltaTime);
		return;
	}

	if (bSlomoActive)
	{
		TickSlomo(ClampedRealDeltaTime);
		return;
	}

	GlobalTimeDilation = 1.0f;
}

void FTimeDilationSystem::TriggerHitStop(float Duration, float Dilation)
{
	HitStopRemainingTime = std::max(HitStopRemainingTime, ClampTime(Duration));
	HitStopDilation = ClampDilation(Dilation);

	if (HitStopRemainingTime > 0.0f)
	{
		GlobalTimeDilation = HitStopDilation;
	}
}

void FTimeDilationSystem::StartSlomo(float TargetDilation, float HoldTime, float BlendInTime, float BlendOutTime)
{
	SlomoStartDilation = IsHitStopActive() ? 1.0f : GlobalTimeDilation;
	SlomoTargetDilation = ClampDilation(TargetDilation);
	SlomoElapsedTime = 0.0f;
	SlomoHoldTime = ClampTime(HoldTime);
	SlomoBlendInTime = ClampTime(BlendInTime);
	SlomoBlendOutTime = ClampTime(BlendOutTime);
	bSlomoActive = SlomoHoldTime > 0.0f || SlomoBlendInTime > 0.0f || SlomoBlendOutTime > 0.0f;

	if (!bSlomoActive)
	{
		GlobalTimeDilation = 1.0f;
		return;
	}

	if (SlomoBlendInTime <= 0.0f)
	{
		GlobalTimeDilation = SlomoTargetDilation;
	}
}

void FTimeDilationSystem::StopSlomo()
{
	bSlomoActive = false;
	SlomoElapsedTime = 0.0f;
	SlomoStartDilation = 1.0f;
	SlomoTargetDilation = 1.0f;
	SlomoHoldTime = 0.0f;
	SlomoBlendInTime = 0.0f;
	SlomoBlendOutTime = 0.0f;
	GlobalTimeDilation = IsHitStopActive() ? HitStopDilation : 1.0f;
}

float FTimeDilationSystem::ClampDilation(float Dilation)
{
	return std::clamp(Dilation, MinTimeDilation, MaxTimeDilation);
}

float FTimeDilationSystem::ClampTime(float Time)
{
	return std::max(0.0f, Time);
}

float FTimeDilationSystem::Lerp(float A, float B, float Alpha)
{
	const float ClampedAlpha = std::clamp(Alpha, 0.0f, 1.0f);
	return A + (B - A) * ClampedAlpha;
}

void FTimeDilationSystem::TickHitStop(float RealDeltaTime)
{
	HitStopRemainingTime = std::max(0.0f, HitStopRemainingTime - RealDeltaTime);
	GlobalTimeDilation = HitStopRemainingTime > 0.0f
		? HitStopDilation
		: (bSlomoActive ? SlomoStartDilation : 1.0f);
}

void FTimeDilationSystem::TickSlomo(float RealDeltaTime)
{
	SlomoElapsedTime += RealDeltaTime;

	const float BlendInEndTime = SlomoBlendInTime;
	const float HoldEndTime = BlendInEndTime + SlomoHoldTime;
	const float BlendOutEndTime = HoldEndTime + SlomoBlendOutTime;

	if (SlomoBlendInTime > 0.0f && SlomoElapsedTime < BlendInEndTime)
	{
		const float Alpha = SlomoElapsedTime / SlomoBlendInTime;

		// EaseInCubic
		const float EaseAlpha = Alpha * Alpha * Alpha;

		// EaseOutCubic
        /*const float P = 1.0f - Alpha;
        const float EaseAlpha = 1.0f - P * P * P;*/

        GlobalTimeDilation = Lerp(SlomoStartDilation, SlomoTargetDilation, EaseAlpha);
		return;
	}

	if (SlomoElapsedTime < HoldEndTime)
	{
		GlobalTimeDilation = SlomoTargetDilation;
		return;
	}

	if (SlomoBlendOutTime > 0.0f && SlomoElapsedTime < BlendOutEndTime)
	{
		const float Alpha = (SlomoElapsedTime - HoldEndTime) / SlomoBlendOutTime;

		// EaseInCubic
        const float EaseAlpha = Alpha * Alpha * Alpha;

		// EaseOutCubic
        /*const float P = 1.0f - Alpha;
        const float EaseAlpha = 1.0f - P * P * P;*/

        GlobalTimeDilation = Lerp(SlomoTargetDilation, SlomoStartDilation, EaseAlpha);
		return;
	}

	bSlomoActive = false;
	GlobalTimeDilation = 1.0f;
}
