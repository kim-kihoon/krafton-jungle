#pragma once

#include "AssetEditorWidget.h"
#include "Editor/Subsystem/EditorAnimationAssetLibrary.h"

#include <imgui.h>

class UAnimInstanceAsset;
enum class EAnimGraphParameterType : uint8;
struct FAnimGraphData;
struct FAnimGraphNodeData;
struct FAnimGraphPinLink;
struct FAnimGraphParameter;
struct FAnimStateMachineData;
struct FAnimStateData;
struct FAnimStateTransitionData;
struct FAnimTransitionCondition;

class FAnimInstanceEditorWidget : public FAssetEditorWidget
{
public:
	FAnimInstanceEditorWidget() = default;

	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Render(float DeltaTime) override;

private:
	enum class ECanvasMode
	{
		StateMachine,
		PoseGraph
	};

	struct FPinRef
	{
		FString NodeId;
		FString PinName;
		bool bInput = false;

		bool IsValid() const { return !NodeId.empty() && !PinName.empty(); }
	};

	struct FPinDrawInfo
	{
		FPinRef Pin;
		float X = 0.0f;
		float Y = 0.0f;
	};

	struct FStatePinRef
	{
		FString StateId;
		bool bInput = false;

		bool IsValid() const { return !StateId.empty(); }
	};

	struct FStatePinDrawInfo
	{
		FStatePinRef Pin;
		float X = 0.0f;
		float Y = 0.0f;
	};

	UAnimInstanceAsset* GetAsset() const;
	FAnimGraphData* GetGraph() const;
	FAnimGraphData* GetEditableGraph() const;

	bool EnsureGraphInvariant();
	bool EnsureGraphInvariant(FAnimGraphData& Graph);
	bool EnsureStateMachineInvariant(UAnimInstanceAsset* Asset);
	void RenderToolbar(UAnimInstanceAsset* Asset);
	void RenderSkeletonPicker(UAnimInstanceAsset* Asset);
	void RenderCanvas(UAnimInstanceAsset* Asset);
	void RenderPoseGraphCanvas(UAnimInstanceAsset* Asset);
	void RenderStateMachineCanvas(UAnimInstanceAsset* Asset);
	void RenderCanvasBreadcrumb(UAnimInstanceAsset* Asset);
	void RenderGraphHeader(UAnimInstanceAsset* Asset, const FAnimGraphData& Graph);
	void RenderStateMachineHeader(UAnimInstanceAsset* Asset);
	void RenderDetails(UAnimInstanceAsset* Asset);
	void RenderAssetDetails(UAnimInstanceAsset* Asset);
	void RenderStateMachineDetails(UAnimInstanceAsset* Asset);
	void RenderValidationSummary(UAnimInstanceAsset* Asset);
	void RenderNodeDetails(UAnimInstanceAsset* Asset, FAnimGraphData& Graph);
	void RenderParameters(UAnimInstanceAsset* Asset);
	void RenderSequencePlayerDetails(UAnimInstanceAsset* Asset, FAnimGraphNodeData& Node);
	void RenderSequenceCreateMenu(UAnimInstanceAsset* Asset, FAnimGraphData& Graph);
	void RenderTransitionDetails(UAnimInstanceAsset* Asset, FAnimStateMachineData& StateMachine);
	void RenderTransitionCondition(UAnimInstanceAsset* Asset, FAnimTransitionCondition& Condition);

	FAnimGraphNodeData* FindNode(FAnimGraphData& Graph, const FString& NodeId) const;
	const FAnimGraphNodeData* FindNode(const FAnimGraphData& Graph, const FString& NodeId) const;
	FAnimStateData* FindState(FAnimStateMachineData& StateMachine, const FString& StateId) const;
	const FAnimStateData* FindState(const FAnimStateMachineData& StateMachine, const FString& StateId) const;
	FAnimStateTransitionData* FindTransition(FAnimStateMachineData& StateMachine, const FString& TransitionId) const;
	const FAnimStateTransitionData* FindTransition(const FAnimStateMachineData& StateMachine, const FString& TransitionId) const;
	int32 FindNodeIndex(const FAnimGraphData& Graph, const FString& NodeId) const;
	int32 FindStateIndex(const FAnimStateMachineData& StateMachine, const FString& StateId) const;
	int32 FindTransitionIndex(const FAnimStateMachineData& StateMachine, const FString& TransitionId) const;
	int32 FindLinkIndex(const FAnimGraphData& Graph, const FAnimGraphPinLink& Link) const;
	int32 CountOutgoingTransitions(const FAnimStateMachineData& StateMachine, const FString& StateId) const;
	bool HasParameter(const FAnimGraphData& Graph, const FString& ParameterName) const;
	bool HasFloatParameter(const UAnimInstanceAsset* Asset, const FString& ParameterName) const;
	const FAnimGraphParameter* FindParameter(const UAnimInstanceAsset* Asset, const FString& ParameterName) const;
	bool HasParameterTypeConflict(const UAnimInstanceAsset* Asset, const FString& ParameterName, EAnimGraphParameterType ParameterType) const;

