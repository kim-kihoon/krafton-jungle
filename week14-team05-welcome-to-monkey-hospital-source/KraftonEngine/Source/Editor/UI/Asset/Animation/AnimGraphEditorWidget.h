#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Animation/Graph/AnimGraphTypes.h"

#include "imgui.h" // ImVec2 — popup 위치 캐시용.

namespace ax { namespace NodeEditor { struct EditorContext; } }

// UAnimGraphAsset 의 시각 노드 그래프 에디터.
// imgui-node-editor 캔버스에 자산 데이터 모델을 그리고 편집 (노드 추가/삭제, 링크 생성/삭제,
// 노드 위치 드래그 동기화) 까지 담당. 컴파일 → 런타임 트리는 후속 단계.
class FAnimGraphEditorWidget : public FAssetEditorWidget
{
public:
	FAnimGraphEditorWidget() = default;
	~FAnimGraphEditorWidget() override;

	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object)    override;
	void Close()                  override;
	void Render(float DeltaTime)  override;
	void RenderDocument(float DeltaTime) override;
	bool AllowsMultipleInstances() const override { return true; }
	bool IsEditingObject(UObject* Object) const override;
	FString GetDocumentTitle() const override;
	FString GetDocumentPayloadId() const override;
	EEditorDocumentTabKind GetDocumentTabKind() const override { return EEditorDocumentTabKind::AnimGraphEditor; }

private:
	enum class EViewMode : uint8
	{
		RootAnimGraph,
		StateMachine,
		StatePose,
		TransitionRule
	};

	void EnsureContext();
	void DestroyContext();

	void CaptureInitialUndoSnapshot();
	void CommitGraphEdit(class UAnimGraphAsset* Asset);
	void UndoGraphEdit(class UAnimGraphAsset* Asset);
	void RedoGraphEdit(class UAnimGraphAsset* Asset);
	TArray<uint8> MakeGraphSnapshot(class UAnimGraphAsset* Asset) const;
	bool RestoreGraphSnapshot(class UAnimGraphAsset* Asset, const TArray<uint8>& Snapshot);

	bool GatherSelectedNodes(class UAnimGraphAsset* Asset, TArray<struct FAnimGraphNode>& OutNodes, TArray<struct FAnimGraphLink>& OutLinks) const;
	bool CloneNodeFragment(class UAnimGraphAsset* Asset, const TArray<struct FAnimGraphNode>& SourceNodes, const TArray<struct FAnimGraphLink>& SourceLinks, const ImVec2& TargetAnchor, TArray<uint32>* OutNewNodeIds = nullptr);
	void SelectOnlyNodes(const TArray<uint32>& NodeIds);
	void CopySelectedNodes(class UAnimGraphAsset* Asset);
	void PasteCopiedNodes(class UAnimGraphAsset* Asset, const ImVec2* OverrideAnchor = nullptr);
	void DuplicateSelectedNodes(class UAnimGraphAsset* Asset);
	void DeleteSelectedNodes(class UAnimGraphAsset* Asset);
	void QueueRootGraphShortcuts(class UAnimGraphAsset* Asset);
	void ProcessQueuedRootGraphCommands(class UAnimGraphAsset* Asset);
	void ValidateGraph(class UAnimGraphAsset* Asset);
	void SanitizeGraphReferences(class UAnimGraphAsset* Asset);
	bool RenderAnimBlueprintNavigator(class UAnimGraphAsset& Asset, uint32 CurrentStateMachineNodeId, int32 CurrentStateIndex, int32 CurrentTransitionIndex, float Height);
	void RenderPinSpawnMenu(class UAnimGraphAsset* Asset);

	ax::NodeEditor::EditorContext* NodeEditorContext = nullptr;

	// 첫 프레임에 데이터 모델의 좌표를 ed::SetNodePosition 으로 push 했는지.
	// Open 마다 false 로 리셋 — 같은 위젯 인스턴스가 다른 자산을 받았을 때 재초기화.
	bool bPositionsPushed = false;

	// 배경 우클릭 시 캡쳐한 캔버스 좌표 — 같은 프레임의 BeginPopup 안에서 신규 노드 spawn 위치로 사용.
	ImVec2 PendingNewNodePosition = ImVec2(0, 0);

	// StateMachine 노드 내부 편집 모드. UE 처럼 StateMachine 노드를 열면 Entry/State/Transition
	// 그래프를 별도 캔버스로 보여준다. 런타임 데이터 모델은 기존 Node.States / Transitions 를 그대로 사용.
	uint32 OpenStateMachineNodeId = 0;
	bool   bStateMachinePositionsPushed = false;
	bool   bCreateReverseTransitionOnDrag = false;
	EViewMode ViewMode = EViewMode::RootAnimGraph;
	int32 OpenStateIndex = -1;
	int32 OpenTransitionIndex = -1;

	void RenderStateMachineEditor(class UAnimGraphAsset& Asset, struct FAnimGraphNode& StateMachineNode, float AvailableHeight);
	void RenderStatePoseEditor(class UAnimGraphAsset& Asset, struct FAnimGraphNode& StateMachineNode, int32 StateIndex, float AvailableHeight);
	void RenderTransitionRuleEditor(class UAnimGraphAsset& Asset, struct FAnimGraphNode& StateMachineNode, int32 TransitionIndex, float AvailableHeight);

	char AddNodeSearchBuf[128] = {};
	char PinSpawnSearchBuf[128] = {};
	bool   bShowPinSpawnMenu    = false;
	uint32 PendingPinSpawnPinId = 0;
	ImVec2 PendingNewNodeScreenPosition = ImVec2(0, 0);

	TArray<TArray<uint8>> UndoStack;
	TArray<TArray<uint8>> RedoStack;
	TArray<struct FAnimGraphNode> ClipboardNodes;
	TArray<struct FAnimGraphLink> ClipboardLinks;
	bool bRestoringSnapshot = false;

	bool bQueuedCopySelected      = false;
	bool bQueuedPasteNodes        = false;
	bool bQueuedDuplicateSelected = false;
	bool bQueuedDeleteSelected    = false;

	TArray<FString> LastValidationMessages;
	bool bLastValidationOk = true;

	bool bRenderingDocument = false;
};
