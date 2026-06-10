#include "AnimGraphAsset.h"

#include "Animation/Sequence/AnimSequenceBase.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <string>

// ── AnimGraphTypes operator<< ──
//
// 주의 — FAnimGraphPin / FAnimGraphTransition 은 멤버가 모두 trivially_copyable
// (FName 2*uint32 / enum class / float / uint32) → struct 자체도 trivially_copyable.
// 그래서 TArray<T> 의 generic operator<< 가 sizeof(T) raw memcpy 분기 (Archive.h:75) 로 빠지고
// 우리 element-wise operator<< 가 호출되지 않는다 — FName 의 풀 인덱스가 raw bytes 로 저장되어
// 다음 세션의 풀 build 결과와 mismatch → "From/To 사라짐" 류 버그.
//
// 해결: 해당 두 TArray 타입에 대한 non-template overload 를 정의 → ADL 이 generic template
// 보다 우선 선택하여 element-wise 경로 강제. FAnimGraphNode/FAnimGraphState 는 nested TArray /
// FString 보유로 자연스럽게 non-trivially-copyable → 영향 없음.

FArchive& operator<<(FArchive& Ar, FAnimGraphPin&        Pin);
FArchive& operator<<(FArchive& Ar, FAnimGraphTransition& T);
FArchive& operator<<(FArchive& Ar, FAnimGraphVariable&   Var);

namespace
{
	constexpr uint32 kAnimGraphAssetMagic   = 0x46475241u; // 'AGRF' - Anim Graph File
	constexpr uint32 kAnimGraphAssetVersion = 3u;          // v3 adds AnimGraph-owned variables.
	thread_local bool g_LoadLegacyTransitionFormat = false;

	struct FLegacyTransitionFormatScope
	{
		explicit FLegacyTransitionFormatScope(bool bEnable)
			: bPrevious(g_LoadLegacyTransitionFormat)
		{
			g_LoadLegacyTransitionFormat = bEnable;
		}
		~FLegacyTransitionFormatScope()
		{
			g_LoadLegacyTransitionFormat = bPrevious;
		}
		bool bPrevious = false;
	};
}

inline FArchive& operator<<(FArchive& Ar, TArray<FAnimGraphPin>& Array)
{
	uint32 N = static_cast<uint32>(Array.size());
	Ar << N;
	if (Ar.IsLoading()) Array.resize(N);
	for (auto& Item : Array) Ar << Item;
	return Ar;
}

inline FArchive& operator<<(FArchive& Ar, TArray<FAnimGraphTransition>& Array)
{
	uint32 N = static_cast<uint32>(Array.size());
	Ar << N;
	if (Ar.IsLoading()) Array.resize(N);
	for (auto& Item : Array) Ar << Item;
	return Ar;
}

