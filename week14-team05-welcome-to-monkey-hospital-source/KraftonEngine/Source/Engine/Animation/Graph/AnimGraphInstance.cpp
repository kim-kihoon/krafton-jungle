#include "AnimGraphInstance.h"

#include "Animation/Graph/AnimGraphAsset.h"
#include "Animation/Graph/AnimGraphCompiler.h"
#include "Animation/Graph/AnimGraphTypes.h"
#include "Animation/Graph/AnimGraphManager.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/AnimationManager.h"
#include "Animation/Nodes/AnimNode_Base.h"
#include "Core/Logging/Log.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Serialization/Archive.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"

#include <algorithm>
#include <cmath>

namespace
{
	UAnimSequenceBase* LoadByPath(const FString& Path)
	{
		if (Path.empty() || Path == "None") return nullptr;
		UAnimSequenceBase* Loaded = FAnimationManager::Get().LoadAnimation(Path);
		if (!IsValid(Loaded))
		{
			Loaded = nullptr;
		}
		if (!Loaded)
		{
			UE_LOG("UAnimGraphInstance: 시퀀스 로드 실패. Path=%s", Path.c_str());
		}
		return Loaded;
	}
}

void UAnimGraphInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh) return;
	FSkeletalMesh* MeshAsset = Mesh->GetSkeletalMeshAsset();
	if (!MeshAsset || MeshAsset->Bones.empty()) return;

	// GraphAsset resolve 우선순위: 외부 SetGraphAsset > GraphAssetPath 로 디스크 로드 > 자동 transient.
	if (GraphAsset && !IsValid(GraphAsset))
	{
		GraphAsset = nullptr;
		CompiledAssetVersion = 0;
	}
	if (!GraphAsset)
	{
		const FString GraphPathStr = GraphAssetPath.ToString();
		if (!GraphPathStr.empty() && GraphPathStr != "None")
		{
			GraphAsset = FAnimGraphManager::Get().Load(GraphPathStr);
			if (!GraphAsset)
			{
				UE_LOG("UAnimGraphInstance: AnimGraph 로드 실패 → transient fallback. Path=%s", GraphPathStr.c_str());
			}
		}
	}
	if (!GraphAsset)
	{
		GraphAsset = UObjectManager::Get().CreateObject<UAnimGraphAsset>(this);
		if (GraphAsset)
		{
			GraphAsset->InitializeDefault();
		}
	}

	if (!IsValid(GraphAsset))
	{
		GraphAsset = nullptr;
		RootNode = nullptr;
		OwnedNodes.clear();
		CompiledAssetVersion = 0;
		return;
	}

	// AnimGraph-owned 변수는 자산 기본값에서 per-instance runtime storage 로 초기화한다.
	SyncRuntimeVariablesFromAsset(/*bResetExistingToDefaults*/true);

	// Version 강제 mismatch 로 첫 컴파일 트리거. (자산이 fresh load 라면 Version 이 0 이지만,
	// 다른 인스턴스가 이미 변경해 Version > 0 인 캐시 자산일 수도 있음 — 안전하게 강제.)
	CompiledAssetVersion = GraphAsset->GetVersion() - 1;
	RecompileTreeIfDirty();
}

void UAnimGraphInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// in-editor live preview: 자산이 변경되면 다음 frame UpdateAnimation 의 RootNode->Update
	// 호출 전에 트리 재생성. 새 트리는 즉시 그 frame 부터 평가.
	RecompileTreeIfDirty();
}

void UAnimGraphInstance::RecompileTreeIfDirty()
{
	if (!IsValid(GraphAsset))
	{
		GraphAsset = nullptr;
		RootNode = nullptr;
		OwnedNodes.clear();
		CompiledAssetVersion = 0;
		return;
	}
	if (GraphAsset->GetVersion() == CompiledAssetVersion) return;

	// Recompile 때 새로 추가된 변수는 기본값으로 채우고, 이미 게임 코드가 set 한 값은 유지한다.
	SyncRuntimeVariablesFromAsset(/*bResetExistingToDefaults*/false);

	// 노드별 SequenceRef 재해상 (per-node SequencePath > Instance.DefaultSequencePath > nullptr).
	const FString DefaultPathStr = DefaultSequencePath.ToString();
	for (FAnimGraphNode& Node : const_cast<TArray<FAnimGraphNode>&>(GraphAsset->GetNodes()))
	{
		if (Node.Type != EAnimGraphNodeType::SequencePlayer) continue;

		UAnimSequenceBase* Seq = LoadByPath(Node.SequencePath);
		if (!Seq) Seq = LoadByPath(DefaultPathStr);
		Node.SequenceRef = IsValid(Seq) ? Seq : nullptr;
	}

	// 기존 트리 폐기 — RootNode 먼저 nullptr 로 끊은 뒤 OwnedNodes clear.
	// (Update 호출 chain 이 다음 줄에 들어가므로 dangling 노출 위험 없음.)
	RootNode = nullptr;
	OwnedNodes.clear();

	FAnimNode_Base* Root = FAnimGraphCompiler::Compile(*GraphAsset, *this);
	if (!Root)
	{
		UE_LOG("UAnimGraphInstance: 컴파일 실패 — 트리 미설정, ref pose 유지.");
		CompiledAssetVersion = GraphAsset->GetVersion();
		return;
	}
	SetRootNode(Root);
	CompiledAssetVersion = GraphAsset->GetVersion();
}

