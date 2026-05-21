#include "Animation/AnimStateMachineNode.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequencePlayer.h"
#include "Core/Logging/Log.h"

namespace
{
const FName NAME_StateElapsedTime("StateElapsedTime");

FName GetStateAnimationPlaybackKey(const FAnimStateDesc& State)
{
    return State.AnimationPath.empty()
        ? State.AnimationName
        : FName(State.AnimationPath);
}

bool CompareFloat(float Left, float Right, EAnimCompareOp Op)
{
    switch (Op)
    {
    case EAnimCompareOp::Equal:
        return Left == Right;
    case EAnimCompareOp::NotEqual:
        return Left != Right;
    case EAnimCompareOp::Greater:
        return Left > Right;
    case EAnimCompareOp::GreaterEqual:
        return Left >= Right;
    case EAnimCompareOp::Less:
        return Left < Right;
    case EAnimCompareOp::LessEqual:
        return Left <= Right;
    default:
        return false;
    }
}

bool CompareInt(int32 Left, int32 Right, EAnimCompareOp Op)
{
    switch (Op)
    {
    case EAnimCompareOp::Equal:
        return Left == Right;
    case EAnimCompareOp::NotEqual:
        return Left != Right;
    case EAnimCompareOp::Greater:
        return Left > Right;
    case EAnimCompareOp::GreaterEqual:
        return Left >= Right;
    case EAnimCompareOp::Less:
        return Left < Right;
    case EAnimCompareOp::LessEqual:
        return Left <= Right;
    default:
        return false;
    }
}

bool CompareBool(bool Left, bool Right, EAnimCompareOp Op)
{
    switch (Op)
    {
    case EAnimCompareOp::Equal:
        return Left == Right;
    case EAnimCompareOp::NotEqual:
        return Left != Right;
    case EAnimCompareOp::IsTrue:
        return Left;
    case EAnimCompareOp::IsFalse:
        return !Left;
    default:
        return false;
    }
}

bool CompareVector(const FVector& Left, const FVector& Right, EAnimCompareOp Op)
{
    switch (Op)
    {
    case EAnimCompareOp::Equal:
        return Left == Right;
    case EAnimCompareOp::NotEqual:
        return Left != Right;
    default:
        return false;
    }
}
} // namespace

FAnimStateMachineNode::FAnimStateMachineNode()
    : SequencePlayer(std::make_unique<FAnimSequencePlayer>())
{
}

FAnimStateMachineNode::~FAnimStateMachineNode() = default;

void FAnimStateMachineNode::Initialize(UAnimInstance* InOwnerAnimInstance)
{
    FAnimNodeBase::Initialize(InOwnerAnimInstance);
    SequencePlayer->Initialize(InOwnerAnimInstance);
}

void FAnimStateMachineNode::SetStateMachineAsset(UAnimStateMachineAsset* InAsset)
{
    Asset = InAsset;
    CurrentState = FName();
    PreviousState = FName();
    StateElapsedTime = 0.0f;
    WarnedMissingParameters.clear();
    SequencePlayer->Stop();
    SequencePlayer->SetSequence(nullptr);

}

bool FAnimStateMachineNode::InitializeStateMachine()
{
    if (!Asset)
    {
        return false;
    }

    FString ValidationMessage;
    if (!Asset->Validate(&ValidationMessage))
    {
        UE_LOG_WARNING("[AnimSM] Runtime initialize failed: %s", ValidationMessage.c_str());
        return false;
    }

    return EnterState(Asset->GetEntryState(), 0.0f, EAnimBlendEaseOption::Linear);
}

void FAnimStateMachineNode::Update(const FAnimNodeUpdateContext& Context)
{
    if (Asset && OwnerAnimInstance && CurrentState.IsValid() && Context.Parameters)
    {
        StateElapsedTime += Context.DeltaSeconds;

        const FAnimStateDesc* CurrentStateDesc = Asset->FindState(CurrentState);
        TArray<const FAnimTransitionDesc*> TransitionsFromCurrent =
            CurrentStateDesc ? Asset->GetTransitionsFrom(CurrentStateDesc->Id) : TArray<const FAnimTransitionDesc*>();
        for (const FAnimTransitionDesc* Transition : TransitionsFromCurrent)
        {
            if (!Transition)
            {
                continue;
            }

            if (EvaluateTransition(*Transition, *Context.Parameters))
            {
                const FAnimStateDesc* NextState = Asset->FindStateById(Transition->ToStateId);
                if (NextState)
                {
                    EnterState(NextState->StateName, Transition->BlendTime, Transition->EaseOption);
                }
                break;
            }
        }
    }

    SequencePlayer->Tick(Context.DeltaSeconds);
}