inline FArchive& operator<<(FArchive& Ar, TArray<FAnimGraphVariable>& Array)
{
	uint32 N = static_cast<uint32>(Array.size());
	Ar << N;
	if (Ar.IsLoading()) Array.resize(N);
	for (auto& Item : Array) Ar << Item;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphPin& Pin)
{
	Ar << Pin.PinId;
	Ar << Pin.OwningNodeId;
	Ar << Pin.Kind;        // enum class : uint8 — trivially_copyable
	Ar << Pin.Type;
	Ar << Pin.DisplayName;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphLink& Link)
{
	Ar << Link.LinkId;
	Ar << Link.FromPinId;
	Ar << Link.ToPinId;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphVariable& Var)
{
	Ar << Var.VariableName;
	Ar << Var.Type;
	Ar << Var.DefaultValue;
	Ar << Var.Category;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphState& State)
{
	Ar << State.StateName;
	Ar << State.SequencePath;
	Ar << State.PlayRate;
	Ar << State.bLooping;
	Ar << State.SubGraphNodeId;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphTransition& T)
{
	Ar << T.FromStateName;
	Ar << T.ToStateName;
	Ar << T.VariableName;
	Ar << T.Op;
	Ar << T.Threshold;
	Ar << T.BlendTime;
	if (Ar.IsLoading() && g_LoadLegacyTransitionFormat)
	{
		// v1 assets stored only Variable/Op/Threshold/Blend. Preserve the old compare-based behavior.
		T.RuleKind = ETransitionRuleKind::FloatCompare;
	}
	else
	{
		Ar << T.RuleKind;
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FAnimGraphNode& Node)
{
	Ar << Node.NodeId;
	Ar << Node.Type;
	Ar << Node.DisplayName;
	Ar << Node.PosX;
	Ar << Node.PosY;
	Ar << Node.Pins;
	Ar << Node.PlayRate;
	Ar << Node.bLooping;
	Ar << Node.SequencePath; // SequenceRef 는 transient — Initialize 시 path 로 재해상.
	Ar << Node.SlotName;
	Ar << Node.BlendWeight;
	Ar << Node.RootBoneName;
	Ar << Node.VariableName;
	Ar << Node.States;
	Ar << Node.Transitions;
	Ar << Node.InitialStateName;
	return Ar;
}

// ── UAnimGraphAsset ──

FAnimGraphVariable* UAnimGraphAsset::AddVariable(const FName& Name, EAnimGraphPinType Type)
{
	FName FinalName = Name == FName::None ? FName("NewVariable") : Name;
	const FString Base = FinalName.ToString().empty() ? FString("NewVariable") : FinalName.ToString();
	int32 Suffix = 1;
	while (FindVariable(FinalName))
	{
		char Buf[128];
		std::snprintf(Buf, sizeof(Buf), "%s_%d", Base.c_str(), Suffix++);
		FinalName = FName(Buf);
	}

	FAnimGraphVariable Var;
	Var.VariableName = FinalName;
	Var.Type = Type;
	Var.DefaultValue = (Type == EAnimGraphPinType::Bool) ? 0.0f : 0.0f;
	Variables.push_back(std::move(Var));
	BumpVersion();
	return &Variables.back();
}

bool UAnimGraphAsset::RemoveVariable(const FName& Name)
{
	if (Name == FName::None) return false;
	const size_t Before = Variables.size();
	Variables.erase(std::remove_if(Variables.begin(), Variables.end(),
		[&Name](const FAnimGraphVariable& V) { return V.VariableName == Name; }), Variables.end());
	const bool bRemoved = Variables.size() != Before;
	if (!bRemoved) return false;

	// Delete cascade: nodes / transition rules bound to the removed variable become unbound.
	for (FAnimGraphNode& Node : Nodes)
	{
		if (Node.VariableName == Name) Node.VariableName = FName::None;
		for (FAnimGraphTransition& T : Node.Transitions)
		{
			if (T.VariableName == Name) T.VariableName = FName::None;
		}
	}
	BumpVersion();
	return true;
}

bool UAnimGraphAsset::RenameVariable(const FName& OldName, const FName& NewName)
{
	if (OldName == FName::None || NewName == FName::None || OldName == NewName) return false;
	if (FindVariable(NewName)) return false;
	FAnimGraphVariable* Var = FindVariable(OldName);
	if (!Var) return false;
	Var->VariableName = NewName;

	// Rename cascade: 모든 Get 노드와 Transition Rule Property Access 를 새 이름으로 갱신.
	for (FAnimGraphNode& Node : Nodes)
	{
		if (Node.VariableName == OldName) Node.VariableName = NewName;
		for (FAnimGraphTransition& T : Node.Transitions)
		{
			if (T.VariableName == OldName) T.VariableName = NewName;
		}
	}
	BumpVersion();
	return true;
}

FAnimGraphVariable* UAnimGraphAsset::FindVariable(const FName& Name)
{
	if (Name == FName::None) return nullptr;
	for (FAnimGraphVariable& Var : Variables)
	{
		if (Var.VariableName == Name) return &Var;
	}
	return nullptr;
}

const FAnimGraphVariable* UAnimGraphAsset::FindVariable(const FName& Name) const
{
	if (Name == FName::None) return nullptr;
	for (const FAnimGraphVariable& Var : Variables)
	{
		if (Var.VariableName == Name) return &Var;
	}
	return nullptr;
}

FAnimGraphNode* UAnimGraphAsset::AddNode(EAnimGraphNodeType Type, const FName& DisplayName, float X, float Y)
{
	FAnimGraphNode Node;
	Node.NodeId      = AllocateId();
	Node.Type        = Type;
	Node.DisplayName = DisplayName;
	Node.PosX        = X;
	Node.PosY        = Y;
	Nodes.push_back(std::move(Node));
	BumpVersion();
	return &Nodes.back();
}

FAnimGraphPin* UAnimGraphAsset::AddPin(FAnimGraphNode& Node, EAnimGraphPinKind Kind, EAnimGraphPinType PinType, const FName& DisplayName)
{
	FAnimGraphPin Pin;
	Pin.PinId        = AllocateId();
	Pin.OwningNodeId = Node.NodeId;
	Pin.Kind         = Kind;
	Pin.Type         = PinType;
	Pin.DisplayName  = DisplayName;
	Node.Pins.push_back(std::move(Pin));
	BumpVersion();
	return &Node.Pins.back();
}

FAnimGraphLink* UAnimGraphAsset::AddLink(uint32 FromPinId, uint32 ToPinId)
{
	// UE Blueprint/AnimGraph input pin semantics: one input pin owns at most one upstream link.
	// Dragging a new source into an already-connected input replaces the previous connection
	// instead of creating ambiguous fan-in. Output fan-out remains allowed.
	Links.erase(std::remove_if(Links.begin(), Links.end(),
		[ToPinId](const FAnimGraphLink& Existing)
		{
			return Existing.ToPinId == ToPinId;
		}), Links.end());

	FAnimGraphLink Link;
	Link.LinkId    = AllocateId();
	Link.FromPinId = FromPinId;
	Link.ToPinId   = ToPinId;
	Links.push_back(std::move(Link));
	BumpVersion();
	return &Links.back();
}

FAnimGraphNode* UAnimGraphAsset::AddNodeOfType(EAnimGraphNodeType Type, float X, float Y)
{
	// 타입별 default display 이름 + 핀 레이아웃. 후속 단계의 컴파일러가 같은 핀 이름을
	// 키로 트리 build 에 사용하므로 핀 명세는 stable.
	switch (Type)
	{
		case EAnimGraphNodeType::OutputPose:
		{
			FAnimGraphNode* N = AddNode(Type, FName("Output Pose"), X, Y);
			AddPin(*N, EAnimGraphPinKind::Input, EAnimGraphPinType::Pose, FName("Result"));
			return N;
		}
		case EAnimGraphNodeType::SequencePlayer:
		{
			FAnimGraphNode* N = AddNode(Type, FName("Sequence Player"), X, Y);
			AddPin(*N, EAnimGraphPinKind::Output, EAnimGraphPinType::Pose, FName("Pose"));
			return N;
		}
		case EAnimGraphNodeType::StateMachine:
		{
			FAnimGraphNode* N = AddNode(Type, FName("State Machine"), X, Y);
			AddPin(*N, EAnimGraphPinKind::Output, EAnimGraphPinType::Pose, FName("Pose"));
			return N;
		}
		case EAnimGraphNodeType::Slot:
		{
			FAnimGraphNode* N = AddNode(Type, FName("Slot"), X, Y);
			AddPin(*N, EAnimGraphPinKind::Input,  EAnimGraphPinType::Pose, FName("Source"));
			AddPin(*N, EAnimGraphPinKind::Output, EAnimGraphPinType::Pose, FName("Result"));
			return N;
		}
		case EAnimGraphNodeType::LayeredBlendPerBone:
		{
			FAnimGraphNode* N = AddNode(Type, FName("Layered Blend"), X, Y);
			AddPin(*N, EAnimGraphPinKind::Input,  EAnimGraphPinType::Pose, FName("Base"));
			AddPin(*N, EAnimGraphPinKind::Input,  EAnimGraphPinType::Pose, FName("Blend"));
			AddPin(*N, EAnimGraphPinKind::Output, EAnimGraphPinType::Pose, FName("Result"));
			return N;
		}
		case EAnimGraphNodeType::BlendListByEnum:
		{
			// V1 — Selector 는 Float (Speed 같은 변수 직결 위해). 노드 안에서 (int)floor + clamp
			// 로 InputPose 인덱스 결정. 실제 enum-driven UE 동작과는 차이.
			FAnimGraphNode* N = AddNode(Type, FName("Blend List"), X, Y);
			AddPin(*N, EAnimGraphPinKind::Input,  EAnimGraphPinType::Float, FName("Selector"));
			AddPin(*N, EAnimGraphPinKind::Input,  EAnimGraphPinType::Pose,  FName("A"));
			AddPin(*N, EAnimGraphPinKind::Input,  EAnimGraphPinType::Pose,  FName("B"));
			AddPin(*N, EAnimGraphPinKind::Output, EAnimGraphPinType::Pose,  FName("Result"));
			return N;
		}
		case EAnimGraphNodeType::VariableGet:
		{
			// V1 — output 타입 항상 Float. Bool/Int 변수 선택 시 컴파일러가 0/1 또는 (float)int 로 cast.
			FAnimGraphNode* N = AddNode(Type, FName("Variable"), X, Y);
			AddPin(*N, EAnimGraphPinKind::Output, EAnimGraphPinType::Float, FName("Value"));
			return N;
		}
		case EAnimGraphNodeType::RefPose:
		{
			FAnimGraphNode* N = AddNode(Type, FName("Ref Pose"), X, Y);
			AddPin(*N, EAnimGraphPinKind::Output, EAnimGraphPinType::Pose, FName("Pose"));
			return N;
		}
	}
	return nullptr;
}

bool UAnimGraphAsset::RemoveNode(uint32 NodeId)
{
	if (NodeId == 0) return false;

	// 노드의 핀 id 들을 먼저 수집 → 그 핀이 from/to 에 사용된 모든 링크 cascade 제거.
	TArray<uint32> PinIds;
	for (const FAnimGraphNode& Node : Nodes)
	{
		if (Node.NodeId != NodeId) continue;
		PinIds.reserve(Node.Pins.size());
		for (const FAnimGraphPin& Pin : Node.Pins) PinIds.push_back(Pin.PinId);
		break;
	}

	if (PinIds.empty())
	{
		// 노드를 못 찾았으면 아무것도 안 함.
		const bool bFound = std::any_of(Nodes.begin(), Nodes.end(),
			[NodeId](const FAnimGraphNode& N) { return N.NodeId == NodeId; });
		if (!bFound) return false;
	}

	Links.erase(std::remove_if(Links.begin(), Links.end(),
		[&PinIds](const FAnimGraphLink& L)
		{
			for (uint32 P : PinIds)
			{
				if (L.FromPinId == P || L.ToPinId == P) return true;
			}
			return false;
		}), Links.end());

	Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(),
		[NodeId](const FAnimGraphNode& N) { return N.NodeId == NodeId; }), Nodes.end());

	// State.SubGraphNodeId is an explicit node-id reference. Clear every state that pointed to
	// the removed node so deleting a nested State Machine cannot leave a dangling runtime ref.
	for (FAnimGraphNode& Node : Nodes)
	{
		if (Node.Type != EAnimGraphNodeType::StateMachine) continue;
		for (FAnimGraphState& State : Node.States)
		{
			if (State.SubGraphNodeId == NodeId)
			{
				State.SubGraphNodeId = 0;
			}
		}
	}

	BumpVersion();
	return true;
}