	FString MakeUniqueNodeId(const FAnimGraphData& Graph, const FString& Prefix) const;
	FString MakeUniqueStateId(const FAnimStateMachineData& StateMachine, const FString& Prefix) const;
	FString MakeUniqueTransitionId(const FAnimStateMachineData& StateMachine) const;
	FString MakeUniqueParameterName(const UAnimInstanceAsset* Asset, EAnimGraphParameterType Type) const;
	void AddNode(FAnimGraphData& Graph, int32 NodeType, float GraphX, float GraphY, const FString& SequencePath = FString());
	void AddState(UAnimInstanceAsset* Asset, float GraphX = 0.0f, float GraphY = 0.0f, bool bUseExplicitPosition = false);
	void DeleteSelectedState(UAnimInstanceAsset* Asset);
	void AddTransitionFromSelectedState(UAnimInstanceAsset* Asset);
	void DeleteSelectedTransition(UAnimInstanceAsset* Asset);
	void OpenStateGraph(const FString& StateId);
	void ReturnToStateMachine();
	void DeleteSelectedNode(FAnimGraphData& Graph);
	void DeleteSelectedLink(FAnimGraphData& Graph);
	int32 CountBrokenLinks(const FAnimGraphData& Graph) const;
	void RemoveInvalidLinks(FAnimGraphData& Graph);
	void CreateStateMachineFromRootGraph(UAnimInstanceAsset* Asset);
	void RenameParameterReferences(FAnimGraphData& Graph, const FString& OldName, const FString& NewName);
	void RenameTransitionParameterReferences(UAnimInstanceAsset* Asset, const FString& OldName, const FString& NewName);
	void RemoveParameterReferences(FAnimGraphData& Graph, const FString& ParameterName);
	void RemoveTransitionParameterReferences(UAnimInstanceAsset* Asset, const FString& ParameterName);
	void RefreshSkeletonOptions();
	void RefreshAnimSequenceOptions(const FString& SkeletonPath);
	const TArray<FEditorAnimationAssetListItem>& GetAnimSequenceOptions(UAnimInstanceAsset* Asset);
	void AssignSequenceToNode(FAnimGraphNodeData& Node, const FString& SequencePath);
	void SetStatusMessage(const FString& Message);
	FString GetCurrentGraphLabel(const UAnimInstanceAsset* Asset) const;
	FString GetNodeDisplayLabel(const FAnimGraphData& Graph, const FString& NodeId) const;

	bool IsValidInputPin(const FAnimGraphNodeData& Node, const FString& PinName) const;
	bool IsValidOutputPin(const FAnimGraphNodeData& Node, const FString& PinName) const;
	bool IsValidLink(const FAnimGraphData& Graph, const FAnimGraphPinLink& Link) const;
	bool CanCreateLink(const FAnimGraphData& Graph, const FPinRef& FromPin, const FPinRef& ToPin) const;
	bool DoesPathReachNode(const FAnimGraphData& Graph, const FString& StartNodeId, const FString& TargetNodeId) const;
	bool CanCreateTransition(const FAnimStateMachineData& StateMachine, const FStatePinRef& FromPin, const FStatePinRef& ToPin) const;
	bool IsTransitionInvalid(const UAnimInstanceAsset* Asset, const FAnimStateMachineData& StateMachine, const FAnimStateTransitionData& Transition) const;
	bool HasSkeleton(const UAnimInstanceAsset* Asset) const;
	bool CanSaveAsset(UAnimInstanceAsset* Asset, FString* OutReason = nullptr) const;
	bool CanSaveGraph(const UAnimInstanceAsset* Asset, const FAnimGraphData& Graph, FString* OutReason = nullptr) const;
	bool CanSaveStateMachine(const UAnimInstanceAsset* Asset, FString* OutReason = nullptr) const;
	bool IsSequenceCompatibleWithAsset(const UAnimInstanceAsset* Asset, const FString& SequencePath, FString* OutActualSkeletonPath = nullptr) const;
	bool IsSequenceNodeSkeletonMismatch(const UAnimInstanceAsset* Asset, const FAnimGraphNodeData& Node, FString* OutActualSkeletonPath = nullptr) const;
	bool TryGetAnimSequencePathFromPayload(const ImGuiPayload* Payload, FString& OutPath) const;
	bool MatchesFilter(const FString& Text, const char* Filter) const;
	void CreateLink(FAnimGraphData& Graph, const FPinRef& FromPin, const FPinRef& ToPin);

