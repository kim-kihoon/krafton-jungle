#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"

#include "imgui.h"
#include "LuaBlueprint/LuaBlueprintTypes.h"

namespace ax::NodeEditor { struct EditorContext; }

class ULuaBlueprintAsset;
class UClass;
struct FFunction;
struct FLuaBlueprintNode;
struct FLuaBlueprintPin;
struct FLuaBlueprintVariable;

class FLuaBlueprintEditorWidget : public FAssetEditorWidget
{
public:
	FLuaBlueprintEditorWidget() = default;
	~FLuaBlueprintEditorWidget() override;

	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Close() override;
	void Render(float DeltaTime) override;
	void RenderDocument(float DeltaTime) override;
	bool AllowsMultipleInstances() const override { return true; }
	bool IsEditingObject(UObject* Object) const override;
	FString GetDocumentTitle() const override;
	FString GetDocumentPayloadId() const override;
	EEditorDocumentTabKind GetDocumentTabKind() const override { return EEditorDocumentTabKind::LuaBlueprintEditor; }

private:
	void EnsureContext();
	void DestroyContext();

	ULuaBlueprintAsset* GetBlueprint() const;
	void RenderToolbar(ULuaBlueprintAsset* Blueprint);
	void RenderCompileErrorPanel(ULuaBlueprintAsset* Blueprint);
	void RenderLeftPanel(ULuaBlueprintAsset* Blueprint);
	void RenderVariables(ULuaBlueprintAsset* Blueprint);
	void RenderPalettePanel(ULuaBlueprintAsset* Blueprint);
	void SpawnPaletteNode(ULuaBlueprintAsset* Blueprint, ELuaBlueprintNodeType Type);
	void RenderGraph(ULuaBlueprintAsset* Blueprint);
	void RenderNodeBody(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node);
	void RenderNodePins(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node);
	void RenderNodeInputPin(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node, FLuaBlueprintPin& Pin);
	void RenderNodeOutputPin(ULuaBlueprintAsset* Blueprint, FLuaBlueprintPin& Pin);
	void RenderNodeInspector(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node);
	void RenderVariableEditor(ULuaBlueprintAsset* Blueprint, FLuaBlueprintVariable& Variable, int32 Index);
	void RenderDiagnostics(ULuaBlueprintAsset* Blueprint);
	void RenderGeneratedLua(ULuaBlueprintAsset* Blueprint);
	void OpenCustomLuaFunctionEditor(ULuaBlueprintAsset* Blueprint, uint32 NodeId);
	void RenderCustomLuaFunctionEditor(ULuaBlueprintAsset* Blueprint);
	void SyncBreakpointsToDebugManager(ULuaBlueprintAsset* Blueprint);
	void ProcessLuaDebugAutoFocus(ULuaBlueprintAsset* Blueprint);
	void QueueNavigateToNode(uint32 NodeId);

	bool RenderInlinePinLiteral(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node, FLuaBlueprintPin& Pin);
	void OpenInlineAssetPicker(const FLuaBlueprintNode& Node, const FLuaBlueprintPin& Pin, const char* AssetType);
	void RenderDeferredInlineAssetPicker(ULuaBlueprintAsset* Blueprint);
	bool ApplyInlineAssetPickerSelection(ULuaBlueprintAsset* Blueprint, const FString& SelectedPath);

	bool AddNodeMenuItem(ULuaBlueprintAsset* Blueprint, ELuaBlueprintNodeType Type);
	FLuaBlueprintNode* SpawnReflectedFunctionNode(ULuaBlueprintAsset* Blueprint, const FFunction* Function, const ImVec2& Position, bool bSelectNode);
	bool AddReflectedFunctionNodeMenuItem(ULuaBlueprintAsset* Blueprint, const UClass* Class, const FFunction* Function, const char* SearchQuery, const ImVec2& Position, bool bSelectNode);
	void RenderReflectedFunctionNodeMenu(ULuaBlueprintAsset* Blueprint, const char* SearchQuery, const ImVec2& Position, bool bFlatList, bool bSelectNode);
	bool AddVariableMenuItem(ULuaBlueprintAsset* Blueprint, ELuaBlueprintPinType Type, const char* Label);
	void RenderAddNodeMenu(ULuaBlueprintAsset* Blueprint);
	void RenderPinSpawnMenu(ULuaBlueprintAsset* Blueprint);
	bool AddContextNodeMenuItem(ULuaBlueprintAsset* Blueprint, ELuaBlueprintNodeType Type);
	bool NodeTypeCanConnectToPendingPin(ELuaBlueprintNodeType Type) const;
	FLuaBlueprintPin* FindFirstCompatiblePinOnNode(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node, const FLuaBlueprintPin& DragPin) const;

