#include "Animation/AnimInstance.h"

#include "Component/SkeletalMeshComponent.h"
#include "Core/ResourceManager.h"

void UAnimInstance::Initialize(USkeletalMeshComponent* InSkelMeshComponent)
{
    // 같은 component 반복 연결은 사용자 초기화 hook 중복 실행 방지
    if (bAnimationInitialized && SkelMeshComponent == InSkelMeshComponent)
    {
        return;
    }

    // component 재바인딩 시 등록 animation/context는 유지하고 내부 node owner 연결 후 사용자 hook 호출
    SkelMeshComponent = InSkelMeshComponent;
    InitializeAnimationNodes();
    NativeInitializeAnimation();
    bAnimationInitialized = true;
}

void UAnimInstance::NativeInitializeAnimation()
{
}

void UAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
}

void UAnimInstance::TickAnimation(float DeltaSeconds)
{
    NativeUpdateAnimation(DeltaSeconds);
    UpdateAnimationGraph(DeltaSeconds);
}

void UAnimInstance::InitializeAnimationNodes()
{
    if (RootNode)
    {
        RootNode->Initialize(this);
    }
}

void UAnimInstance::UpdateAnimationGraph(float DeltaSeconds)
{
    if (!RootNode)
    {
        return;
    }

    FAnimNodeUpdateContext Context;
    Context.DeltaSeconds = DeltaSeconds;
    Context.OwnerAnimInstance = this;
    Context.Parameters = &AnimParameters;
    RootNode->Update(Context);
}

void UAnimInstance::RegisterAnimation(const FName& AnimationName, UAnimationSequenceBase* Sequence)
{
    if (!AnimationName.IsValid())
    {
        return;
    }

    for (FNamedAnimation& RegisteredAnimation : RegisteredAnimations)
    {
        if (RegisteredAnimation.AnimationName == AnimationName)
        {
            RegisteredAnimation.Sequence = Sequence;
            return;
        }
    }

    FNamedAnimation RegisteredAnimation;
    RegisteredAnimation.AnimationName = AnimationName;
    RegisteredAnimation.Sequence = Sequence;
    RegisteredAnimations.push_back(RegisteredAnimation);
}

bool UAnimInstance::RegisterAnimationPath(const FName& AnimationName, const FString& AnimationPath)
{
    if (!AnimationName.IsValid() || AnimationPath.empty())
    {
        return false;
    }

    UAnimationSequenceBase* Sequence = FResourceManager::Get().LoadAnimationSequence(AnimationPath);
    if (!Sequence)
    {
        return false;
    }

    RegisterAnimation(AnimationName, Sequence);
    return true;
}

UAnimationSequenceBase* UAnimInstance::FindRegisteredAnimation(const FName& AnimationName) const
{
    for (const FNamedAnimation& RegisteredAnimation : RegisteredAnimations)
    {
        if (RegisteredAnimation.AnimationName == AnimationName)
        {
            return RegisteredAnimation.Sequence;
        }
    }

    return nullptr;
}

bool UAnimInstance::PlayAnimationByName(const FName& AnimationName, bool bLoop)
{
    FAnimStateMachineNode* StateMachineNode = GetStateMachine();
    return StateMachineNode ? StateMachineNode->PlayAnimationByName(AnimationName, bLoop) : false;
}

bool UAnimInstance::BlendToAnimationByName(
    const FName& AnimationName,
    bool bLoop,
    float BlendTime,
    EAnimBlendEaseOption EaseOption)
{
    FAnimStateMachineNode* StateMachineNode = GetStateMachine();
    return StateMachineNode
        ? StateMachineNode->BlendToAnimationByName(AnimationName, bLoop, BlendTime, EaseOption)
        : false;
}

void UAnimInstance::SetStateMachineAsset(UAnimStateMachineAsset* InStateMachineAsset)
{
    FAnimStateMachineNode* StateMachineNode = GetOrCreateStateMachineRootNode();
    if (StateMachineNode)
    {
        StateMachineNode->SetStateMachineAsset(InStateMachineAsset);
        if (InStateMachineAsset)
        {
            AnimParameters.EnsureDefaults(InStateMachineAsset->GetParameters());
        }
        StateMachineNode->InitializeStateMachine();
    }
}

UAnimStateMachineAsset* UAnimInstance::GetStateMachineAsset() const
{
    const FAnimStateMachineNode* StateMachineNode = GetStateMachine();
    return StateMachineNode ? StateMachineNode->GetAsset() : nullptr;
}