bool UAnimGraphAsset::RemoveLink(uint32 LinkId)
{
	if (LinkId == 0) return false;
	const size_t Before = Links.size();
	Links.erase(std::remove_if(Links.begin(), Links.end(),
		[LinkId](const FAnimGraphLink& L) { return L.LinkId == LinkId; }), Links.end());
	const bool bRemoved = Links.size() != Before;
	if (bRemoved) BumpVersion();
	return bRemoved;
}

bool UAnimGraphAsset::CanLinkPins(uint32 PinAId, uint32 PinBId, uint32* OutFromPinId, uint32* OutToPinId) const
{
	if (PinAId == 0 || PinBId == 0 || PinAId == PinBId) return false;

	const FAnimGraphPin* A = FindPin(PinAId);
	const FAnimGraphPin* B = FindPin(PinBId);
	if (!A || !B) return false;

	// 같은 노드 내 핀 끼리 연결 금지.
	if (A->OwningNodeId == B->OwningNodeId) return false;

	// Kind 가 반대여야 함 — 양쪽 다 input 또는 양쪽 다 output 이면 거부.
	if (A->Kind == B->Kind) return false;

	// 타입 일치 — Pose-Pose, Float-Float 등.
	if (A->Type != B->Type) return false;

	// 방향 정규화 — 드래그 방향에 무관하게 from=Output, to=Input.
	const FAnimGraphPin* From = (A->Kind == EAnimGraphPinKind::Output) ? A : B;
	const FAnimGraphPin* To   = (From == A) ? B : A;

	// 중복 링크 거부. 다른 output 이 같은 input 에 연결된 경우는 AddLink 에서 기존 input 링크를
	// 교체한다. 이것이 UE 그래프의 단일 input-pin 연결 규칙과 가장 가깝다.
	for (const FAnimGraphLink& L : Links)
	{
		if (L.FromPinId == From->PinId && L.ToPinId == To->PinId) return false;
	}

	if (OutFromPinId) *OutFromPinId = From->PinId;
	if (OutToPinId)   *OutToPinId   = To->PinId;
	return true;
}

