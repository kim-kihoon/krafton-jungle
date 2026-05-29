#include "AnimationStateMachine.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationRuntime.h"
#include "Math/Transform.h"

#include <algorithm>

void UAnimationStateMachine::UpdateAnimationState(float DeltaTime)
{
	PrevStateLocalTime = StateLocalTime;
	StateLocalTime += DeltaTime;

	if (BlendAlpha < 1.0f)
	{
		BlendingPrevStateTime += (DeltaTime * PlayRate);
		BlendAlpha = (BlendDuration > 0.0f)
			? std::min(BlendAlpha + DeltaTime / BlendDuration, 1.0f)
			: 1.0f;
	}

	ProcessState(DeltaTime);
}

void UAnimationStateMachine::GenerateFinalPose(FPoseContext& OutPose, TArray<FAnimNotifyEvent>& OutNotifies) const
{
	if (!CurrentSequence)
		return;

	FPoseContext CurrentPose;
	if (!CurrentSequence->EvaluatePose(StateLocalTime, CurrentPose))
		return;

	if (BlendAlpha >= 1.0f || !PrevSequence)
	{
		OutPose = CurrentPose;
	}
	else
	{
		FPoseContext PrevPose;
		if (!PrevSequence->EvaluatePose(BlendingPrevStateTime, PrevPose) 
			|| PrevPose.BoneLocalTransforms.size() != CurrentPose.BoneLocalTransforms.size())
		{
			OutPose = CurrentPose;
		}
		else
		{
			FAnimationRuntime::BlendTwoPoses(PrevPose, CurrentPose, BlendAlpha, OutPose);
		}
	}

	if (CurrentSequence)
	{
		CurrentSequence->CollectNotifies(PrevStateLocalTime, StateLocalTime, true, PlayRate < 0.0f, OutNotifies);
	}

	if (BlendAlpha < 1.0f && PrevSequence)
	{
		PrevSequence->CollectNotifies(PrevStateLocalTime, StateLocalTime, true, PlayRate < 0.0f, OutNotifies);
	}
}

UAnimSequence* UAnimationStateMachine::GetCurrentSequence() const
{
	return CurrentSequence;
}

float UAnimationStateMachine::GetCurrentStateTime() const
{
	return StateLocalTime;
}