void UAnimGraphInstance::Serialize(FArchive& Ar)
{
	// Editor-set 데모 파라미터만 — 런타임 GraphAsset 포인터는 transient (다음 InitializeAnimation
	// 에서 path 로 재해상). PIE Duplicate (UObject::Duplicate = Serialize 왕복) 가 path 들만 라운드트립.
	// UCharacterAnimInstance 와 동일하게 Super::Serialize 호출 안 함 (ObjectName 직렬화 skip).
	FString SeqPathStr   = Ar.IsSaving() ? DefaultSequencePath.ToString() : FString();
	FString GraphPathStr = Ar.IsSaving() ? GraphAssetPath.ToString()      : FString();
	Ar << SeqPathStr;
	Ar << GraphPathStr;
	if (Ar.IsLoading())
	{
		DefaultSequencePath = FSoftObjectPtr(SeqPathStr);
		GraphAssetPath      = FSoftObjectPtr(GraphPathStr);
	}
}


UAnimGraphAsset* UAnimGraphInstance::GetGraphAsset() const
{
	return IsValid(GraphAsset) ? GraphAsset : nullptr;
}

void UAnimGraphInstance::SetGraphAsset(UAnimGraphAsset* InAsset)
{
	GraphAsset = IsValid(InAsset) ? InAsset : nullptr;
	CompiledAssetVersion = 0;
	RootNode = nullptr;
	OwnedNodes.clear();
	SyncRuntimeVariablesFromAsset(/*bResetExistingToDefaults*/true);
}

FAnimGraphRuntimeVariable* UAnimGraphInstance::FindRuntimeVariable(FName VariableName)
{
	if (VariableName == FName::None) return nullptr;
	for (FAnimGraphRuntimeVariable& V : RuntimeVariables)
	{
		if (V.VariableName == VariableName) return &V;
	}
	return nullptr;
}

const FAnimGraphRuntimeVariable* UAnimGraphInstance::FindRuntimeVariable(FName VariableName) const
{
	if (VariableName == FName::None) return nullptr;
	for (const FAnimGraphRuntimeVariable& V : RuntimeVariables)
	{
		if (V.VariableName == VariableName) return &V;
	}
	return nullptr;
}

void UAnimGraphInstance::SyncRuntimeVariablesFromAsset(bool bResetExistingToDefaults)
{
	if (!IsValid(GraphAsset))
	{
		RuntimeVariables.clear();
		return;
	}

	// Asset 에서 삭제된 변수는 runtime 에서도 제거.
	RuntimeVariables.erase(std::remove_if(RuntimeVariables.begin(), RuntimeVariables.end(),
		[this](const FAnimGraphRuntimeVariable& RuntimeVar)
		{
			return !GraphAsset || !GraphAsset->FindVariable(RuntimeVar.VariableName);
		}), RuntimeVariables.end());

	for (const FAnimGraphVariable& Decl : GraphAsset->GetVariables())
	{
		if (Decl.VariableName == FName::None) continue;
		FAnimGraphRuntimeVariable* Existing = FindRuntimeVariable(Decl.VariableName);
		if (!Existing)
		{
			FAnimGraphRuntimeVariable RuntimeVar;
			RuntimeVar.VariableName = Decl.VariableName;
			RuntimeVar.Type = Decl.Type;
			RuntimeVar.Value = Decl.DefaultValue;
			RuntimeVariables.push_back(RuntimeVar);
		}
		else
		{
			Existing->Type = Decl.Type;
			if (bResetExistingToDefaults)
			{
				Existing->Value = Decl.DefaultValue;
			}
		}
	}
}

bool UAnimGraphInstance::SetGraphVariableFloat(FName VariableName, float Value)
{
	SyncRuntimeVariablesFromAsset(/*bResetExistingToDefaults*/false);
	FAnimGraphRuntimeVariable* V = FindRuntimeVariable(VariableName);
	if (!V) return false;
	V->Value = Value;
	return true;
}

bool UAnimGraphInstance::SetGraphVariableBool(FName VariableName, bool bValue)
{
	SyncRuntimeVariablesFromAsset(/*bResetExistingToDefaults*/false);
	FAnimGraphRuntimeVariable* V = FindRuntimeVariable(VariableName);
	if (!V) return false;
	V->Value = bValue ? 1.0f : 0.0f;
	return true;
}

bool UAnimGraphInstance::SetGraphVariableInt(FName VariableName, int32 Value)
{
	SyncRuntimeVariablesFromAsset(/*bResetExistingToDefaults*/false);
	FAnimGraphRuntimeVariable* V = FindRuntimeVariable(VariableName);
	if (!V) return false;
	V->Value = static_cast<float>(Value);
	return true;
}

bool UAnimGraphInstance::GetGraphVariableAsFloat(FName VariableName, float& OutValue) const
{
	const FAnimGraphRuntimeVariable* V = FindRuntimeVariable(VariableName);
	if (!V) return false;
	OutValue = V->Value;
	return true;
}

bool UAnimGraphInstance::GetGraphVariableFloat(FName VariableName, float& OutValue) const
{
	return GetGraphVariableAsFloat(VariableName, OutValue);
}

bool UAnimGraphInstance::GetGraphVariableBool(FName VariableName, bool& bOutValue) const
{
	float V = 0.0f;
	if (!GetGraphVariableAsFloat(VariableName, V)) return false;
	bOutValue = V >= 0.5f;
	return true;
}

bool UAnimGraphInstance::GetGraphVariableInt(FName VariableName, int32& OutValue) const
{
	float V = 0.0f;
	if (!GetGraphVariableAsFloat(VariableName, V)) return false;
	OutValue = static_cast<int32>(std::floor(V + 0.5f));
	return true;
}

void UAnimGraphInstance::AddReferencedObjects(FReferenceCollector& Collector)
{
	UAnimInstance::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(GraphAsset, "AnimGraphInstance.GraphAsset");
}

void UAnimGraphInstance::BeginDestroy()
{
	GraphAsset = nullptr;
	CompiledAssetVersion = 0;
	UAnimInstance::BeginDestroy();
}
