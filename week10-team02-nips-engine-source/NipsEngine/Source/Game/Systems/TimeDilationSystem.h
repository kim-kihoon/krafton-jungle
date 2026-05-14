#pragma once

class FTimeDilationSystem
{
public:
	static FTimeDilationSystem& Get();

	void Reset();
	void Tick(float RealDeltaTime);

	void TriggerHitStop(float Duration, float Dilation = 0.0f);
	void StartSlomo(float TargetDilation, float HoldTime, float BlendInTime = 0.0f, float BlendOutTime = 0.0f);
	void StopSlomo();

	float GetGlobalTimeDilation() const { return GlobalTimeDilation; }


	float GetScaledDeltaTime(float DeltaTime) const 
	{ 
		return DeltaTime * GlobalTimeDilation; 
	}



	bool IsHitStopActive() const { return HitStopRemainingTime > 0.0f; }
	bool IsSlomoActive() const { return bSlomoActive; }

private:
	FTimeDilationSystem() = default;

	static float ClampDilation(float Dilation);
	static float ClampTime(float Time);
	static float Lerp(float A, float B, float Alpha);

	void TickHitStop(float RealDeltaTime);
	void TickSlomo(float RealDeltaTime);

	float GlobalTimeDilation = 1.0f;

	float HitStopRemainingTime = 0.0f;
	float HitStopDilation = 0.0f;

	bool bSlomoActive = false;
	float SlomoStartDilation = 1.0f;
	float SlomoTargetDilation = 1.0f;
	float SlomoElapsedTime = 0.0f;
	float SlomoHoldTime = 0.0f;
	float SlomoBlendInTime = 0.0f;
	float SlomoBlendOutTime = 0.0f;
};
