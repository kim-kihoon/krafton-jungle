#pragma once

#include "Animation/AnimationSequenceBase.h"
#include "Animation/AnimStateMachineNode.h"
#include "Object/Object.h"

#include <memory>

class USkeletalMeshComponent;
class FAnimSequencePlayer;

#include "UAnimInstance.generated.h"

UCLASS()
class UAnimInstance : public UObject
{
public:
    GENERATED_BODY(UAnimInstance, UObject)

    virtual void Initialize(USkeletalMeshComponent* InSkelMeshComponent);
    // 파생 AnimInstance가 상속해서 초기화 로직을 넣는 hook
    // root node owner 연결 같은 내부 초기화는 InitializeAnimationNodes 단계가 담당하므로 직접 건드리지 않음
    virtual void NativeInitializeAnimation();
    // 파생 AnimInstance가 상속해서 매 프레임 상태 변수를 갱신하는 hook
    // AnimGraph/root node 평가는 UpdateAnimationGraph 단계가 담당하므로 override 구현자가 직접 호출하지 않아도 됨
    virtual void NativeUpdateAnimation(float DeltaSeconds);

    virtual void TickAnimation(float DeltaSeconds);

    USkeletalMeshComponent* GetSkelMeshComponent() const { return SkelMeshComponent; }

    void RegisterAnimation(const FName& AnimationName, UAnimationSequenceBase* Sequence);
    bool RegisterAnimationPath(const FName& AnimationName, const FString& AnimationPath);
    UAnimationSequenceBase* FindRegisteredAnimation(const FName& AnimationName) const;
    virtual bool PlayAnimationByName(const FName& AnimationName, bool bLoop);
    virtual bool BlendToAnimationByName(
        const FName& AnimationName,
        bool bLoop,
        float BlendTime,
        EAnimBlendEaseOption EaseOption);

    void SetStateMachineAsset(UAnimStateMachineAsset* InStateMachineAsset);
    UAnimStateMachineAsset* GetStateMachineAsset() const;
    FAnimStateMachineNode* GetStateMachine();
    const FAnimStateMachineNode* GetStateMachine() const;

    void SetAnimBoolParameter(const FName& Name, bool Value);
    void SetAnimIntParameter(const FName& Name, int32 Value);
    void SetAnimFloatParameter(const FName& Name, float Value);
    void SetAnimVectorParameter(const FName& Name, const FVector& Value);
    void SetAnimTriggerParameter(const FName& Name);
    FAnimStateMachineParameterStore& GetAnimParameters() { return AnimParameters; }
    const FAnimStateMachineParameterStore& GetAnimParameters() const { return AnimParameters; }

    void SetLooping(bool bInLooping);
    bool IsLooping() const;
    virtual void SetReversePlay(bool bInReversePlay);
    virtual bool IsReversePlaying() const;

protected:
    friend class FAnimSequencePlayer;

    // 사용자 hook이 아니라 Initialize 내부 runtime node owner 연결 단계
    virtual void InitializeAnimationNodes();
    // 사용자 hook이 아니라 TickAnimation 내부 graph evaluation 단계
    virtual void UpdateAnimationGraph(float DeltaSeconds);

    void TriggerAnimNotifies(
        UAnimationSequenceBase* AnimationSequence,
        float PreviousTime,
        float CurrentTime,
        float PlayLength,
        bool bLooped,
        bool bForwardPlayback);
    void TriggerAnimNotifiesInRange(
        UAnimationSequenceBase* AnimationSequence,
        float RangeStart,
        float RangeEnd);

protected:
    UPROPERTY(Transient, Read)
    USkeletalMeshComponent* SkelMeshComponent = nullptr;

    struct FNamedAnimation
    {
        FName AnimationName;
        UAnimationSequenceBase* Sequence = nullptr;
    };

    TArray<FNamedAnimation> RegisteredAnimations;

    // Root animation node placeholder. Today it can be a state machine node,
    // but the base instance owns it through the generic AnimNode boundary.
    std::unique_ptr<FAnimNodeBase> RootNode;
    FAnimStateMachineParameterStore AnimParameters;

private:
    FAnimStateMachineNode* GetOrCreateStateMachineRootNode();

    bool bAnimationInitialized = false;
};