	// 변수 패널에서 캔버스 위로 drag → drop 시 Get/Set 팝업.
	void HandleVariableDropOnCanvas();
	void RenameVariableCascade(ULuaBlueprintAsset* Blueprint, const FName& OldName, const FName& NewName);
	void RenameCustomLuaFunctionCascade(ULuaBlueprintAsset* Blueprint, const FName& OldName, const FName& NewName);
	void SpawnVariableNode(ULuaBlueprintAsset* Blueprint, ELuaBlueprintNodeType Type, const FName& VariableName, const ImVec2& Position);

	// 에디터 대형 기능: snapshot 기반 Undo/Redo 와 선택 노드 copy/paste.
	void CaptureInitialUndoSnapshot(ULuaBlueprintAsset* Blueprint);
	void CommitBlueprintEdit(ULuaBlueprintAsset* Blueprint);
	void UndoBlueprintEdit(ULuaBlueprintAsset* Blueprint);
	void RedoBlueprintEdit(ULuaBlueprintAsset* Blueprint);
	bool RestoreBlueprintSnapshot(ULuaBlueprintAsset* Blueprint, const TArray<uint8>& Snapshot);
	bool GatherSelectedNodes(ULuaBlueprintAsset* Blueprint, TArray<FLuaBlueprintNode>& OutNodes, TArray<FLuaBlueprintLink>& OutLinks) const;
	bool CloneNodeFragment(ULuaBlueprintAsset* Blueprint, const TArray<FLuaBlueprintNode>& SourceNodes, const TArray<FLuaBlueprintLink>& SourceLinks, const ImVec2& TargetAnchor, TArray<uint32>* OutNewNodeIds = nullptr, const ImVec2* SourceAnchorOverride = nullptr);
	void SelectOnlyNodes(const TArray<uint32>& NodeIds);
	void CopySelectedNodes(ULuaBlueprintAsset* Blueprint);
	void PasteCopiedNodes(ULuaBlueprintAsset* Blueprint, const ImVec2* OverrideAnchor = nullptr);
	void DeleteSelectedNodes(ULuaBlueprintAsset* Blueprint);
	bool DeleteNodesIncludingContainedGroups(ULuaBlueprintAsset* Blueprint, const TArray<uint32>& RootNodeIds);
	void DuplicateSelectedNodes(ULuaBlueprintAsset* Blueprint);
	// 선택된 노드들의 bounding box 를 감싸는 Comment(Group) 노드 생성.
	void GroupSelectedNodesAsComment(ULuaBlueprintAsset* Blueprint);
	void ProcessQueuedNodeEditorCommands(ULuaBlueprintAsset* Blueprint);
	void RemoveVariableCascade(ULuaBlueprintAsset* Blueprint, const FName& VariableName);
	void RenderInputPinConnectionStatus(ULuaBlueprintAsset* Blueprint, const FLuaBlueprintPin& Pin);

private:
	ax::NodeEditor::EditorContext* NodeEditorContext = nullptr;
	bool   bPositionsPushed = false;
	ImVec2 PendingNewNodePosition = ImVec2(0, 0);

	char AddNodeSearchBuf[64] = {};
	char PaletteSearchBuf[64] = {};

	// 툴바 "Add Node" 버튼이 띄우는 검색형 노드 메뉴 팝업 트리거.
	bool bOpenToolbarAddNodeMenu = false;

