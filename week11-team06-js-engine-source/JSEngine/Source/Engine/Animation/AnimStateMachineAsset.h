#pragma once

#include "Animation/AnimStateMachineTypes.h"
#include "Object/Object.h"

#include "UAnimStateMachineAsset.generated.h"

class FAnimStateMachineSerializer;

UCLASS()
class UAnimStateMachineAsset : public UObject
{
public:
    GENERATED_BODY(UAnimStateMachineAsset, UObject)

    void Clear();
    void SetEntryState(const FName& StateName);
    bool SetEntryStateId(FAnimStateId StateId);
    bool AddState(
        const FName& StateName,
        const FName& AnimationName,
        bool bLoop,
        const FString& AnimationPath = "");
    bool RenameState(FAnimStateId StateId, const FName& NewName);
    bool RemoveState(FAnimStateId StateId);
    bool SetStateLoop(FAnimStateId StateId, bool bLoop);
    bool SetStateAnimationPath(const FName& StateName, const FString& AnimationPath);
    bool SetStateAnimationPathById(FAnimStateId StateId, const FString& AnimationPath);
    bool SetStateEditorPosition(FAnimStateId StateId, float NodeX, float NodeY);
    bool AddTransition(
        const FName& FromState,
        const FName& ToState,
        const TArray<FAnimTransitionCondition>& Conditions,
        float BlendTime,
        int32 Priority,
        EAnimBlendEaseOption EaseOption = EAnimBlendEaseOption::Linear);
    bool RemoveTransition(FAnimTransitionId TransitionId);
    bool ReconnectTransition(FAnimTransitionId TransitionId, FAnimStateId FromStateId, FAnimStateId ToStateId);
    bool UpdateTransition(
        FAnimTransitionId TransitionId,
        float BlendTime,
        int32 Priority,
        EAnimBlendEaseOption EaseOption);
    bool SetTransitionConditions(
        FAnimTransitionId TransitionId,
        const TArray<FAnimTransitionCondition>& Conditions);
    bool AddBoolTransition(
        const FName& FromState,
        const FName& ToState,
        const FName& ParameterName,
        EAnimCompareOp CompareOp,
        bool Value,
        float BlendTime,
        int32 Priority,
        EAnimBlendEaseOption EaseOption = EAnimBlendEaseOption::Linear);
    bool AddIntTransition(
        const FName& FromState,
        const FName& ToState,
        const FName& ParameterName,
        EAnimCompareOp CompareOp,
        int32 Value,
        float BlendTime,
        int32 Priority,
        EAnimBlendEaseOption EaseOption = EAnimBlendEaseOption::Linear);
    bool AddFloatTransition(
        const FName& FromState,
        const FName& ToState,
        const FName& ParameterName,
        EAnimCompareOp CompareOp,
        float Value,
        float BlendTime,
        int32 Priority,
        EAnimBlendEaseOption EaseOption = EAnimBlendEaseOption::Linear);
    bool AddVectorTransition(
        const FName& FromState,
        const FName& ToState,
        const FName& ParameterName,
        EAnimCompareOp CompareOp,
        const FVector& Value,
        float BlendTime,
        int32 Priority,
        EAnimBlendEaseOption EaseOption = EAnimBlendEaseOption::Linear);
    bool AddTriggerTransition(
        const FName& FromState,
        const FName& ToState,
        const FName& ParameterName,
        float BlendTime,
        int32 Priority,
        EAnimBlendEaseOption EaseOption = EAnimBlendEaseOption::Linear);

    bool AddParameter(const FName& ParameterName, EAnimParameterType ParameterType);
    bool RenameParameter(const FName& OldParameterName, const FName& NewParameterName);
    bool SetParameterType(const FName& ParameterName, EAnimParameterType ParameterType);
    bool RemoveParameter(const FName& ParameterName);
    bool RemoveParameterAndConditions(const FName& ParameterName);
    uint32 RemoveUnusedParameters();

    const FName& GetEntryState() const { return EntryState; }
    FAnimStateId GetEntryStateId() const { return EntryStateId; }
    const TArray<FAnimStateMachineParameterDesc>& GetParameters() const { return Parameters; }
    const TArray<FAnimStateDesc>& GetStates() const { return States; }
    const TArray<FAnimTransitionDesc>& GetTransitions() const { return Transitions; }
    const TArray<FAnimStateEditorMetadata>& GetStateEditorMetadata() const { return StateEditorMetadata; }
    const TArray<FAnimTransitionEditorMetadata>& GetTransitionEditorMetadata() const { return TransitionEditorMetadata; }

    const FAnimStateMachineParameterDesc* FindParameter(const FName& ParameterName) const;
    FAnimStateMachineParameterDesc* FindParameter(const FName& ParameterName);
    const FAnimStateDesc* FindState(const FName& StateName) const;
    FAnimStateDesc* FindState(const FName& StateName);
    const FAnimStateDesc* FindStateById(FAnimStateId StateId) const;
    FAnimStateDesc* FindStateById(FAnimStateId StateId);
    const FAnimTransitionDesc* FindTransitionById(FAnimTransitionId TransitionId) const;
    FAnimTransitionDesc* FindTransitionById(FAnimTransitionId TransitionId);
    TArray<const FAnimTransitionDesc*> GetTransitionsFrom(const FName& StateName) const;
    TArray<const FAnimTransitionDesc*> GetTransitionsFrom(FAnimStateId StateId) const;

    bool Validate(FString* OutMessage = nullptr) const;

    bool SaveToFile(const FString& Path) const;
    static UAnimStateMachineAsset* LoadFromFile(const FString& Path);
    static bool ReadAnimationDependenciesFromFile(const FString& Path, TArray<FString>& OutAnimationPaths);

private:
    friend class FAnimStateMachineSerializer;

    FAnimStateId GenerateStateId() const;
    FAnimTransitionId GenerateTransitionId() const;
    bool AddStateWithId(
        FAnimStateId StateId,
        const FName& StateName,
        const FName& AnimationName,
        bool bLoop,
        const FString& AnimationPath);
    bool AddTransitionWithId(
        FAnimTransitionId TransitionId,
        FAnimStateId FromStateId,
        FAnimStateId ToStateId,
        const TArray<FAnimTransitionCondition>& Conditions,
        float BlendTime,
        int32 Priority,
        EAnimBlendEaseOption EaseOption = EAnimBlendEaseOption::Linear);
    bool HasDuplicateTransition(
        FAnimStateId FromStateId,
        FAnimStateId ToStateId,
        const TArray<FAnimTransitionCondition>& Conditions) const;
    bool HasDuplicateTransition(
        FAnimStateId FromStateId,
        FAnimStateId ToStateId,
        const TArray<FAnimTransitionCondition>& Conditions,
        FAnimTransitionId IgnoredTransitionId) const;
    bool IsParameterReferenced(const FName& ParameterName) const;
    bool IsConditionCompatibleWithDeclaration(const FAnimTransitionCondition& Condition, FString* OutMessage = nullptr) const;
    bool EnsureParameterDeclaration(const FName& ParameterName, EAnimParameterType ParameterType);

private:
    FName EntryState;
    FAnimStateId EntryStateId = InvalidAnimStateId;
    TArray<FAnimStateMachineParameterDesc> Parameters;
    TArray<FAnimStateDesc> States;
    TArray<FAnimTransitionDesc> Transitions;
    TArray<FAnimStateEditorMetadata> StateEditorMetadata;
    TArray<FAnimTransitionEditorMetadata> TransitionEditorMetadata;
};
