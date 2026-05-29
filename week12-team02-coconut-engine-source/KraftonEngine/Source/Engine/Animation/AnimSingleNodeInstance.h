#pragma once
#include "Animation/AnimInstance.h"
#include "Animation/AnimTypes.h"

class UAnimationAsset;
class UAnimSequence;

class UAnimSingleNodeInstance : public UAnimInstance
{
public:
	DECLARE_CLASS(UAnimSingleNodeInstance, UAnimInstance)

	void SetAnimation(UAnimationAsset* Asset);
	void Play(bool bInLooping);
	void Stop();
	void SetPlayRate(float Rate);

	void Initialize(USkeletalMeshComponent* InOwner, const FString& InScriptPath = "") override;
	void NativeUpdateAnimation(float DeltaSeconds) override;
	void GetCurrentPose(FPoseContext& OutPose) override;

	float    GetCurrentTickTime() const { return CurrentTime; }
	void     SetCurrentTime(float InTime);
	bool     IsPlaying() const { return bPlaying; }
	void     SetLooping(bool bInLooping) { bLooping = bInLooping; }

private:
	UAnimSequence* Sequence = nullptr;
	float    PrevTime = 0.0f;
	float    CurrentTime = 0.0f;
	float    PlayRate = 1.0f;
	bool     bLooping = false;
	bool     bPlaying = false;
};