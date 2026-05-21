#include "Animation/AnimSingleNodeInstance.h"

bool UAnimSingleNodeInstance::PlayAnimationByName(const FName& AnimationName, bool bLoop)
{
    return SequencePlayer.PlayAnimationByName(AnimationName, bLoop);
}

bool UAnimSingleNodeInstance::BlendToAnimationByName(
    const FName& AnimationName,
    bool bLoop,
    float BlendTime,
    EAnimBlendEaseOption EaseOption)
{
    return SequencePlayer.BlendToAnimationByName(AnimationName, bLoop, BlendTime, EaseOption);
}

void UAnimSingleNodeInstance::InitializeAnimationNodes()
{
    UAnimInstance::InitializeAnimationNodes();
    SequencePlayer.Initialize(this);
}

void UAnimSingleNodeInstance::UpdateAnimationGraph(float DeltaSeconds)
{
    SequencePlayer.Tick(DeltaSeconds);
}
