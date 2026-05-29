#include "Animation/AnimGraphInstance.h"

#include "Animation/AnimInstanceAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceManager.h"
#include "Animation/AnimationRuntime.h"
#include "Core/Log.h"
#include "Object/ObjectFactory.h"

#include <algorithm>
#include <cstdarg>
#include <cmath>

IMPLEMENT_CLASS(UAnimGraphInstance, UAnimInstance)

namespace
{
	const char* PosePinName = "Pose";
	const char* InputAPinName = "A";
	const char* InputBPinName = "B";
	const char* RootStateKey = "__Root";

	float Clamp01(float Value)
	{
		return (std::max)(0.0f, (std::min)(Value, 1.0f));
	}

	float NormalizeSequenceEvalTime(float Time, float Length, bool bLoop)
	{
		if (Length <= 0.0f)
		{
			return 0.0f;
		}

		if (bLoop)
		{
			Time = std::fmod(Time, Length);
			if (Time < 0.0f)
			{
				Time += Length;
			}
			return Time;
		}

		return (std::max)(0.0f, (std::min)(Time, Length));
	}
}

void UAnimGraphInstance::SetAsset(UAnimInstanceAsset* InAsset)
{
	Asset = InAsset;
	LoggedWarnings.clear();
	ResetRuntimeState();
	InitializeParametersFromAsset();
}

void UAnimGraphInstance::SetFloatParameter(const FString& Name, float Value)
{
	if (Name.empty())
	{
		return;
	}

	FloatParameters[Name] = Value;
}

float UAnimGraphInstance::GetFloatParameter(const FString& Name, float DefaultValue) const
{
	auto It = FloatParameters.find(Name);
	return It != FloatParameters.end() ? It->second : DefaultValue;
}

void UAnimGraphInstance::SetBoolParameter(const FString& Name, bool Value)
{
	if (Name.empty())
	{
		return;
	}

	BoolParameters[Name] = Value;
}

bool UAnimGraphInstance::GetBoolParameter(const FString& Name, bool DefaultValue) const
{
	auto It = BoolParameters.find(Name);
	return It != BoolParameters.end() ? It->second : DefaultValue;
}

void UAnimGraphInstance::Initialize(USkeletalMeshComponent* InOwner, const FString& InScriptPath)
{
	Super::Initialize(InOwner, InScriptPath);
	ResetRuntimeState();
	InitializeParametersFromAsset();
}

void UAnimGraphInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (Asset && Asset->HasStateMachine())
	{
		AdvanceStateMachine(DeltaSeconds);
	}
	else
	{
		AdvanceSequencePlayers(DeltaSeconds);
	}
}

void UAnimGraphInstance::GetCurrentPose(FPoseContext& OutPose)
{
	OutPose.Reset();
	if (!Asset)
	{
		return;
	}

	FEvaluateResult Result = Asset->HasStateMachine()
		? EvaluateStateMachine()
		: EvaluateGraph(Asset->GetGraph(), RootStateKey);
	if (!Result.bValid)
	{
		return;
	}

	OutPose = Result.Pose;
	LastEvaluatedTime = Result.NotifyTime;
	for (const FAnimNotifyEvent& Notify : Result.Notifies)
	{
		RouteNotify(Notify);
	}
}

void UAnimGraphInstance::ResetRuntimeState()
{
	SequenceStates.clear();
	CurrentStateId.clear();
	PreviousStateId.clear();
	TransitionElapsed = 0.0f;
	TransitionDuration = 0.0f;
	ResetNotifyState();
	LastEvaluatedTime = 0.0f;
}

void UAnimGraphInstance::InitializeParametersFromAsset()
{
	FloatParameters.clear();
	BoolParameters.clear();
	if (!Asset)
	{
		return;
	}

	InitializeParameterDefinitions(Asset->GetParameters());
	InitializeParametersFromGraph(Asset->GetGraph());
	for (const FAnimStateData& State : Asset->GetStateMachine().States)
	{
		InitializeParametersFromGraph(State.Graph);
	}
}

