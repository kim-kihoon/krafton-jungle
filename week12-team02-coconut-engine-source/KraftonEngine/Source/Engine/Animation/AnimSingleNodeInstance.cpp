#include "AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Object/ObjectFactory.h"
#include <algorithm>

IMPLEMENT_CLASS(UAnimSingleNodeInstance, UAnimInstance)

void UAnimSingleNodeInstance::Initialize(USkeletalMeshComponent* InOwner, const FString& InScriptPath)
{
	Super::Initialize(InOwner, InScriptPath);
	CurrentTime = 0.0f;
	bPlaying = false;
}

void UAnimSingleNodeInstance::SetAnimation(UAnimationAsset* Asset)
{
	ResetNotifyState();
	Sequence = Cast<UAnimSequence>(Asset);
	CurrentTime = 0.0f;
	PlayRate = 1.0f;
	bPlaying = false;
}

void UAnimSingleNodeInstance::Play(bool bInLooping)
{
	if (!Sequence)
		return;

	bLooping = bInLooping;
	bPlaying = true;
}

void UAnimSingleNodeInstance::Stop()
{
	ResetNotifyState();
	bPlaying = false;
}

void UAnimSingleNodeInstance::SetPlayRate(float Rate)
{
	PlayRate = Rate;
}

void UAnimSingleNodeInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (!bPlaying || !Sequence)
		return;

	PrevTime = CurrentTime;
	CurrentTime += DeltaSeconds * PlayRate;

	float Length = Sequence->GetPlayLength();
	if (bLooping)
	{
		while (CurrentTime >= Length) CurrentTime -= Length;	// 정방향 재생
		while (CurrentTime < 0.0f) CurrentTime += Length;		// 역방향 재생
	}
	else if (PlayRate >= 0.0f && CurrentTime >= Length)	
	{
		CurrentTime = Length;
		bPlaying = false;
	}
	else if (PlayRate < 0.0f && CurrentTime <= 0.0f)
	{
		CurrentTime = 0.0f;
		bPlaying = false;
	}

	LastEvaluatedTime = CurrentTime;
}

void UAnimSingleNodeInstance::SetCurrentTime(float InTime)
{
	float Length = Sequence ? Sequence->GetPlayLength() : 0.0f;
	CurrentTime = (Length > 0.0f) ? std::clamp(InTime, 0.0f, Length) : 0.0f;
}

void UAnimSingleNodeInstance::GetCurrentPose(FPoseContext& OutPose)
{
	if (!Sequence)
		return;

	if (!Sequence->EvaluatePose(CurrentTime, OutPose))
		return;

	TArray<FAnimNotifyEvent> Notifies;
	Sequence->CollectNotifies(PrevTime, CurrentTime, bLooping, PlayRate < 0.0f, Notifies);

	for (const FAnimNotifyEvent& Notify : Notifies)
		RouteNotify(Notify);
}
