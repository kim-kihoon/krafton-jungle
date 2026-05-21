#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequencePlayer.h"

#include "UAnimSingleNodeInstance.generated.h"

UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
public:
    GENERATED_BODY(UAnimSingleNodeInstance, UAnimInstance)

    void SetSequence(UAnimationSequenceBase* InSequence) { SequencePlayer.SetSequence(InSequence); }
    UAnimationSequenceBase* GetSequence() const { return SequencePlayer.GetSequence(); }

    bool PlayAnimationByName(const FName& AnimationName, bool bLoop) override;
    bool BlendToAnimationByName(
        const FName& AnimationName,
        bool bLoop,
        float BlendTime,
        EAnimBlendEaseOption EaseOption) override;

    void Play() { SequencePlayer.Play(); }
    void Pause() { SequencePlayer.Pause(); }
    void Stop() { SequencePlayer.Stop(); }

    void SetLooping(bool bInLooping) { SequencePlayer.SetLooping(bInLooping); }
    bool IsLooping() const { return SequencePlayer.IsLooping(); }

    void SetPlayRate(float InPlayRate) { SequencePlayer.SetPlayRate(InPlayRate); }
    float GetPlayRate() const { return SequencePlayer.GetPlayRate(); }
    void SetReversePlay(bool bInReversePlay) override { SequencePlayer.SetReversePlay(bInReversePlay); }
    bool IsReversePlaying() const override { return SequencePlayer.IsReversePlaying(); }

    void SetCurrentTime(float InCurrentTime) { SequencePlayer.SetCurrentTime(InCurrentTime); }
    float GetCurrentTime() const { return SequencePlayer.GetCurrentTime(); }

    bool IsPlaying() const { return SequencePlayer.IsPlaying(); }
    bool IsPaused() const { return SequencePlayer.IsPaused(); }

    const TArray<FMatrix>& GetCurrentLocalPose() const { return SequencePlayer.GetCurrentLocalPose(); }

protected:
    // root graph를 사용하지 않는 단일 sequence player 전용 내부 초기화
    void InitializeAnimationNodes() override;
    // root graph 대신 단일 sequence player만 평가하는 특수 AnimInstance
    void UpdateAnimationGraph(float DeltaSeconds) override;

private:
    FAnimSequencePlayer SequencePlayer;
};