	// Variable → canvas drop 처리:
	//   frame N : drop 수신, 페이로드 + 스크린 좌표 캡쳐.
	//   frame N+1: ed::Begin 안에서 screen→canvas 변환 + Get/Set 팝업 오픈.
	// 한 프레임 지연 비용으로 ed::ScreenToCanvas 호출 컨텍스트 안정성 확보.
	bool    bPendingVariableDrop = false;
	FName   PendingVariableDropName;
	ImVec2  PendingVariableScreenPos = ImVec2(0, 0);
	ImVec2  PendingVariableDropPos   = ImVec2(0, 0);
	bool    bShowVariableDropMenu    = false;

	// 핀을 빈 공간으로 끌어 놓았을 때 UE Blueprint 처럼 context-sensitive node list 를 띄운다.
	bool   bShowPinSpawnMenu    = false;
	uint32 PendingPinSpawnPinId  = 0;
	ImVec2 PendingPinSpawnPos    = ImVec2(0, 0);
	char   PinSpawnSearchBuf[64] = {};

	TArray<TArray<uint8>> UndoStack;
	TArray<TArray<uint8>> RedoStack;
	TArray<FLuaBlueprintNode> ClipboardNodes;
	TArray<FLuaBlueprintLink> ClipboardLinks;
	bool bRestoringSnapshot = false;

	// NodeEditor API selection calls require NodeEditorContext to be current.
	// Hotkeys/menu entries can fire before ed::Begin(), so selection commands are queued
	// and executed inside RenderGraph() after SetCurrentEditor/Begin.
	bool bQueuedCopySelected = false;
	bool bQueuedPasteNodes = false;
	bool bQueuedDuplicateSelected = false;
	bool bQueuedDeleteSelected = false;
	bool bQueuedGroupSelected = false;

	// ImGui popups stay open across frames, but ax::NodeEditor::Show*ContextMenu() only returns
	// the target id on the frame the popup is opened. Persist the ids so right-click menu actions
	// operate on the node/link/pin that opened the menu, not on a zeroed local id on later frames.
	uint32 ContextMenuNodeId = 0;
	uint32 ContextMenuPinId = 0;
	uint32 ContextMenuLinkId = 0;
	bool bPendingInitialContentFit = false;
	bool bPendingNodeGeometryEdit = false;
	bool bSuppressInitialGeometryDirty = false;
	bool bRenderingDocument = false;
	bool bDebugBreakpointsSynced = false;
	bool bPendingNavigateToNode = false;
	uint32 PendingNavigateToNodeId = 0;
	uint64 LastHandledLuaDebugFocusSerial = 0;
	char DebugWatchExpressionBuf[256] = {};

	// NodeEditor 캔버스 안에서 ImGui combo/popup/tooltip 을 직접 열면 좌표와 hit-test 가 섞일 수 있다.
	// 노드 내부에서는 버튼 클릭/호버 상태와 screen-space anchor 만 캡쳐하고, 실제 asset picker 와 tooltip 은 ed::End() 이후 overlay layer 에서만 렌더링한다.
	bool    bOpenInlineAssetPicker = false;
	bool    bInlineAssetPickerVisible = false;
	uint32  InlineAssetPickerNodeId = 0;
	uint32  InlineAssetPickerPinId = 0;
	FString InlineAssetPickerType;
	FString InlineAssetPickerPreviewPath;
	ImVec2  InlineAssetPickerAnchorPos = ImVec2(0, 0);
	ImVec2  InlineAssetPickerFallbackPos = ImVec2(0, 0);
	ImVec2  InlineAssetPickerOwnerMin = ImVec2(0, 0);
	ImVec2  InlineAssetPickerOwnerMax = ImVec2(0, 0);
	char    InlineAssetPickerSearchBuf[128] = {};
	bool    bInlineAssetHoverTooltipVisible = false;
	FString InlineAssetHoverTooltipPath;

	bool   bCustomLuaFunctionEditorOpen = false;
	bool   bCustomLuaFunctionEditorRequestFocus = false;
	uint32 CustomLuaFunctionEditorNodeId = 0;
	char   CustomLuaFunctionNameBuf[128] = {};
	char   CustomLuaFunctionCodeBuf[32768] = {};
};