void UAnimGraphInstance::InitializeParameterDefinitions(const TArray<FAnimGraphParameter>& Parameters)
{
	for (const FAnimGraphParameter& Parameter : Parameters)
	{
		if (!Parameter.Name.empty())
		{
			if (Parameter.ParameterType == EAnimGraphParameterType::Bool)
			{
				if (BoolParameters.find(Parameter.Name) == BoolParameters.end())
				{
					BoolParameters[Parameter.Name] = Parameter.DefaultBoolValue;
				}
			}
			else if (FloatParameters.find(Parameter.Name) == FloatParameters.end())
			{
				FloatParameters[Parameter.Name] = Parameter.DefaultFloatValue;
			}
		}
	}
}

void UAnimGraphInstance::InitializeParametersFromGraph(const FAnimGraphData& Graph)
{
	InitializeParameterDefinitions(Graph.Parameters);
}

void UAnimGraphInstance::AdvanceSequencePlayers(float DeltaSeconds)
{
	if (!Asset)
	{
		return;
	}

	AdvanceGraphSequencePlayers(Asset->GetGraph(), RootStateKey, DeltaSeconds);
}

void UAnimGraphInstance::AdvanceGraphSequencePlayers(const FAnimGraphData& Graph, const FString& StateKey, float DeltaSeconds)
{
	for (const FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (Node.NodeType != EAnimGraphNodeType::SequencePlayer)
		{
			continue;
		}

		FSequencePlayerRuntimeState& State = SequenceStates[MakeSequenceStateKey(StateKey, Node.NodeId)];
		State.PrevTime = State.CurrentTime;

		UAnimSequence* Sequence = LoadSequenceForNode(Node);
		if (!Sequence)
		{
			continue;
		}

		const float Length = Sequence->GetPlayLength();
		if (Length <= 0.0f)
		{
			State.CurrentTime = 0.0f;
			continue;
		}

		const float NextTime = State.CurrentTime + DeltaSeconds * Node.PlayRate;
		State.CurrentTime = Node.bLoop
			? NextTime
			: (std::max)(0.0f, (std::min)(NextTime, Length));
	}
}

void UAnimGraphInstance::AdvanceStateMachine(float DeltaSeconds)
{
	EnsureCurrentState();
	if (CurrentStateId.empty())
	{
		return;
	}

	if (PreviousStateId.empty())
	{
		if (const FAnimStateTransitionData* Transition = FindTriggeredTransition(CurrentStateId))
		{
			StartTransition(*Transition);
		}
	}

	if (!PreviousStateId.empty())
	{
		if (const FAnimStateData* PreviousState = FindState(PreviousStateId))
		{
			AdvanceGraphSequencePlayers(PreviousState->Graph, PreviousStateId, DeltaSeconds);
		}
	}

	if (const FAnimStateData* CurrentState = FindState(CurrentStateId))
	{
		AdvanceGraphSequencePlayers(CurrentState->Graph, CurrentStateId, DeltaSeconds);
	}

	if (!PreviousStateId.empty())
	{
		TransitionElapsed += DeltaSeconds;
		if (TransitionDuration <= 0.0f || TransitionElapsed >= TransitionDuration)
		{
			PreviousStateId.clear();
			TransitionElapsed = 0.0f;
			TransitionDuration = 0.0f;
		}
	}
}

void UAnimGraphInstance::EnsureCurrentState()
{
	if (!Asset || !Asset->HasStateMachine())
	{
		CurrentStateId.clear();
		return;
	}

	if (!CurrentStateId.empty() && FindState(CurrentStateId))
	{
		return;
	}

	const FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	if (!StateMachine.EntryStateId.empty() && FindState(StateMachine.EntryStateId))
	{
		CurrentStateId = StateMachine.EntryStateId;
	}
	else if (!StateMachine.States.empty())
	{
		CurrentStateId = StateMachine.States.front().StateId;
	}
}

