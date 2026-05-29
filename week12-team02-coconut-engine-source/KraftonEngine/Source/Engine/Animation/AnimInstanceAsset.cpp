#include "Animation/AnimInstanceAsset.h"

#include "Animation/AnimGraphInstance.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

IMPLEMENT_CLASS(UAnimInstanceAsset, UObject)

namespace
{
	constexpr uint32 AnimInstancePayloadMagic = 0x41494D49; // AIMA
	constexpr int32 AnimInstancePayloadVersion = 2;

	void SerializeStringWithKnownLength(FArchive& Ar, FString& Value, uint32 Length)
	{
		if (Ar.IsLoading())
		{
			Value.resize(Length);
		}
		if (Length > 0)
		{
			Ar.Serialize(Value.data(), Length * sizeof(char));
		}
	}

	void SerializeLegacyGraphParameter(FArchive& Ar, FAnimGraphParameter& Parameter)
	{
		Ar << Parameter.Name;
		Ar << Parameter.DefaultFloatValue;
		Parameter.ParameterType = EAnimGraphParameterType::Float;
		Parameter.DefaultBoolValue = false;
	}

	void SerializeLegacyGraphData(FArchive& Ar, FAnimGraphData& Graph)
	{
		Ar << Graph.OutputNodeId;
		Ar << Graph.Nodes;
		Ar << Graph.Links;

		uint32 ParameterCount = 0;
		Ar << ParameterCount;
		if (Ar.IsLoading())
		{
			Graph.Parameters.resize(ParameterCount);
		}
		for (FAnimGraphParameter& Parameter : Graph.Parameters)
		{
			SerializeLegacyGraphParameter(Ar, Parameter);
		}
	}
}

FArchive& operator<<(FArchive& Ar, FAnimGraphParameter& Parameter)
{
	Ar << Parameter.Name;

	int32 ParameterType = static_cast<int32>(Parameter.ParameterType);
	Ar << ParameterType;
	if (Ar.IsLoading())
	{
		Parameter.ParameterType = static_cast<EAnimGraphParameterType>(ParameterType);
	}

	Ar << Parameter.DefaultFloatValue;
	Ar << Parameter.DefaultBoolValue;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphPinLink& Link)
{
	Ar << Link.FromNodeId;
	Ar << Link.FromPinName;
	Ar << Link.ToNodeId;
	Ar << Link.ToPinName;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphNodeData& Node)
{
	Ar << Node.NodeId;
	Ar << Node.DisplayName;

	int32 NodeType = static_cast<int32>(Node.NodeType);
	Ar << NodeType;
	if (Ar.IsLoading())
	{
		Node.NodeType = static_cast<EAnimGraphNodeType>(NodeType);
	}

	Ar << Node.AnimSequencePath;
	Ar << Node.PlayRate;
	Ar << Node.bLoop;

	Ar << Node.ParameterName;
	Ar << Node.MinValue;
	Ar << Node.MaxValue;

	Ar << Node.EditorPosX;
	Ar << Node.EditorPosY;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphData& Graph)
{
	Ar << Graph.OutputNodeId;
	Ar << Graph.Nodes;
	Ar << Graph.Links;
	Ar << Graph.Parameters;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimTransitionCondition& Condition)
{
	Ar << Condition.ParameterName;

	int32 Operator = static_cast<int32>(Condition.Operator);
	Ar << Operator;
	if (Ar.IsLoading())
	{
		Condition.Operator = static_cast<EAnimTransitionConditionOperator>(Operator);
	}

	Ar << Condition.FloatThreshold;
	Ar << Condition.bBoolExpected;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimStateTransitionData& Transition)
{
	Ar << Transition.TransitionId;
	Ar << Transition.FromStateId;
	Ar << Transition.ToStateId;
	Ar << Transition.BlendDuration;
	Ar << Transition.Conditions;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimStateData& State)
{
	Ar << State.StateId;
	Ar << State.DisplayName;
	Ar << State.Graph;
	Ar << State.EditorPosX;
	Ar << State.EditorPosY;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimStateMachineData& StateMachine)
{
	Ar << StateMachine.EntryStateId;
	Ar << StateMachine.States;
	Ar << StateMachine.Transitions;
	return Ar;
}

void UAnimInstanceAsset::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		PromoteLegacyGraphParametersToAsset();
		uint32 Magic = AnimInstancePayloadMagic;
		int32 Version = AnimInstancePayloadVersion;
		Ar << Magic;
		Ar << Version;
		Ar << SkeletonPath;
		Ar << PreviewMeshPath;
		Ar << Parameters;
		Ar << Graph;
		Ar << StateMachine;
		return;
	}

	uint32 MagicOrLegacySkeletonLength = 0;
	Ar << MagicOrLegacySkeletonLength;
	if (MagicOrLegacySkeletonLength == AnimInstancePayloadMagic)
	{
		int32 Version = 0;
		Ar << Version;
		Ar << SkeletonPath;
		Ar << PreviewMeshPath;
		if (Version >= 2)
		{
			Ar << Parameters;
		}
		else
		{
			Parameters.clear();
		}
		Ar << Graph;
		Ar << StateMachine;
		PromoteLegacyGraphParametersToAsset();
		return;
	}

	SerializeStringWithKnownLength(Ar, SkeletonPath, MagicOrLegacySkeletonLength);
	Ar << PreviewMeshPath;
	SerializeLegacyGraphData(Ar, Graph);
	StateMachine = FAnimStateMachineData();
	Parameters.clear();
	PromoteLegacyGraphParametersToAsset();
}

UAnimGraphInstance* UAnimInstanceAsset::CreateRuntimeInstance()
{
	UAnimGraphInstance* Instance = GUObjectArray.CreateObject<UAnimGraphInstance>();
	if (Instance)
	{
		Instance->SetAsset(this);
	}
	return Instance;
}

void UAnimInstanceAsset::PromoteLegacyGraphParametersToAsset()
{
	const bool bCollectLegacyParameters = Parameters.empty();

	auto AddParameter = [this, bCollectLegacyParameters](const FAnimGraphParameter& Parameter)
	{
		if (!bCollectLegacyParameters || Parameter.Name.empty())
		{
			return;
		}

		for (const FAnimGraphParameter& Existing : Parameters)
		{
			if (Existing.Name == Parameter.Name && Existing.ParameterType == Parameter.ParameterType)
			{
				return;
			}
		}
		Parameters.push_back(Parameter);
	};

	for (const FAnimGraphParameter& Parameter : Graph.Parameters)
	{
		AddParameter(Parameter);
	}
	Graph.Parameters.clear();

	for (FAnimStateData& State : StateMachine.States)
	{
		for (const FAnimGraphParameter& Parameter : State.Graph.Parameters)
		{
			AddParameter(Parameter);
		}
		State.Graph.Parameters.clear();
	}
}