FAnimStateMachineNode* UAnimInstance::GetStateMachine()
{
    return RootNode ? RootNode->AsStateMachineNode() : nullptr;
}

const FAnimStateMachineNode* UAnimInstance::GetStateMachine() const
{
    return RootNode ? RootNode->AsStateMachineNode() : nullptr;
}

void UAnimInstance::SetAnimBoolParameter(const FName& Name, bool Value)
{
    AnimParameters.SetBool(Name, Value);
}

void UAnimInstance::SetAnimIntParameter(const FName& Name, int32 Value)
{
    AnimParameters.SetInt(Name, Value);
}

void UAnimInstance::SetAnimFloatParameter(const FName& Name, float Value)
{
    AnimParameters.SetFloat(Name, Value);
}

void UAnimInstance::SetAnimVectorParameter(const FName& Name, const FVector& Value)
{
    AnimParameters.SetVector(Name, Value);
}

void UAnimInstance::SetAnimTriggerParameter(const FName& Name)
{
    AnimParameters.SetTrigger(Name);
}

void UAnimInstance::SetLooping(bool bInLooping)
{
    FAnimStateMachineNode* StateMachineNode = GetOrCreateStateMachineRootNode();
    if (StateMachineNode)
    {
        StateMachineNode->SetLooping(bInLooping);
    }
}

bool UAnimInstance::IsLooping() const
{
    const FAnimStateMachineNode* StateMachineNode = GetStateMachine();
    return StateMachineNode ? StateMachineNode->IsLooping() : false;
}

void UAnimInstance::SetReversePlay(bool bInReversePlay)
{
    FAnimStateMachineNode* StateMachineNode = GetOrCreateStateMachineRootNode();
    if (StateMachineNode)
    {
        StateMachineNode->SetReversePlay(bInReversePlay);
    }
}

bool UAnimInstance::IsReversePlaying() const
{
    const FAnimStateMachineNode* StateMachineNode = GetStateMachine();
    return StateMachineNode ? StateMachineNode->IsReversePlaying() : false;
}

FAnimStateMachineNode* UAnimInstance::GetOrCreateStateMachineRootNode()
{
    FAnimStateMachineNode* StateMachineNode = GetStateMachine();
    if (StateMachineNode)
    {
        return StateMachineNode;
    }

    RootNode = std::make_unique<FAnimStateMachineNode>();
    RootNode->Initialize(this);
    return RootNode->AsStateMachineNode();
}

void UAnimInstance::TriggerAnimNotifies(
    UAnimationSequenceBase* AnimationSequence,
    float PreviousTime,
    float CurrentTime,
    float PlayLength,
    bool bLooped,
    bool bForwardPlayback)
{
    if (!AnimationSequence || !SkelMeshComponent || PlayLength <= 0.0f)
    {
        return;
    }

    if (bLooped)
    {
        if (bForwardPlayback)
        {
            TriggerAnimNotifiesInRange(AnimationSequence, PreviousTime, PlayLength);
            TriggerAnimNotifiesInRange(AnimationSequence, 0.0f, CurrentTime);
        }
        else
        {
            TriggerAnimNotifiesInRange(AnimationSequence, PreviousTime, 0.0f);
            TriggerAnimNotifiesInRange(AnimationSequence, PlayLength, CurrentTime);
        }
        return;
    }

    TriggerAnimNotifiesInRange(AnimationSequence, PreviousTime, CurrentTime);
}

void UAnimInstance::TriggerAnimNotifiesInRange(
    UAnimationSequenceBase* AnimationSequence,
    float RangeStart,
    float RangeEnd)
{
    if (!AnimationSequence || !SkelMeshComponent)
    {
        return;
    }

    const bool bForward = RangeStart <= RangeEnd;
    for (const FAnimNotifyEvent& Notify : AnimationSequence->GetNotifies())
    {
        if (!Notify.NotifyName.IsValid())
        {
            continue;
        }

        bool bShouldTrigger = false;
        if (bForward)
        {
            const bool bIncludesPlaybackStart = RangeStart <= 0.0f;
            bShouldTrigger =
                ((Notify.TriggerTime > RangeStart) ||
                 (bIncludesPlaybackStart && Notify.TriggerTime >= 0.0f)) &&
                Notify.TriggerTime <= RangeEnd;
        }
        else
        {
            bShouldTrigger = Notify.TriggerTime < RangeStart && Notify.TriggerTime >= RangeEnd;
        }

        if (bShouldTrigger)
        {
            SkelMeshComponent->HandleAnimNotify(Notify);
        }
    }
}
