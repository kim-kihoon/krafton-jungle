#pragma once

#include "Animation/AnimStateMachineTypes.h"
#include "Animation/AnimationSequenceBase.h"
#include "Core/Containers/Array.h"
#include "Math/Matrix.h"

class UAnimInstance;

class FAnimSequencePlayer
{
public:
    void Initialize(UAnimInstance* InOwnerAnimInstance);

    void SetSequence(UAnimationSequenceBase* InSequence);
    UAnimationSequenceBase* GetSequence() const { return Sequence; }

    bool PlayAnimationByName(const FName& AnimationName, bool bLoop);
    bool BlendToAnimationByName(
        const FName& AnimationName,
        bool bLoop,
        float BlendTime,
        EAnimBlendEaseOption EaseOption);

    void Play();
    void Pause();
    void Stop();
    void Tick(float DeltaSeconds);

    void SetLooping(bool bInLooping) { bLooping = bInLooping; }   
    bool IsLooping() const { return bLooping; }

    void SetPlayRate(float InPlayRate);
    float GetPlayRate() const { return PlayRate; }
    void SetReversePlay(bool bInReversePlay);
    bool IsReversePlaying() const { return bReversePlay; }

    void SetCurrentTime(float InCurrentTime);
    float GetCurrentTime() const { return CurrentTime; }

    bool IsPlaying() const { return bPlaying; }
    bool IsPaused() const { return bPaused; }

    const TArray<FMatrix>& GetCurrentLocalPose() const { return CurrentLocalPose; }

private:
    UAnimationSequenceBase* ResolveAnimationByName(const FName& AnimationName);
    bool AdvanceTime(float& InOutTime, float DeltaSeconds, float PlayLength, bool bInLooping, bool& bOutLooped);
    bool SampleCurrentPose();
    bool SamplePose(UAnimationSequenceBase* AnimationSequence, float Time, TArray<FMatrix>& OutLocalPose) const;
    bool SampleBlendedPose(float Alpha);
    float ApplyBlendEase(float Alpha) const;
    bool ApplyCurrentPoseToSkeletalMesh();
    void FinishBlend();
    float GetEffectivePlayRate() const;

private:
    UAnimInstance* OwnerAnimInstance = nullptr;
    UAnimationSequenceBase* Sequence = nullptr;
    TArray<FMatrix> CurrentLocalPose;
    float CurrentTime = 0.0f;
    float PlayRate = 1.0f;
    bool bReversePlay = false;
    bool bPlaying = false;
    bool bPaused = false;
    bool bLooping = false;

    UAnimationSequenceBase* BlendSourceSequence = nullptr;
    UAnimationSequenceBase* BlendTargetSequence = nullptr;
    TArray<FMatrix> BlendSourcePose;
    TArray<FMatrix> BlendTargetPose;
    float BlendSourceTime = 0.0f;
    float BlendTargetTime = 0.0f;
    float BlendElapsedTime = 0.0f;
    float BlendDuration = 0.0f;
    EAnimBlendEaseOption BlendEaseOption = EAnimBlendEaseOption::Linear;
    bool bBlendSourceLooping = false;
    bool bBlending = false;
};