void FAnimStateMachineNode::Reset()
{
    UAnimStateMachineAsset* ExistingAsset = Asset;
    SetStateMachineAsset(nullptr);

    if (ExistingAsset)
    {
        SetStateMachineAsset(ExistingAsset);
        InitializeStateMachine();
    }
}

bool FAnimStateMachineNode::PlayAnimationByName(const FName& AnimationName, bool bLoop)
{
    return SequencePlayer->PlayAnimationByName(AnimationName, bLoop);
}

bool FAnimStateMachineNode::BlendToAnimationByName(
    const FName& AnimationName,
    bool bLoop,
    float BlendTime,
    EAnimBlendEaseOption EaseOption)
{
    return SequencePlayer->BlendToAnimationByName(AnimationName, bLoop, BlendTime, EaseOption);
}

void FAnimStateMachineNode::SetLooping(bool bInLooping)
{
    SequencePlayer->SetLooping(bInLooping);
}

bool FAnimStateMachineNode::IsLooping() const
{
    return SequencePlayer->IsLooping();
}

void FAnimStateMachineNode::SetReversePlay(bool bInReversePlay)
{
    SequencePlayer->SetReversePlay(bInReversePlay);
}

bool FAnimStateMachineNode::IsReversePlaying() const
{
    return SequencePlayer->IsReversePlaying();
}

bool FAnimStateMachineNode::EnterState(
    const FName& NewState,
    float BlendTime,
    EAnimBlendEaseOption EaseOption)
{
    if (!Asset || !OwnerAnimInstance || !NewState.IsValid() || CurrentState == NewState)
    {
        return false;
    }

    const FAnimStateDesc* State = Asset->FindState(NewState);
    if (!State)
    {
        UE_LOG_WARNING("[AnimSM] Missing state: %s", NewState.ToString().c_str());
        return false;
    }

    const FName OldState = CurrentState;
    PreviousState = CurrentState;
    CurrentState = NewState;
    StateElapsedTime = 0.0f;

    const bool bPlayedAnimation = PlayStateAnimation(*State, BlendTime, EaseOption);

    UE_LOG(
        "[AnimSM] State change: %s -> %s (BlendTime=%.3f)",
        OldState.IsValid() ? OldState.ToString().c_str() : "<None>",
        CurrentState.ToString().c_str(),
        BlendTime);
    return bPlayedAnimation;
}

bool FAnimStateMachineNode::PlayStateAnimation(
    const FAnimStateDesc& State,
    float BlendTime,
    EAnimBlendEaseOption EaseOption)
{
    const FName AnimationKey = GetStateAnimationPlaybackKey(State);
    if (!AnimationKey.IsValid())
    {
        UE_LOG_WARNING("[AnimSM] Missing animation for state: %s", State.StateName.ToString().c_str());
        return false;
    }

    if (!SequencePlayer->BlendToAnimationByName(AnimationKey, State.bLoop, BlendTime, EaseOption))
    {
        UE_LOG_WARNING(
            "[AnimSM] Missing animation mapping: state=%s animation=%s",
            State.StateName.ToString().c_str(),
            AnimationKey.ToString().c_str());
        return false;
    }

    return true;
}

bool FAnimStateMachineNode::EvaluateTransition(
    const FAnimTransitionDesc& Transition,
    FAnimStateMachineParameterStore& Parameters) const
{
    for (const FAnimTransitionCondition& Condition : Transition.Conditions)
    {
        if (!EvaluateCondition(Condition, Parameters))
        {
            return false;
        }
    }

    return !Transition.Conditions.empty();
}