void UAnimGraphInstance::StartTransition(const FAnimStateTransitionData& Transition)
{
	if (!FindState(Transition.ToStateId))
	{
		return;
	}

	PreviousStateId = CurrentStateId;
	CurrentStateId = Transition.ToStateId;
	TransitionElapsed = 0.0f;
	TransitionDuration = (std::max)(0.0f, Transition.BlendDuration);
	ResetStateSequencePlayers(CurrentStateId);
	if (TransitionDuration <= 0.0f)
	{
		PreviousStateId.clear();
	}
}

void UAnimGraphInstance::ResetStateSequencePlayers(const FString& StateId)
{
	const FString Prefix = StateId + ":";
	for (auto It = SequenceStates.begin(); It != SequenceStates.end();)
	{
		if (It->first.rfind(Prefix, 0) == 0)
		{
			It = SequenceStates.erase(It);
		}
		else
		{
			++It;
		}
	}
}

const FAnimStateData* UAnimGraphInstance::FindState(const FString& StateId) const
{
	if (!Asset || StateId.empty())
	{
		return nullptr;
	}

	for (const FAnimStateData& State : Asset->GetStateMachine().States)
	{
		if (State.StateId == StateId)
		{
			return &State;
		}
	}
	return nullptr;
}

const FAnimStateTransitionData* UAnimGraphInstance::FindTriggeredTransition(const FString& StateId) const
{
	if (!Asset)
	{
		return nullptr;
	}

	for (const FAnimStateTransitionData& Transition : Asset->GetStateMachine().Transitions)
	{
		if (Transition.FromStateId == StateId && IsTransitionTriggered(Transition))
		{
			return &Transition;
		}
	}
	return nullptr;
}

bool UAnimGraphInstance::IsTransitionTriggered(const FAnimStateTransitionData& Transition) const
{
	if (!FindState(Transition.FromStateId) || !FindState(Transition.ToStateId) || Transition.Conditions.empty())
	{
		return false;
	}

	for (const FAnimTransitionCondition& Condition : Transition.Conditions)
	{
		if (!IsTransitionConditionMet(Condition))
		{
			return false;
		}
	}
	return true;
}

bool UAnimGraphInstance::IsTransitionConditionMet(const FAnimTransitionCondition& Condition) const
{
	const FAnimGraphParameter* Parameter = FindParameterDefinition(Condition.ParameterName);
	if (!Parameter)
	{
		return false;
	}

	if (Parameter->ParameterType == EAnimGraphParameterType::Bool)
	{
		auto It = BoolParameters.find(Condition.ParameterName);
		if (It == BoolParameters.end())
		{
			return false;
		}

		bool ExpectedValue = Condition.bBoolExpected;
		if (Condition.Operator == EAnimTransitionConditionOperator::IsTrue)
		{
			ExpectedValue = true;
		}
		else if (Condition.Operator == EAnimTransitionConditionOperator::IsFalse)
		{
			ExpectedValue = false;
		}
		return It->second == ExpectedValue;
	}

	auto It = FloatParameters.find(Condition.ParameterName);
	if (It == FloatParameters.end())
	{
		return false;
	}

	const float Value = It->second;
	const float Threshold = Condition.FloatThreshold;
	switch (Condition.Operator)
	{
	case EAnimTransitionConditionOperator::Greater:
		return Value > Threshold;
	case EAnimTransitionConditionOperator::GreaterEqual:
		return Value >= Threshold;
	case EAnimTransitionConditionOperator::Less:
		return Value < Threshold;
	case EAnimTransitionConditionOperator::LessEqual:
		return Value <= Threshold;
	case EAnimTransitionConditionOperator::Equal:
		return std::fabs(Value - Threshold) <= 0.0001f;
	case EAnimTransitionConditionOperator::NotEqual:
		return std::fabs(Value - Threshold) > 0.0001f;
	default:
		return false;
	}
}

