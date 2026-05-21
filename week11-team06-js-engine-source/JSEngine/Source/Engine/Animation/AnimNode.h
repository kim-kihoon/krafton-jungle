#pragma once

class FAnimStateMachineNode;
class UAnimInstance;
class FAnimStateMachineParameterStore;

struct FAnimNodeUpdateContext
{
    float DeltaSeconds = 0.0f;
    UAnimInstance* OwnerAnimInstance = nullptr;
    FAnimStateMachineParameterStore* Parameters = nullptr;
};

// Unreal식 AnimInstance를 모방하기 위한 구조. 현재로써는 상속계층에 AnimStateMachineNode만 존재.
class FAnimNodeBase
{
public:
    virtual ~FAnimNodeBase() = default;

    virtual void Initialize(UAnimInstance* InOwnerAnimInstance)
    {
        OwnerAnimInstance = InOwnerAnimInstance;
    }

    virtual void Update(const FAnimNodeUpdateContext& Context) = 0;
    virtual void Reset() {}

    virtual FAnimStateMachineNode* AsStateMachineNode() { return nullptr; }
    virtual const FAnimStateMachineNode* AsStateMachineNode() const { return nullptr; }

protected:
    UAnimInstance* OwnerAnimInstance = nullptr;
};