bool UAnimGraphAsset::HasOutputPoseNode() const
{
	for (const FAnimGraphNode& N : Nodes)
	{
		if (N.Type == EAnimGraphNodeType::OutputPose) return true;
	}
	return false;
}

void UAnimGraphAsset::InitializeDefault()
{
	Nodes.clear();
	Links.clear();
	Variables.clear();
	NextId = 1;

	// ⚠ Nodes 가 std::vector — 후속 AddNode 호출이 reallocation 을 일으키면 이전에 받은
	// raw pointer 가 invalidate 됨. id 만 즉시 캡쳐하고 pointer 는 버린다.
	uint32 SourceOutId = 0;
	if (FAnimGraphNode* Source = AddNodeOfType(EAnimGraphNodeType::SequencePlayer, -240.0f, 0.0f))
	{
		if (!Source->Pins.empty()) SourceOutId = Source->Pins.front().PinId;
	}

	uint32 SinkInId = 0;
	if (FAnimGraphNode* Sink = AddNodeOfType(EAnimGraphNodeType::OutputPose, 0.0f, 0.0f))
	{
		if (!Sink->Pins.empty()) SinkInId = Sink->Pins.front().PinId;
	}

	if (SourceOutId && SinkInId)
	{
		AddLink(SourceOutId, SinkInId);
	}
}