	void DrawGraphGrid(const ImVec2& CanvasMin, const ImVec2& CanvasMax, ImDrawList* DrawList) const;
	void DrawNode(const FAnimGraphNodeData& Node, const ImVec2& CanvasMin, ImDrawList* DrawList, TArray<FPinDrawInfo>& OutPins) const;
	void DrawLink(const FAnimGraphData& Graph, const FAnimGraphPinLink& Link, int32 LinkIndex, const TArray<FPinDrawInfo>& Pins, ImDrawList* DrawList, bool bInvalid) const;
	void DrawStateNode(const UAnimInstanceAsset* Asset, const FAnimStateMachineData& StateMachine, const FAnimStateData& State, const ImVec2& CanvasMin, ImDrawList* DrawList, TArray<FStatePinDrawInfo>& OutPins) const;
	void DrawTransitionLink(const UAnimInstanceAsset* Asset, const FAnimStateMachineData& StateMachine, const FAnimStateTransitionData& Transition, int32 TransitionIndex, const ImVec2& CanvasMin, ImDrawList* DrawList) const;
	const FPinDrawInfo* FindDrawnPin(const TArray<FPinDrawInfo>& Pins, const FPinRef& Pin) const;
	const FPinDrawInfo* FindHoveredPin(const TArray<FPinDrawInfo>& Pins, const ImVec2& MousePos) const;
	const FStatePinDrawInfo* FindDrawnStatePin(const TArray<FStatePinDrawInfo>& Pins, const FStatePinRef& Pin) const;
	const FStatePinDrawInfo* FindHoveredStatePin(const TArray<FStatePinDrawInfo>& Pins, const ImVec2& MousePos) const;
	int32 FindHoveredLink(const FAnimGraphData& Graph, const TArray<FPinDrawInfo>& Pins, const ImVec2& MousePos) const;
	int32 FindHoveredTransition(const FAnimStateMachineData& StateMachine, const ImVec2& MousePos, const ImVec2& CanvasMin) const;

	ImVec2 GraphToScreen(const FAnimGraphNodeData& Node, const ImVec2& CanvasMin) const;
	ImVec2 StateToScreen(const FAnimStateData& State, const ImVec2& CanvasMin) const;
	void ScreenToGraph(const ImVec2& ScreenPos, const ImVec2& CanvasMin, float& OutX, float& OutY) const;
	ImVec2 GetNodeSize(const FAnimGraphNodeData& Node) const;
	ImVec2 GetPinScreenPosition(const FAnimGraphNodeData& Node, const FString& PinName, bool bInput, const ImVec2& CanvasMin) const;
	ImVec2 GetStateNodeSize(const FAnimStateData& State) const;
	ImVec2 GetStatePinScreenPosition(const FAnimStateData& State, bool bInput, const ImVec2& CanvasMin) const;
	FString GetTransitionSummary(const UAnimInstanceAsset* Asset, const FAnimStateTransitionData& Transition) const;
	void CreateTransition(FAnimStateMachineData& StateMachine, const FStatePinRef& FromPin, const FStatePinRef& ToPin);

	bool InputFString(const char* Label, FString& Value, size_t BufferSize = 512);

	ECanvasMode CanvasMode = ECanvasMode::StateMachine;
	FString SelectedNodeId;
	FString SelectedStateId;
	FString SelectedTransitionId;
	int32 SelectedLinkIndex = -1;
	FPinRef PendingLinkPin;
	FStatePinRef PendingTransitionPin;
	FString DraggingNodeId;
	FString DraggingStateId;
	bool bDraggingCanvas = false;
	float CanvasPanX = 0.0f;
	float CanvasPanY = 0.0f;
	float CanvasZoom = 1.0f;
	float PendingCreateGraphX = 0.0f;
	float PendingCreateGraphY = 0.0f;
	TArray<FEditorAnimationAssetListItem> SkeletonOptions;
	TArray<FEditorAnimationAssetListItem> AnimSequenceOptions;
	FString CachedAnimSequenceSkeletonPath;
	FString StatusMessage;
	char SkeletonSearchBuffer[128] = {};
	bool bSkeletonOptionsLoaded = false;
};
