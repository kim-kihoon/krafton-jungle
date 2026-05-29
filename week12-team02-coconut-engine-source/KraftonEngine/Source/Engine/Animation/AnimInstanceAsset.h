#pragma once

#include "Object/Object.h"

class UAnimGraphInstance;

enum class EAnimGraphNodeType : uint8
{
	SequencePlayer = 0,
	Blend2ByFloat,
	Output,
};

enum class EAnimGraphParameterType : uint8
{
	Float = 0,
	Bool,
};

enum class EAnimTransitionConditionOperator : uint8
{
	Greater = 0,
	GreaterEqual,
	Less,
	LessEqual,
	Equal,
	NotEqual,
	IsTrue,
	IsFalse,
};

struct FAnimGraphParameter
{
	FString Name;
	EAnimGraphParameterType ParameterType = EAnimGraphParameterType::Float;
	float DefaultFloatValue = 0.0f;
	bool DefaultBoolValue = false;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphParameter& Parameter);
};

struct FAnimGraphPinLink
{
	FString FromNodeId;
	FString FromPinName;
	FString ToNodeId;
	FString ToPinName;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphPinLink& Link);
};

struct FAnimGraphNodeData
{
	FString NodeId;
	FString DisplayName;
	EAnimGraphNodeType NodeType = EAnimGraphNodeType::SequencePlayer;

	// SequencePlayer
	FString AnimSequencePath;
	float PlayRate = 1.0f;
	bool bLoop = true;

	// Blend2ByFloat
	FString ParameterName;
	float MinValue = 0.0f;
	float MaxValue = 1.0f;

	// Editor-only layout metadata for the future graph UI.
	float EditorPosX = 0.0f;
	float EditorPosY = 0.0f;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphNodeData& Node);
};

struct FAnimGraphData
{
	FString OutputNodeId;
	TArray<FAnimGraphNodeData> Nodes;
	TArray<FAnimGraphPinLink> Links;
	TArray<FAnimGraphParameter> Parameters;

	friend FArchive& operator<<(FArchive& Ar, FAnimGraphData& Graph);
};

struct FAnimTransitionCondition
{
	FString ParameterName;
	EAnimTransitionConditionOperator Operator = EAnimTransitionConditionOperator::Greater;
	float FloatThreshold = 0.0f;
	bool bBoolExpected = true;

	friend FArchive& operator<<(FArchive& Ar, FAnimTransitionCondition& Condition);
};

struct FAnimStateTransitionData
{
	FString TransitionId;
	FString FromStateId;
	FString ToStateId;
	float BlendDuration = 0.2f;
	TArray<FAnimTransitionCondition> Conditions;

	friend FArchive& operator<<(FArchive& Ar, FAnimStateTransitionData& Transition);
};

struct FAnimStateData
{
	FString StateId;
	FString DisplayName;
	FAnimGraphData Graph;
	float EditorPosX = 0.0f;
	float EditorPosY = 0.0f;

	friend FArchive& operator<<(FArchive& Ar, FAnimStateData& State);
};

struct FAnimStateMachineData
{
	FString EntryStateId;
	TArray<FAnimStateData> States;
	TArray<FAnimStateTransitionData> Transitions;

	bool HasStates() const { return !States.empty(); }

	friend FArchive& operator<<(FArchive& Ar, FAnimStateMachineData& StateMachine);
};

class UAnimInstanceAsset : public UObject
{
public:
	DECLARE_CLASS(UAnimInstanceAsset, UObject)

	void Serialize(FArchive& Ar) override;

	const FString& GetAssetPathFileName() const override { return AssetPathFileName; }
	void SetAssetPathFileName(const FString& InPathFileName) { AssetPathFileName = InPathFileName; }

	const FString& GetSkeletonPath() const { return SkeletonPath; }
	void SetSkeletonPath(const FString& InSkeletonPath) { SkeletonPath = InSkeletonPath; }

	const FString& GetPreviewMeshPath() const { return PreviewMeshPath; }
	void SetPreviewMeshPath(const FString& InPreviewMeshPath) { PreviewMeshPath = InPreviewMeshPath; }

	FAnimGraphData& GetGraph() { return Graph; }
	const FAnimGraphData& GetGraph() const { return Graph; }

	TArray<FAnimGraphParameter>& GetParameters() { return Parameters; }
	const TArray<FAnimGraphParameter>& GetParameters() const { return Parameters; }
	void PromoteLegacyGraphParametersToAsset();

	FAnimStateMachineData& GetStateMachine() { return StateMachine; }
	const FAnimStateMachineData& GetStateMachine() const { return StateMachine; }
	bool HasStateMachine() const { return StateMachine.HasStates(); }

	UAnimGraphInstance* CreateRuntimeInstance();

private:
	FString AssetPathFileName = "None";
	FString SkeletonPath;
	FString PreviewMeshPath;
	TArray<FAnimGraphParameter> Parameters;
	FAnimGraphData Graph;
	FAnimStateMachineData StateMachine;
};