const FAnimGraphParameter* UAnimGraphInstance::FindParameterDefinition(const FString& Name) const
{
	if (!Asset || Name.empty())
	{
		return nullptr;
	}

	for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
	{
		if (Parameter.Name == Name)
		{
			return &Parameter;
		}
	}

	for (const FAnimGraphParameter& Parameter : Asset->GetGraph().Parameters)
	{
		if (Parameter.Name == Name)
		{
			return &Parameter;
		}
	}

	for (const FAnimStateData& State : Asset->GetStateMachine().States)
	{
		for (const FAnimGraphParameter& Parameter : State.Graph.Parameters)
		{
			if (Parameter.Name == Name)
			{
				return &Parameter;
			}
		}
	}

	return nullptr;
}

const FAnimGraphNodeData* UAnimGraphInstance::FindNode(const FAnimGraphData& Graph, const FString& NodeId) const
{
	if (NodeId.empty())
	{
		return nullptr;
	}

	for (const FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (Node.NodeId == NodeId)
		{
			return &Node;
		}
	}

	return nullptr;
}

const FAnimGraphPinLink* UAnimGraphInstance::FindInputLink(const FAnimGraphData& Graph, const FString& ToNodeId, const FString& ToPinName) const
{
	for (const FAnimGraphPinLink& Link : Graph.Links)
	{
		if (Link.ToNodeId == ToNodeId && Link.ToPinName == ToPinName)
		{
			return &Link;
		}
	}

	return nullptr;
}

const FAnimGraphNodeData* UAnimGraphInstance::FindLinkedInputNode(const FAnimGraphData& Graph, const FString& ToNodeId, const FString& ToPinName) const
{
	const FAnimGraphPinLink* Link = FindInputLink(Graph, ToNodeId, ToPinName);
	return Link ? FindNode(Graph, Link->FromNodeId) : nullptr;
}

UAnimGraphInstance::FEvaluateResult UAnimGraphInstance::EvaluateGraph(const FAnimGraphData& Graph, const FString& StateKey)
{
	const FAnimGraphNodeData* OutputNode = FindNode(Graph, Graph.OutputNodeId);
	if (!OutputNode)
	{
		for (const FAnimGraphNodeData& Node : Graph.Nodes)
		{
			if (Node.NodeType == EAnimGraphNodeType::Output)
			{
				OutputNode = &Node;
				break;
			}
		}
	}

	if (!OutputNode)
	{
		LogWarningOnce("MissingOutput:" + StateKey, "AnimGraph evaluation failed: output node is missing. Asset=%s State=%s", Asset ? Asset->GetAssetPathFileName().c_str() : "", StateKey.c_str());
		return FEvaluateResult();
	}

	TSet<FString> VisitingNodes;
	return EvaluateNode(Graph, StateKey, *OutputNode, VisitingNodes);
}

UAnimGraphInstance::FEvaluateResult UAnimGraphInstance::EvaluateStateMachine()
{
	EnsureCurrentState();
	const FAnimStateData* CurrentState = FindState(CurrentStateId);
	if (!CurrentState)
	{
		return FEvaluateResult();
	}

	FEvaluateResult Current = EvaluateGraph(CurrentState->Graph, CurrentStateId);
	if (PreviousStateId.empty() || TransitionDuration <= 0.0f)
	{
		return Current;
	}

	const FAnimStateData* PreviousState = FindState(PreviousStateId);
	if (!PreviousState)
	{
		return Current;
	}

	FEvaluateResult Previous = EvaluateGraph(PreviousState->Graph, PreviousStateId);
	if (!Previous.bValid || !Current.bValid)
	{
		return Current.bValid ? Current : Previous;
	}

	if (Previous.Pose.BoneLocalTransforms.size() != Current.Pose.BoneLocalTransforms.size())
	{
		LogWarningOnce("StateBlendPoseSize:" + PreviousStateId + ":" + CurrentStateId, "AnimStateMachine blend failed: pose bone count mismatch. Previous=%s Current=%s", PreviousStateId.c_str(), CurrentStateId.c_str());
		return Current;
	}

	FEvaluateResult Result;
	const float Alpha = Clamp01(TransitionElapsed / TransitionDuration);
	FAnimationRuntime::BlendTwoPoses(Previous.Pose, Current.Pose, Alpha, Result.Pose);
	Result.Notifies = Alpha < 0.5f ? Previous.Notifies : Current.Notifies;
	Result.NotifyTime = Alpha < 0.5f ? Previous.NotifyTime : Current.NotifyTime;
	Result.bValid = !Result.Pose.BoneLocalTransforms.empty();
	return Result;
}

