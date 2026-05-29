#pragma once

#include "Animation/AnimInstance.h"

class UAnimInstanceAsset;
class UAnimSequence;
struct FAnimGraphData;
struct FAnimGraphNodeData;
struct FAnimGraphPinLink;
struct FAnimGraphParameter;
struct FAnimStateData;
struct FAnimStateTransitionData;
struct FAnimTransitionCondition;

class UAnimGraphInstance : public UAnimInstance
{
public:
	DECLARE_CLASS(UAnimGraphInstance, UAnimInstance)

	void SetAsset(UAnimInstanceAsset* InAsset);
	UAnimInstanceAsset* GetAsset() const { return Asset; }

	void SetFloatParameter(const FString& Name, float Value);
	float GetFloatParameter(const FString& Name, float DefaultValue = 0.0f) const;
	void SetBoolParameter(const FString& Name, bool Value);
	bool GetBoolParameter(const FString& Name, bool DefaultValue = false) const;

	void Initialize(USkeletalMeshComponent* InOwner, const FString& InScriptPath = "") override;
	void NativeUpdateAnimation(float DeltaSeconds) override;
	void GetCurrentPose(FPoseContext& OutPose) override;

private:
	struct FSequencePlayerRuntimeState
	{
		float PrevTime = 0.0f;
		float CurrentTime = 0.0f;
	};

	struct FEvaluateResult
	{
		bool bValid = false;
		FPoseContext Pose;
		TArray<FAnimNotifyEvent> Notifies;
		float NotifyTime = 0.0f;
	};

	UAnimInstanceAsset* Asset = nullptr;
	TMap<FString, float> FloatParameters;
	TMap<FString, bool> BoolParameters;
	TMap<FString, FSequencePlayerRuntimeState> SequenceStates;
	TSet<FString> LoggedWarnings;
	FString CurrentStateId;
	FString PreviousStateId;
	float TransitionElapsed = 0.0f;
	float TransitionDuration = 0.0f;

	void ResetRuntimeState();
	void InitializeParametersFromAsset();
	void InitializeParameterDefinitions(const TArray<FAnimGraphParameter>& Parameters);
	void InitializeParametersFromGraph(const FAnimGraphData& Graph);
	void AdvanceSequencePlayers(float DeltaSeconds);

	void AdvanceGraphSequencePlayers(const FAnimGraphData& Graph, const FString& StateKey, float DeltaSeconds);
	void AdvanceStateMachine(float DeltaSeconds);
	void EnsureCurrentState();
	void StartTransition(const FAnimStateTransitionData& Transition);
	void ResetStateSequencePlayers(const FString& StateId);

	const FAnimStateData* FindState(const FString& StateId) const;
	const FAnimStateTransitionData* FindTriggeredTransition(const FString& StateId) const;
	bool IsTransitionTriggered(const FAnimStateTransitionData& Transition) const;
	bool IsTransitionConditionMet(const FAnimTransitionCondition& Condition) const;
	const FAnimGraphParameter* FindParameterDefinition(const FString& Name) const;

	const FAnimGraphNodeData* FindNode(const FAnimGraphData& Graph, const FString& NodeId) const;
	const FAnimGraphPinLink* FindInputLink(const FAnimGraphData& Graph, const FString& ToNodeId, const FString& ToPinName) const;
	const FAnimGraphNodeData* FindLinkedInputNode(const FAnimGraphData& Graph, const FString& ToNodeId, const FString& ToPinName) const;

	FEvaluateResult EvaluateGraph(const FAnimGraphData& Graph, const FString& StateKey);
	FEvaluateResult EvaluateStateMachine();
	FEvaluateResult EvaluateNode(const FAnimGraphData& Graph, const FString& StateKey, const FAnimGraphNodeData& Node, TSet<FString>& VisitingNodes);
	FEvaluateResult EvaluateSequencePlayerNode(const FString& StateKey, const FAnimGraphNodeData& Node);
	FEvaluateResult EvaluateBlend2ByFloatNode(const FAnimGraphData& Graph, const FString& StateKey, const FAnimGraphNodeData& Node, TSet<FString>& VisitingNodes);
	FEvaluateResult EvaluateLinkedInput(const FAnimGraphData& Graph, const FString& StateKey, const FAnimGraphNodeData& Node, const FString& PinName, TSet<FString>& VisitingNodes);

	float ResolveBlendAlpha(const FAnimGraphNodeData& Node);
	FString MakeSequenceStateKey(const FString& StateKey, const FString& NodeId) const;
	UAnimSequence* LoadSequenceForNode(const FAnimGraphNodeData& Node);
	void LogWarningOnce(const FString& Key, const char* Format, ...);
};