bool FAnimStateMachineNode::EvaluateCondition(
    const FAnimTransitionCondition& Condition,
    FAnimStateMachineParameterStore& Parameters) const
{
    const FAnimStateMachineParameterDesc* Parameter = Asset ? Asset->FindParameter(Condition.ParameterName) : nullptr;
    if (!Parameter)
    {
        if (!HasWarnedMissingParameter(Condition.ParameterName))
        {
            UE_LOG_WARNING("[AnimSM] Missing parameter declaration: %s", Condition.ParameterName.ToString().c_str());
            MarkMissingParameterWarned(Condition.ParameterName);
        }
        return false;
    }

    if (Parameter->Type == EAnimParameterType::Trigger)
    {
        const bool bTriggered = Parameters.ConsumeTrigger(Condition.ParameterName);
        if (!bTriggered && !HasWarnedMissingParameter(Condition.ParameterName))
        {
            UE_LOG_WARNING("[AnimSM] Missing or inactive trigger parameter: %s", Condition.ParameterName.ToString().c_str());
            MarkMissingParameterWarned(Condition.ParameterName);
        }
        return bTriggered;
    }

    if (Parameter->Type == EAnimParameterType::Bool)
    {
        bool Value = false;
        if (!Parameters.GetBool(Condition.ParameterName, Value))
        {
            if (!HasWarnedMissingParameter(Condition.ParameterName))
            {
                UE_LOG_WARNING("[AnimSM] Missing bool parameter: %s", Condition.ParameterName.ToString().c_str());
                MarkMissingParameterWarned(Condition.ParameterName);
            }
            return false;
        }
        return CompareBool(Value, Condition.CompareValue.BoolValue, Condition.CompareOp);
    }

    if (Parameter->Type == EAnimParameterType::Int)
    {
        int32 Value = 0;
        if (!Parameters.GetInt(Condition.ParameterName, Value))
        {
            if (!HasWarnedMissingParameter(Condition.ParameterName))
            {
                UE_LOG_WARNING("[AnimSM] Missing int parameter: %s", Condition.ParameterName.ToString().c_str());
                MarkMissingParameterWarned(Condition.ParameterName);
            }
            return false;
        }
        return CompareInt(Value, Condition.CompareValue.IntValue, Condition.CompareOp);
    }

    if (Parameter->Type == EAnimParameterType::Float)
    {
        if (Condition.ParameterName == NAME_StateElapsedTime)
        {
            return CompareFloat(StateElapsedTime, Condition.CompareValue.FloatValue, Condition.CompareOp);
        }

        float Value = 0.0f;
        if (!Parameters.GetFloat(Condition.ParameterName, Value))
        {
            if (!HasWarnedMissingParameter(Condition.ParameterName))
            {
                UE_LOG_WARNING("[AnimSM] Missing float parameter: %s", Condition.ParameterName.ToString().c_str());
                MarkMissingParameterWarned(Condition.ParameterName);
            }
            return false;
        }
        return CompareFloat(Value, Condition.CompareValue.FloatValue, Condition.CompareOp);
    }

    if (Parameter->Type == EAnimParameterType::Vector)
    {
        FVector Value = FVector::ZeroVector;
        if (!Parameters.GetVector(Condition.ParameterName, Value))
        {
            if (!HasWarnedMissingParameter(Condition.ParameterName))
            {
                UE_LOG_WARNING("[AnimSM] Missing vector parameter: %s", Condition.ParameterName.ToString().c_str());
                MarkMissingParameterWarned(Condition.ParameterName);
            }
            return false;
        }
        return CompareVector(Value, Condition.CompareValue.VectorValue, Condition.CompareOp);
    }

    return false;
}

bool FAnimStateMachineNode::HasWarnedMissingParameter(const FName& ParameterName) const
{
    for (const FName& WarnedParameter : WarnedMissingParameters)
    {
        if (WarnedParameter == ParameterName)
        {
            return true;
        }
    }

    return false;
}

void FAnimStateMachineNode::MarkMissingParameterWarned(const FName& ParameterName) const
{
    if (ParameterName.IsValid())
    {
        WarnedMissingParameters.push_back(ParameterName);
    }
}
