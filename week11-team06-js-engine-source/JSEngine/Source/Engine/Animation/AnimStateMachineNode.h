#pragma once

#include "Animation/AnimNode.h"
#include "Animation/AnimStateMachineAsset.h"
#include "Animation/AnimStateMachineParameterStore.h"

#include <memory>

class UAnimInstance;
class FAnimSequencePlayer;

class FAnimStateMachineNode : public FAnimNodeBase
{
public:
    FAnimStateMachineNode();
    ~FAnimStateMachineNode() override;

    void Initialize(UAnimInstance* InOwnerAnimInstance) override;
    void Update(const FAnimNodeUpdateContext& Context) override;
    void Reset() override;

    FAnimStateMachineNode* AsStateMachineNode() override { return this; }
    const FAnimStateMachineNode* AsStateMachineNode() const override { return this; }

    void SetStateMachineAsset(UAnimStateMachineAsset* InAsset);
    bool InitializeStateMachine();

    FName GetCurrentState() const { return CurrentState; }
    FName GetPreviousState() const { return PreviousState; }
    float GetStateElapsedTime() const { return StateElapsedTime; }
    UAnimStateMachineAsset* GetAsset() const { return Asset; }

    void SetLooping(bool bInLooping);
    bool IsLooping() const;
    void SetReversePlay(bool bInReversePlay);
    bool IsReversePlaying() const;

    bool PlayAnimationByName(const FName& AnimationName, bool bLoop);
    bool BlendToAnimationByName(
        const FName& AnimationName,
        bool bLoop,
        float BlendTime,
        EAnimBlendEaseOption EaseOption);

private:
    bool EnterState(const FName& NewState, float BlendTime, EAnimBlendEaseOption EaseOption);
    bool PlayStateAnimation(const FAnimStateDesc& State, float BlendTime, EAnimBlendEaseOption EaseOption);
    bool EvaluateTransition(const FAnimTransitionDesc& Transition, FAnimStateMachineParameterStore& Parameters) const;
    bool EvaluateCondition(const FAnimTransitionCondition& Condition, FAnimStateMachineParameterStore& Parameters) const;
    bool HasWarnedMissingParameter(const FName& ParameterName) const;
    void MarkMissingParameterWarned(const FName& ParameterName) const;

private:
    UAnimStateMachineAsset* Asset = nullptr;
    std::unique_ptr<FAnimSequencePlayer> SequencePlayer;
    FName CurrentState;
    FName PreviousState;
    float StateElapsedTime = 0.0f;
    mutable TArray<FName> WarnedMissingParameters;
};