FAnimGraphNode* UAnimGraphAsset::FindNode(uint32 NodeId)
{
	if (NodeId == 0) return nullptr;
	for (FAnimGraphNode& Node : Nodes)
	{
		if (Node.NodeId == NodeId) return &Node;
	}
	return nullptr;
}

const FAnimGraphNode* UAnimGraphAsset::FindNode(uint32 NodeId) const
{
	if (NodeId == 0) return nullptr;
	for (const FAnimGraphNode& Node : Nodes)
	{
		if (Node.NodeId == NodeId) return &Node;
	}
	return nullptr;
}

FAnimGraphNode* UAnimGraphAsset::FindFirstNodeOfType(EAnimGraphNodeType Type)
{
	for (FAnimGraphNode& Node : Nodes)
	{
		if (Node.Type == Type) return &Node;
	}
	return nullptr;
}

FAnimGraphPin* UAnimGraphAsset::FindPin(uint32 PinId)
{
	if (PinId == 0) return nullptr;
	for (FAnimGraphNode& Node : Nodes)
	{
		for (FAnimGraphPin& Pin : Node.Pins)
		{
			if (Pin.PinId == PinId) return &Pin;
		}
	}
	return nullptr;
}

const FAnimGraphPin* UAnimGraphAsset::FindPin(uint32 PinId) const
{
	if (PinId == 0) return nullptr;
	for (const FAnimGraphNode& Node : Nodes)
	{
		for (const FAnimGraphPin& Pin : Node.Pins)
		{
			if (Pin.PinId == PinId) return &Pin;
		}
	}
	return nullptr;
}