UAnimGraphInstance::FEvaluateResult UAnimGraphInstance::EvaluateNode(const FAnimGraphData& Graph, const FString& StateKey, const FAnimGraphNodeData& Node, TSet<FString>& VisitingNodes)
{
	FEvaluateResult Result;
	if (!Asset)
	{
		return Result;
	}

	if (Node.NodeId.empty())
	{
		LogWarningOnce("EmptyNodeId", "AnimGraph evaluation failed: node id is empty. Asset=%s", Asset->GetAssetPathFileName().c_str());
		return Result;
	}

	if (VisitingNodes.find(Node.NodeId) != VisitingNodes.end())
	{
		LogWarningOnce("Cycle:" + Node.NodeId, "AnimGraph evaluation failed: cycle detected at node %s. Asset=%s", Node.NodeId.c_str(), Asset->GetAssetPathFileName().c_str());
		return Result;
	}

	VisitingNodes.insert(Node.NodeId);

	switch (Node.NodeType)
	{
	case EAnimGraphNodeType::SequencePlayer:
		Result = EvaluateSequencePlayerNode(StateKey, Node);
		break;
	case EAnimGraphNodeType::Blend2ByFloat:
		Result = EvaluateBlend2ByFloatNode(Graph, StateKey, Node, VisitingNodes);
		break;
	case EAnimGraphNodeType::Output:
		Result = EvaluateLinkedInput(Graph, StateKey, Node, PosePinName, VisitingNodes);
		break;
	default:
		LogWarningOnce("UnknownNodeType:" + Node.NodeId, "AnimGraph evaluation failed: unknown node type. Node=%s Asset=%s", Node.NodeId.c_str(), Asset->GetAssetPathFileName().c_str());
		break;
	}

	VisitingNodes.erase(Node.NodeId);
	return Result;
}

UAnimGraphInstance::FEvaluateResult UAnimGraphInstance::EvaluateSequencePlayerNode(const FString& StateKey, const FAnimGraphNodeData& Node)
{
	FEvaluateResult Result;
	UAnimSequence* Sequence = LoadSequenceForNode(Node);
	if (!Sequence)
	{
		return Result;
	}

	FSequencePlayerRuntimeState& State = SequenceStates[MakeSequenceStateKey(StateKey, Node.NodeId)];
	const float Length = Sequence->GetPlayLength();
	const float EvalTime = NormalizeSequenceEvalTime(State.CurrentTime, Length, Node.bLoop);
	if (!Sequence->EvaluatePose(EvalTime, Result.Pose, false))
	{
		LogWarningOnce("EvaluateSequence:" + Node.NodeId, "AnimGraph sequence evaluation failed. Node=%s Sequence=%s", Node.NodeId.c_str(), Node.AnimSequencePath.c_str());
		return Result;
	}

	Sequence->CollectNotifies(State.PrevTime, State.CurrentTime, Node.bLoop, Node.PlayRate < 0.0f, Result.Notifies);
	Result.NotifyTime = EvalTime;
	Result.bValid = true;
	return Result;
}

UAnimGraphInstance::FEvaluateResult UAnimGraphInstance::EvaluateBlend2ByFloatNode(const FAnimGraphData& Graph, const FString& StateKey, const FAnimGraphNodeData& Node, TSet<FString>& VisitingNodes)
{
	FEvaluateResult Result;
	FEvaluateResult A = EvaluateLinkedInput(Graph, StateKey, Node, InputAPinName, VisitingNodes);
	FEvaluateResult B = EvaluateLinkedInput(Graph, StateKey, Node, InputBPinName, VisitingNodes);
	if (!A.bValid || !B.bValid)
	{
		LogWarningOnce("BlendMissingInput:" + Node.NodeId, "AnimGraph blend evaluation failed: missing input. Node=%s Asset=%s", Node.NodeId.c_str(), Asset ? Asset->GetAssetPathFileName().c_str() : "");
		return Result;
	}

	if (A.Pose.BoneLocalTransforms.size() != B.Pose.BoneLocalTransforms.size())
	{
		LogWarningOnce("BlendPoseSize:" + Node.NodeId, "AnimGraph blend evaluation failed: pose bone count mismatch. Node=%s", Node.NodeId.c_str());
		return Result;
	}

	const float Alpha = ResolveBlendAlpha(Node);
	FAnimationRuntime::BlendTwoPoses(A.Pose, B.Pose, Alpha, Result.Pose);

	Result.Notifies = Alpha < 0.5f ? A.Notifies : B.Notifies;
	Result.NotifyTime = Alpha < 0.5f ? A.NotifyTime : B.NotifyTime;
	Result.bValid = !Result.Pose.BoneLocalTransforms.empty();
	return Result;
}

UAnimGraphInstance::FEvaluateResult UAnimGraphInstance::EvaluateLinkedInput(const FAnimGraphData& Graph, const FString& StateKey, const FAnimGraphNodeData& Node, const FString& PinName, TSet<FString>& VisitingNodes)
{
	FEvaluateResult Result;
	const FAnimGraphNodeData* InputNode = FindLinkedInputNode(Graph, Node.NodeId, PinName);
	if (!InputNode)
	{
		LogWarningOnce("MissingLink:" + Node.NodeId + ":" + PinName, "AnimGraph evaluation failed: missing input link. Node=%s Pin=%s", Node.NodeId.c_str(), PinName.c_str());
		return Result;
	}

	return EvaluateNode(Graph, StateKey, *InputNode, VisitingNodes);
}

float UAnimGraphInstance::ResolveBlendAlpha(const FAnimGraphNodeData& Node)
{
	if (Node.ParameterName.empty())
	{
		LogWarningOnce("MissingParameterName:" + Node.NodeId, "AnimGraph blend node has no parameter name. Node=%s", Node.NodeId.c_str());
		return 0.0f;
	}

	auto It = FloatParameters.find(Node.ParameterName);
	if (It == FloatParameters.end())
	{
		LogWarningOnce("MissingParameter:" + Node.ParameterName, "AnimGraph parameter is missing: %s", Node.ParameterName.c_str());
		return 0.0f;
	}

	const float Denominator = Node.MaxValue - Node.MinValue;
	if (std::fabs(Denominator) <= 0.00001f)
	{
		LogWarningOnce("InvalidRange:" + Node.NodeId, "AnimGraph blend node has invalid range. Node=%s", Node.NodeId.c_str());
		return 0.0f;
	}

	return Clamp01((It->second - Node.MinValue) / Denominator);
}

FString UAnimGraphInstance::MakeSequenceStateKey(const FString& StateKey, const FString& NodeId) const
{
	return StateKey + ":" + NodeId;
}

UAnimSequence* UAnimGraphInstance::LoadSequenceForNode(const FAnimGraphNodeData& Node)
{
	if (Node.AnimSequencePath.empty())
	{
		LogWarningOnce("EmptySequence:" + Node.NodeId, "AnimGraph sequence node has no sequence path. Node=%s", Node.NodeId.c_str());
		return nullptr;
	}

	UAnimSequence* Sequence = FAnimSequenceManager::Get().Load(Node.AnimSequencePath);
	if (!Sequence)
	{
		LogWarningOnce("MissingSequence:" + Node.AnimSequencePath, "AnimGraph sequence load failed. Node=%s Path=%s", Node.NodeId.c_str(), Node.AnimSequencePath.c_str());
	}
	return Sequence;
}

void UAnimGraphInstance::LogWarningOnce(const FString& Key, const char* Format, ...)
{
	if (LoggedWarnings.find(Key) != LoggedWarnings.end())
	{
		return;
	}

	LoggedWarnings.insert(Key);

	va_list Args;
	va_start(Args, Format);
	FLogManager::Get().LogV(Format, Args);
	va_end(Args);
}