void UAnimGraphAsset::Serialize(FArchive& Ar)
{
	// UObject::Serialize 호출 안 함 — 다른 자산(UFloatCurveAsset 등)과 동일 패턴 (자산 패키지
	// 컨텍스트에서 ObjectName 직렬화 불필요).
	if (Ar.IsSaving())
	{
		uint32 Magic = kAnimGraphAssetMagic;
		uint32 Version = kAnimGraphAssetVersion;
		Ar << Magic;
		Ar << Version;
		Ar << NextId;
		Ar << Nodes;
		Ar << Links;
		Ar << OwnerClassName;
		Ar << Variables;
		return;
	}

	uint32 First = 0;
	Ar << First;
	uint32 Version = 1;
	if (First == kAnimGraphAssetMagic)
	{
		Ar << Version;
		Ar << NextId;
	}
	else
	{
		// Legacy v1 files started directly with NextId. Treat the first word as that field.
		NextId = First;
	}

	const bool bLegacyTransitions = Version < 2;
	FLegacyTransitionFormatScope LegacyTransitionScope(bLegacyTransitions);
	Ar << Nodes;
	Ar << Links;
	Ar << OwnerClassName;
	if (Version >= 3)
	{
		Ar << Variables;
	}
	else
	{
		Variables.clear();
	}

	// Loaded assets can come from older editor versions or hand-edited files. Normalize explicit
	// id/name references here too, not only in the editor, so runtime compilation never starts
	// from dangling pins, stale transitions, duplicate input fan-in, or invalid nested SM refs.
	std::unordered_set<uint32> PinIds;
	for (const FAnimGraphNode& Node : Nodes)
	{
		for (const FAnimGraphPin& Pin : Node.Pins) PinIds.insert(Pin.PinId);
	}

	std::unordered_map<uint32, uint32> LastLinkForInputPin;
	Links.erase(std::remove_if(Links.begin(), Links.end(),
		[&](const FAnimGraphLink& Link)
		{
			const FAnimGraphPin* From = FindPin(Link.FromPinId);
			const FAnimGraphPin* To   = FindPin(Link.ToPinId);
			if (!From || !To || !PinIds.count(Link.FromPinId) || !PinIds.count(Link.ToPinId)) return true;
			if (From->Kind != EAnimGraphPinKind::Output || To->Kind != EAnimGraphPinKind::Input || From->Type != To->Type) return true;
			auto It = LastLinkForInputPin.find(To->PinId);
			if (It != LastLinkForInputPin.end()) return true;
			LastLinkForInputPin[To->PinId] = Link.LinkId;
			return false;
		}), Links.end());

	auto FindStateIndexLocal = [](const TArray<FAnimGraphState>& States, FName Name) -> int32
	{
		for (int32 i = 0; i < static_cast<int32>(States.size()); ++i)
		{
			if (States[i].StateName == Name) return i;
		}
		return -1;
	};

	auto IsValidStateMachineNodeId = [&](uint32 NodeId) -> bool
	{
		if (NodeId == 0) return false;
		const FAnimGraphNode* Node = FindNode(NodeId);
		return Node && Node->Type == EAnimGraphNodeType::StateMachine;
	};

	for (FAnimGraphNode& Node : Nodes)
	{
		if (Node.Type != EAnimGraphNodeType::StateMachine) continue;

		if (!Node.States.empty() && FindStateIndexLocal(Node.States, Node.InitialStateName) < 0)
		{
			Node.InitialStateName = Node.States.front().StateName;
		}
		else if (Node.States.empty())
		{
			Node.InitialStateName = FName::None;
		}

		for (FAnimGraphState& State : Node.States)
		{
			if (State.SubGraphNodeId == Node.NodeId || (State.SubGraphNodeId != 0 && !IsValidStateMachineNodeId(State.SubGraphNodeId)))
			{
				State.SubGraphNodeId = 0;
			}
		}

		std::unordered_set<std::string> SeenTransitions;
		Node.Transitions.erase(std::remove_if(Node.Transitions.begin(), Node.Transitions.end(),
			[&](const FAnimGraphTransition& T)
			{
				const bool bMissingTo = T.ToStateName == FName::None || FindStateIndexLocal(Node.States, T.ToStateName) < 0;
				const bool bMissingFrom = T.FromStateName != FName::None && FindStateIndexLocal(Node.States, T.FromStateName) < 0;
				if (bMissingTo || bMissingFrom) return true;
				const FString Key = (T.FromStateName == FName::None ? FString("<Any>") : T.FromStateName.ToString()) + "->" + T.ToStateName.ToString();
				if (SeenTransitions.count(Key)) return true;
				SeenTransitions.insert(Key);
				return false;
			}), Node.Transitions.end());
	}
}


void UAnimGraphAsset::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);
	for (FAnimGraphNode& Node : Nodes)
	{
		if (Node.SequenceRef && !IsValid(Node.SequenceRef))
		{
			Node.SequenceRef = nullptr;
		}
		Collector.AddReferencedObject(Node.SequenceRef, "AnimGraphAsset.Node.SequenceRef");
	}
}
