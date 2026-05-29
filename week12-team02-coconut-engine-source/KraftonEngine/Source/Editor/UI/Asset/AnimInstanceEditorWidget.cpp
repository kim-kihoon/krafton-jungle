#include "AnimInstanceEditorWidget.h"

#include "Animation/AnimInstanceAsset.h"
#include "Animation/AnimInstanceAssetManager.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "Object/Object.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace
{
	const char* PinPose = "Pose";
	const char* PinA = "A";
	const char* PinB = "B";

	constexpr float PinHitRadius = 9.0f;
	constexpr float NodeRounding = 6.0f;
	constexpr float GridStep = 32.0f;
	constexpr float NodeHeaderHeight = 30.0f;
	constexpr float NodePaddingX = 12.0f;
	constexpr float DefaultNodeWidth = 244.0f;
	constexpr float OutputNodeWidth = 180.0f;
	constexpr float StateNodeWidth = 224.0f;
	constexpr float StateNodeHeight = 112.0f;
	constexpr float MinCanvasZoom = 0.45f;
	constexpr float MaxCanvasZoom = 2.0f;

	const char* GetParameterTypeName(EAnimGraphParameterType Type)
	{
		return Type == EAnimGraphParameterType::Bool ? "Bool" : "Float";
	}

	const char* GetFloatConditionOperatorName(EAnimTransitionConditionOperator Operator)
	{
		switch (Operator)
		{
		case EAnimTransitionConditionOperator::Greater:
			return ">";
		case EAnimTransitionConditionOperator::GreaterEqual:
			return ">=";
		case EAnimTransitionConditionOperator::Less:
			return "<";
		case EAnimTransitionConditionOperator::LessEqual:
			return "<=";
		case EAnimTransitionConditionOperator::Equal:
			return "==";
		case EAnimTransitionConditionOperator::NotEqual:
			return "!=";
		default:
			return ">";
		}
	}

	const char* GetBoolConditionOperatorName(EAnimTransitionConditionOperator Operator)
	{
		return Operator == EAnimTransitionConditionOperator::IsFalse ? "Is False" : "Is True";
	}

	const char* GetNodeTypeName(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
		case EAnimGraphNodeType::SequencePlayer:
			return "Sequence Player";
		case EAnimGraphNodeType::Blend2ByFloat:
			return "Blend2 by Float";
		case EAnimGraphNodeType::Output:
			return "Output";
		default:
			return "Unknown";
		}
	}

	FString GetStateDisplayName(const FAnimStateData& State)
	{
		return State.DisplayName.empty() ? State.StateId : State.DisplayName;
	}

	FString FormatFloatValue(float Value)
	{
		char Buffer[32] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.2f", Value);
		return Buffer;
	}

	FString GetConditionSummary(const FAnimTransitionCondition& Condition, const FAnimGraphParameter* Parameter)
	{
		if (Condition.ParameterName.empty() || !Parameter)
		{
			return "Missing parameter";
		}

		if (Parameter->ParameterType == EAnimGraphParameterType::Bool)
		{
			return Condition.ParameterName + " " + GetBoolConditionOperatorName(Condition.Operator);
		}

		return Condition.ParameterName + " " + GetFloatConditionOperatorName(Condition.Operator) + " " + FormatFloatValue(Condition.FloatThreshold);
	}

	FString GetNodeIdPrefix(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
		case EAnimGraphNodeType::SequencePlayer:
			return "Sequence";
		case EAnimGraphNodeType::Blend2ByFloat:
			return "Blend";
		case EAnimGraphNodeType::Output:
			return "Output";
		default:
			return "Node";
		}
	}

	ImU32 GetNodeAccentColor(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
		case EAnimGraphNodeType::SequencePlayer:
			return IM_COL32(235, 180, 80, 255);
		case EAnimGraphNodeType::Blend2ByFloat:
			return IM_COL32(95, 170, 245, 255);
		case EAnimGraphNodeType::Output:
			return IM_COL32(105, 210, 130, 255);
		default:
			return IM_COL32(150, 150, 150, 255);
		}
	}

	float DistanceSquared(const ImVec2& A, const ImVec2& B)
	{
		const float DX = A.x - B.x;
		const float DY = A.y - B.y;
		return DX * DX + DY * DY;
	}

	float DistanceToSegmentSquared(const ImVec2& Point, const ImVec2& A, const ImVec2& B)
	{
		const float VX = B.x - A.x;
		const float VY = B.y - A.y;
		const float WX = Point.x - A.x;
		const float WY = Point.y - A.y;
		const float LenSq = VX * VX + VY * VY;
		if (LenSq <= 0.0001f)
		{
			return DistanceSquared(Point, A);
		}

		float T = (WX * VX + WY * VY) / LenSq;
		T = (std::max)(0.0f, (std::min)(T, 1.0f));
		return DistanceSquared(Point, ImVec2(A.x + VX * T, A.y + VY * T));
	}

	ImVec2 BezierPoint(const ImVec2& P0, const ImVec2& P1, const ImVec2& P2, const ImVec2& P3, float T)
	{
		const float U = 1.0f - T;
		const float B0 = U * U * U;
		const float B1 = 3.0f * U * U * T;
		const float B2 = 3.0f * U * T * T;
		const float B3 = T * T * T;
		return ImVec2(
			P0.x * B0 + P1.x * B1 + P2.x * B2 + P3.x * B3,
			P0.y * B0 + P1.y * B1 + P2.y * B2 + P3.y * B3);
	}

	bool IsPointInsideRect(const ImVec2& P, const ImVec2& Min, const ImVec2& Max)
	{
		return P.x >= Min.x && P.x <= Max.x && P.y >= Min.y && P.y <= Max.y;
	}

	FString GetPathDisplayName(const FString& Path)
	{
		if (Path.empty())
		{
			return FString();
		}

		const size_t Slash = Path.find_last_of("/\\");
		const size_t Start = Slash == FString::npos ? 0 : Slash + 1;
		size_t End = Path.find_last_of('.');
		if (End == FString::npos || End < Start)
		{
			End = Path.size();
		}
		return Path.substr(Start, End - Start);
	}

	void DrawFieldLabel(const char* Label)
	{
		ImGui::TextDisabled("%s", Label);
	}

	void DrawWrappedValue(const FString& Value, const char* EmptyText = "None")
	{
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
		ImGui::TextUnformatted(Value.empty() ? EmptyText : Value.c_str());
		ImGui::PopTextWrapPos();
	}

	FString FitTextToWidth(const FString& Text, float MaxWidth, float TextScale = 1.0f)
	{
		if (Text.empty() || MaxWidth <= 0.0f)
		{
			return FString();
		}

		const float SafeTextScale = (std::max)(0.01f, TextScale);
		const float UnscaledMaxWidth = MaxWidth / SafeTextScale;
		if (ImGui::CalcTextSize(Text.c_str()).x <= UnscaledMaxWidth)
		{
			return Text;
		}

		const FString Ellipsis = "...";
		const float EllipsisWidth = ImGui::CalcTextSize(Ellipsis.c_str()).x;
		if (UnscaledMaxWidth <= EllipsisWidth)
		{
			return FString();
		}

		FString Result = Text;
		while (!Result.empty())
		{
			const FString Candidate = Result + Ellipsis;
			if (ImGui::CalcTextSize(Candidate.c_str()).x <= UnscaledMaxWidth)
			{
				return Candidate;
			}
			Result.pop_back();
		}

		return Ellipsis;
	}

	void DrawClippedText(ImDrawList* DrawList, const ImVec2& Pos, float MaxWidth, ImU32 Color, const FString& Text, float TextScale = 1.0f)
	{
		const float SafeTextScale = (std::max)(0.01f, TextScale);
		const FString FittedText = FitTextToWidth(Text, MaxWidth, SafeTextScale);
		if (!FittedText.empty())
		{
			DrawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * SafeTextScale, Pos, Color, FittedText.c_str());
		}
	}
}

bool FAnimInstanceEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UAnimInstanceAsset>();
}

void FAnimInstanceEditorWidget::Open(UObject* Object)
{
	if (!CanEdit(Object))
	{
		return;
	}

	EditedObject = Object;
	bOpen = true;
	RequestFocus();
	SelectedNodeId.clear();
	SelectedStateId.clear();
	SelectedTransitionId.clear();
	SelectedLinkIndex = -1;
	PendingLinkPin = FPinRef();
	PendingTransitionPin = FStatePinRef();
	DraggingNodeId.clear();
	DraggingStateId.clear();
	CanvasMode = ECanvasMode::StateMachine;
	CanvasPanX = 32.0f;
	CanvasPanY = 32.0f;
	CanvasZoom = 1.0f;
	SkeletonSearchBuffer[0] = '\0';
	StatusMessage.clear();
	CachedAnimSequenceSkeletonPath.clear();
	AnimSequenceOptions.clear();
	bSkeletonOptionsLoaded = false;
	if (UAnimInstanceAsset* Asset = GetAsset())
	{
		Asset->PromoteLegacyGraphParametersToAsset();
	}
	EnsureGraphInvariant();
	if (UAnimInstanceAsset* Asset = GetAsset())
	{
		if (Asset->HasStateMachine())
		{
			SelectedStateId = Asset->GetStateMachine().EntryStateId.empty()
				? Asset->GetStateMachine().States.front().StateId
				: Asset->GetStateMachine().EntryStateId;
			CanvasMode = ECanvasMode::StateMachine;
		}
		else
		{
			CanvasMode = ECanvasMode::PoseGraph;
		}
	}
	ClearDirty();
}

void FAnimInstanceEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UAnimInstanceAsset* Asset = GetAsset();
	if (!Asset)
	{
		return;
	}

	if (EnsureGraphInvariant())
	{
		MarkDirty();
	}

	bool bWindowOpen = true;
	FString VisibleTitle = "Anim Instance Editor";
	if (!Asset->GetAssetPathFileName().empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += Asset->GetAssetPathFileName();
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGui::SetNextWindowSize(ImVec2(980.0f, 620.0f), ImGuiCond_Once);
	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}
	const FString WindowTitle = VisibleTitle + "###AnimInstanceEditor";
	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
		return;
	}

	RenderToolbar(Asset);
	ImGui::Separator();

	if (!StatusMessage.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "%s", StatusMessage.c_str());
		ImGui::Separator();
	}

	if (!HasSkeleton(Asset))
	{
		RenderSkeletonPicker(Asset);
	}
	else
	{
		const float DetailsWidth = 320.0f;
		RenderCanvas(Asset);
		ImGui::SameLine();
		ImGui::BeginChild("AnimInstanceDetails", ImVec2(DetailsWidth, 0.0f), true);
		RenderDetails(Asset);
		ImGui::EndChild();
	}

	ImGui::End();
	if (!bWindowOpen)
	{
		Close();
	}
}

UAnimInstanceAsset* FAnimInstanceEditorWidget::GetAsset() const
{
	return Cast<UAnimInstanceAsset>(EditedObject);
}

FAnimGraphData* FAnimInstanceEditorWidget::GetGraph() const
{
	UAnimInstanceAsset* Asset = GetAsset();
	return Asset ? &Asset->GetGraph() : nullptr;
}

FAnimGraphData* FAnimInstanceEditorWidget::GetEditableGraph() const
{
	UAnimInstanceAsset* Asset = GetAsset();
	if (!Asset)
	{
		return nullptr;
	}

	if (Asset->HasStateMachine())
	{
		FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
		const FString StateId = SelectedStateId.empty() ? StateMachine.EntryStateId : SelectedStateId;
		if (FAnimStateData* State = FindState(StateMachine, StateId))
		{
			return &State->Graph;
		}
		if (!StateMachine.States.empty())
		{
			return &StateMachine.States.front().Graph;
		}
	}

	return &Asset->GetGraph();
}

bool FAnimInstanceEditorWidget::EnsureGraphInvariant(FAnimGraphData& Graph)
{
	bool bChanged = false;
	int32 FirstOutputIndex = -1;
	for (int32 Index = 0; Index < static_cast<int32>(Graph.Nodes.size()); ++Index)
	{
		if (Graph.Nodes[Index].NodeType == EAnimGraphNodeType::Output)
		{
			if (FirstOutputIndex == -1)
			{
				FirstOutputIndex = Index;
			}
		}
	}

	if (FirstOutputIndex == -1)
	{
		FAnimGraphNodeData OutputNode;
		OutputNode.NodeId = MakeUniqueNodeId(Graph, "Output");
		OutputNode.DisplayName = "Output";
		OutputNode.NodeType = EAnimGraphNodeType::Output;
		OutputNode.EditorPosX = 420.0f;
		OutputNode.EditorPosY = 80.0f;
		Graph.OutputNodeId = OutputNode.NodeId;
		Graph.Nodes.push_back(OutputNode);
		return true;
	}

	const FString FirstOutputId = Graph.Nodes[FirstOutputIndex].NodeId;
	if (Graph.OutputNodeId != FirstOutputId)
	{
		Graph.OutputNodeId = FirstOutputId;
		bChanged = true;
	}

	for (int32 Index = static_cast<int32>(Graph.Nodes.size()) - 1; Index >= 0; --Index)
	{
		if (Index == FirstOutputIndex || Graph.Nodes[Index].NodeType != EAnimGraphNodeType::Output)
		{
			continue;
		}

		const FString RemovedNodeId = Graph.Nodes[Index].NodeId;
		Graph.Nodes.erase(Graph.Nodes.begin() + Index);
		Graph.Links.erase(std::remove_if(Graph.Links.begin(), Graph.Links.end(),
			[&RemovedNodeId](const FAnimGraphPinLink& Link)
			{
				return Link.FromNodeId == RemovedNodeId || Link.ToNodeId == RemovedNodeId;
			}),
			Graph.Links.end());
		if (SelectedNodeId == RemovedNodeId)
		{
			SelectedNodeId = FirstOutputId;
		}
		bChanged = true;
	}

	return bChanged;
}

bool FAnimInstanceEditorWidget::EnsureStateMachineInvariant(UAnimInstanceAsset* Asset)
{
	if (!Asset || !Asset->HasStateMachine())
	{
		return false;
	}

	bool bChanged = false;
	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	for (FAnimStateData& State : StateMachine.States)
	{
		bChanged |= EnsureGraphInvariant(State.Graph);
	}

	if (!FindState(StateMachine, StateMachine.EntryStateId) && !StateMachine.States.empty())
	{
		StateMachine.EntryStateId = StateMachine.States.front().StateId;
		bChanged = true;
	}

	if (!FindState(StateMachine, SelectedStateId) && !StateMachine.States.empty())
	{
		SelectedStateId = StateMachine.EntryStateId.empty() ? StateMachine.States.front().StateId : StateMachine.EntryStateId;
		SelectedNodeId.clear();
		SelectedLinkIndex = -1;
	}

	StateMachine.Transitions.erase(std::remove_if(StateMachine.Transitions.begin(), StateMachine.Transitions.end(),
		[this, &StateMachine, &bChanged](const FAnimStateTransitionData& Transition)
		{
			const bool bInvalid = !FindState(StateMachine, Transition.FromStateId) || !FindState(StateMachine, Transition.ToStateId);
			if (bInvalid)
			{
				bChanged = true;
			}
			return bInvalid;
		}),
		StateMachine.Transitions.end());

	return bChanged;
}

bool FAnimInstanceEditorWidget::EnsureGraphInvariant()
{
	UAnimInstanceAsset* Asset = GetAsset();
	if (!Asset)
	{
		return false;
	}

	bool bChanged = EnsureGraphInvariant(Asset->GetGraph());
	bChanged |= EnsureStateMachineInvariant(Asset);
	return bChanged;
}

void FAnimInstanceEditorWidget::RenderToolbar(UAnimInstanceAsset* Asset)
{
	FString SaveReason;
	const bool bCanSave = CanSaveAsset(Asset, &SaveReason);
	ImGui::BeginDisabled(!bCanSave);
	if (ImGui::Button("Save"))
	{
		EnsureGraphInvariant();
		if (FAnimInstanceAssetManager::Get().Save(Asset))
		{
			ClearDirty();
			SetStatusMessage("AnimInstance asset saved.");
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", Asset->GetAssetPathFileName().empty() ? "Unsaved asset" : Asset->GetAssetPathFileName().c_str());
	if (!bCanSave && !SaveReason.empty())
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", SaveReason.c_str());
	}
	if (HasSkeleton(Asset))
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Skeleton: %s", Asset->GetSkeletonPath().c_str());
	}
}

void FAnimInstanceEditorWidget::RenderSkeletonPicker(UAnimInstanceAsset* Asset)
{
	if (!bSkeletonOptionsLoaded)
	{
		RefreshSkeletonOptions();
	}

	ImGui::TextUnformatted("Select Skeleton");
	ImGui::TextDisabled("AnimInstance editing starts after a Skeleton is selected. The Skeleton cannot be changed later.");
	ImGui::Spacing();

	ImGui::SetNextItemWidth(300.0f);
	ImGui::InputText("Search", SkeletonSearchBuffer, sizeof(SkeletonSearchBuffer));
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		RefreshSkeletonOptions();
	}

	if (SkeletonOptions.empty())
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.25f, 1.0f), "No Skeleton assets found. Import a Skeleton, SkeletalMesh, or AnimSequence first.");
		return;
	}

	static int32 SelectedSkeletonIndex = -1;
	if (SelectedSkeletonIndex >= static_cast<int32>(SkeletonOptions.size()))
	{
		SelectedSkeletonIndex = -1;
	}

	ImGui::BeginChild("SkeletonPickerList", ImVec2(0.0f, -42.0f), true);
	for (int32 Index = 0; Index < static_cast<int32>(SkeletonOptions.size()); ++Index)
	{
		const FEditorAnimationAssetListItem& Item = SkeletonOptions[Index];
		if (!MatchesFilter(Item.DisplayName, SkeletonSearchBuffer) && !MatchesFilter(Item.FullPath, SkeletonSearchBuffer))
		{
			continue;
		}

		ImGui::PushID(Index);
		const bool bSelected = SelectedSkeletonIndex == Index;
		if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
		{
			SelectedSkeletonIndex = Index;
		}
		ImGui::TextDisabled("%s", Item.FullPath.c_str());
		ImGui::PopID();
	}
	ImGui::EndChild();

	ImGui::BeginDisabled(SelectedSkeletonIndex < 0 || SelectedSkeletonIndex >= static_cast<int32>(SkeletonOptions.size()));
	if (ImGui::Button("Select Skeleton"))
	{
		const FEditorAnimationAssetListItem& Item = SkeletonOptions[SelectedSkeletonIndex];
		Asset->SetSkeletonPath(Item.FullPath);
		EnsureGraphInvariant();
		if (!Asset->HasStateMachine())
		{
			CreateStateMachineFromRootGraph(Asset);
		}
		CanvasMode = ECanvasMode::StateMachine;
		RefreshAnimSequenceOptions(Item.FullPath);
		SetStatusMessage("Skeleton selected. Graph editing is now enabled.");
		if (FAnimGraphData* EditableGraph = GetEditableGraph())
		{
			SelectedNodeId = EditableGraph->OutputNodeId;
		}
		MarkDirty();
	}
	ImGui::EndDisabled();
}

void FAnimInstanceEditorWidget::RenderGraphHeader(UAnimInstanceAsset* Asset, const FAnimGraphData& Graph)
{
	RenderCanvasBreadcrumb(Asset);
	ImGui::SameLine();
	ImGui::TextDisabled("| %zu nodes | %zu links | %zu global params", Graph.Nodes.size(), Graph.Links.size(), Asset ? Asset->GetParameters().size() : 0);

	if (Asset && Asset->HasStateMachine())
	{
		const FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
		ImGui::SameLine();
		ImGui::TextDisabled("| %zu states", StateMachine.States.size());
		if (!SelectedStateId.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("| %d outgoing", CountOutgoingTransitions(StateMachine, SelectedStateId));
		}
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("-##ZoomOut"))
	{
		CanvasZoom = (std::max)(MinCanvasZoom, CanvasZoom / 1.12f);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("100%##ZoomReset"))
	{
		CanvasZoom = 1.0f;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("+##ZoomIn"))
	{
		CanvasZoom = (std::min)(MaxCanvasZoom, CanvasZoom * 1.12f);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Reset View"))
	{
		CanvasPanX = 32.0f;
		CanvasPanY = 32.0f;
		CanvasZoom = 1.0f;
	}

	FString SaveReason;
	if (!CanSaveAsset(Asset, &SaveReason) && !SaveReason.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "Save blocked: %s", SaveReason.c_str());
	}
}

void FAnimInstanceEditorWidget::RenderCanvasBreadcrumb(UAnimInstanceAsset* Asset)
{
	if (!Asset || !Asset->HasStateMachine())
	{
		ImGui::TextUnformatted("Root Graph");
		return;
	}

	if (CanvasMode == ECanvasMode::PoseGraph)
	{
		if (ImGui::SmallButton("< State Machine"))
		{
			ReturnToStateMachine();
		}
		ImGui::SameLine();
	}

	ImGui::TextDisabled("AnimInstance");
	ImGui::SameLine();
	ImGui::TextDisabled(">");
	ImGui::SameLine();

	if (CanvasMode == ECanvasMode::PoseGraph)
	{
		if (ImGui::SmallButton("State Machine##BreadcrumbStateMachine"))
		{
			ReturnToStateMachine();
		}
		ImGui::SameLine();
		ImGui::TextDisabled(">");
		ImGui::SameLine();

		const FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
		const FAnimStateData* State = FindState(StateMachine, SelectedStateId);
		if (!State && !StateMachine.EntryStateId.empty())
		{
			State = FindState(StateMachine, StateMachine.EntryStateId);
		}
		ImGui::TextUnformatted(State ? GetStateDisplayName(*State).c_str() : "State Graph");
		return;
	}

	ImGui::TextUnformatted("State Machine");
}

void FAnimInstanceEditorWidget::RenderStateMachineHeader(UAnimInstanceAsset* Asset)
{
	RenderCanvasBreadcrumb(Asset);

	if (Asset && Asset->HasStateMachine())
	{
		const FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
		ImGui::SameLine();
		ImGui::TextDisabled("| %zu states | %zu transitions", StateMachine.States.size(), StateMachine.Transitions.size());
		if (!SelectedStateId.empty())
		{
			const FAnimStateData* SelectedState = FindState(StateMachine, SelectedStateId);
			const FString SelectedLabel = SelectedState ? GetStateDisplayName(*SelectedState) : SelectedStateId;
			ImGui::SameLine();
			ImGui::TextDisabled("| selected: %s", SelectedLabel.c_str());
		}
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("-##SMZoomOut"))
	{
		CanvasZoom = (std::max)(MinCanvasZoom, CanvasZoom / 1.12f);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("100%##SMZoomReset"))
	{
		CanvasZoom = 1.0f;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("+##SMZoomIn"))
	{
		CanvasZoom = (std::min)(MaxCanvasZoom, CanvasZoom * 1.12f);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Reset View##SM"))
	{
		CanvasPanX = 32.0f;
		CanvasPanY = 32.0f;
		CanvasZoom = 1.0f;
	}

	FString SaveReason;
	if (!CanSaveAsset(Asset, &SaveReason) && !SaveReason.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "Save blocked: %s", SaveReason.c_str());
	}
}

void FAnimInstanceEditorWidget::RenderCanvas(UAnimInstanceAsset* Asset)
{
	if (Asset && Asset->HasStateMachine() && CanvasMode == ECanvasMode::StateMachine)
	{
		RenderStateMachineCanvas(Asset);
		return;
	}

	RenderPoseGraphCanvas(Asset);
}

void FAnimInstanceEditorWidget::RenderPoseGraphCanvas(UAnimInstanceAsset* Asset)
{
	FAnimGraphData* EditableGraph = GetEditableGraph();
	if (!EditableGraph)
	{
		return;
	}

	FAnimGraphData& Graph = *EditableGraph;
	const float DetailsWidth = 336.0f;
	ImGui::BeginChild("AnimInstanceGraph", ImVec2(-DetailsWidth, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	RenderGraphHeader(Asset, Graph);
	ImGui::Separator();

	ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
	ImVec2 CanvasSize = ImGui::GetContentRegionAvail();
	CanvasSize.x = (std::max)(CanvasSize.x, 280.0f);
	CanvasSize.y = (std::max)(CanvasSize.y, 280.0f);
	const ImVec2 CanvasMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);
	ImGui::InvisibleButton("##AnimGraphCanvas", CanvasSize, ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bCanvasHovered = ImGui::IsItemHovered();
	const ImGuiIO& IO = ImGui::GetIO();
	if (bCanvasHovered && IO.MouseWheel != 0.0f && !IO.WantTextInput)
	{
		float MouseGraphX = 0.0f;
		float MouseGraphY = 0.0f;
		ScreenToGraph(IO.MousePos, CanvasMin, MouseGraphX, MouseGraphY);

		const float ZoomFactor = std::pow(1.12f, IO.MouseWheel);
		CanvasZoom = (std::max)(MinCanvasZoom, (std::min)(CanvasZoom * ZoomFactor, MaxCanvasZoom));
		CanvasPanX = IO.MousePos.x - CanvasMin.x - MouseGraphX * CanvasZoom;
		CanvasPanY = IO.MousePos.y - CanvasMin.y - MouseGraphY * CanvasZoom;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(CanvasMin, CanvasMax, true);
	DrawGraphGrid(CanvasMin, CanvasMax, DrawList);
	int32 PoseNodeCount = 0;
	for (const FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (Node.NodeType != EAnimGraphNodeType::Output)
		{
			++PoseNodeCount;
		}
	}
	if (PoseNodeCount == 0)
	{
		const char* EmptyText = "No pose nodes in this graph";
		const ImVec2 TextSize = ImGui::CalcTextSize(EmptyText);
		DrawList->AddText(ImVec2(CanvasMin.x + (CanvasSize.x - TextSize.x) * 0.5f, CanvasMin.y + (CanvasSize.y - TextSize.y) * 0.5f), IM_COL32(120, 126, 138, 180), EmptyText);
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("AnimSequenceContentItem"))
		{
			FString SequencePath;
			if (TryGetAnimSequencePathFromPayload(Payload, SequencePath))
			{
				FString ActualSkeletonPath;
				if (IsSequenceCompatibleWithAsset(Asset, SequencePath, &ActualSkeletonPath))
				{
					float GraphX = 0.0f;
					float GraphY = 0.0f;
					ScreenToGraph(IO.MousePos, CanvasMin, GraphX, GraphY);
					AddNode(Graph, static_cast<int32>(EAnimGraphNodeType::SequencePlayer), GraphX, GraphY, SequencePath);
				}
				else
				{
					SetStatusMessage("AnimSequence drop rejected: Skeleton mismatch or invalid AnimSequence.");
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	TArray<FPinDrawInfo> Pins;
	for (const FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (Node.NodeType == EAnimGraphNodeType::SequencePlayer)
		{
			Pins.push_back({ { Node.NodeId, PinPose, false }, GetPinScreenPosition(Node, PinPose, false, CanvasMin).x, GetPinScreenPosition(Node, PinPose, false, CanvasMin).y });
		}
		else if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat)
		{
			Pins.push_back({ { Node.NodeId, PinA, true }, GetPinScreenPosition(Node, PinA, true, CanvasMin).x, GetPinScreenPosition(Node, PinA, true, CanvasMin).y });
			Pins.push_back({ { Node.NodeId, PinB, true }, GetPinScreenPosition(Node, PinB, true, CanvasMin).x, GetPinScreenPosition(Node, PinB, true, CanvasMin).y });
			Pins.push_back({ { Node.NodeId, PinPose, false }, GetPinScreenPosition(Node, PinPose, false, CanvasMin).x, GetPinScreenPosition(Node, PinPose, false, CanvasMin).y });
		}
		else if (Node.NodeType == EAnimGraphNodeType::Output)
		{
			Pins.push_back({ { Node.NodeId, PinPose, true }, GetPinScreenPosition(Node, PinPose, true, CanvasMin).x, GetPinScreenPosition(Node, PinPose, true, CanvasMin).y });
		}
	}

	TSet<FString> DrawnInputPins;
	for (int32 LinkIndex = 0; LinkIndex < static_cast<int32>(Graph.Links.size()); ++LinkIndex)
	{
		const FAnimGraphPinLink& Link = Graph.Links[LinkIndex];
		const FString InputKey = Link.ToNodeId + "." + Link.ToPinName;
		const bool bDuplicateInput = DrawnInputPins.find(InputKey) != DrawnInputPins.end();
		const bool bInvalid = bDuplicateInput || !IsValidLink(Graph, Link);
		DrawLink(Graph, Link, LinkIndex, Pins, DrawList, bInvalid);
		DrawnInputPins.insert(InputKey);
	}

	const FPinDrawInfo* HoveredPin = FindHoveredPin(Pins, IO.MousePos);
	bool bMouseOverNode = false;

	for (FAnimGraphNodeData& Node : Graph.Nodes)
	{
		DrawNode(Node, CanvasMin, DrawList, Pins);

		const ImVec2 NodeMin = GraphToScreen(Node, CanvasMin);
		const ImVec2 NodeSize = GetNodeSize(Node);
		const ImVec2 NodeMax(NodeMin.x + NodeSize.x, NodeMin.y + NodeSize.y);
		if (IsSequenceNodeSkeletonMismatch(Asset, Node))
		{
			DrawList->AddRect(NodeMin, NodeMax, IM_COL32(245, 80, 70, 255), NodeRounding * CanvasZoom, 0, (std::max)(1.0f, 3.0f * CanvasZoom));
			DrawClippedText(DrawList, ImVec2(NodeMin.x + NodePaddingX * CanvasZoom, NodeMax.y - 22.0f * CanvasZoom), NodeSize.x - NodePaddingX * 2.0f * CanvasZoom, IM_COL32(245, 110, 95, 255), "Skeleton mismatch", CanvasZoom);
		}
		else if (Node.NodeType == EAnimGraphNodeType::SequencePlayer && Node.AnimSequencePath.empty())
		{
			DrawList->AddRect(NodeMin, NodeMax, IM_COL32(245, 180, 70, 190), NodeRounding * CanvasZoom, 0, (std::max)(1.0f, 2.0f * CanvasZoom));
		}
		const bool bInsideNode = IsPointInsideRect(IO.MousePos, NodeMin, NodeMax);
		bMouseOverNode |= bInsideNode;

		ImGui::PushID(Node.NodeId.c_str());
		ImGui::SetCursorScreenPos(NodeMin);
		ImGui::InvisibleButton("##AnimGraphNodeHit", NodeSize, ImGuiButtonFlags_MouseButtonLeft);
		const bool bNodeClicked = (ImGui::IsItemClicked(ImGuiMouseButton_Left) || (bInsideNode && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
			&& !HoveredPin;
		if (bNodeClicked)
		{
			SelectedNodeId = Node.NodeId;
			SelectedLinkIndex = -1;
			DraggingNodeId = Node.NodeId;
		}
		if (DraggingNodeId == Node.NodeId && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !PendingLinkPin.IsValid())
		{
			if (IO.MouseDelta.x != 0.0f || IO.MouseDelta.y != 0.0f)
			{
				Node.EditorPosX += IO.MouseDelta.x / CanvasZoom;
				Node.EditorPosY += IO.MouseDelta.y / CanvasZoom;
				MarkDirty();
			}
		}
		ImGui::PopID();
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		DraggingNodeId.clear();
	}

	const int32 HoveredLinkIndex = (!HoveredPin && !bMouseOverNode) ? FindHoveredLink(Graph, Pins, IO.MousePos) : -1;
	if (HoveredLinkIndex >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		SelectedNodeId.clear();
		SelectedLinkIndex = HoveredLinkIndex;
	}
	else if (bCanvasHovered && !HoveredPin && !bMouseOverNode && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		SelectedNodeId.clear();
		SelectedLinkIndex = -1;
	}

	if (HoveredPin && !HoveredPin->Pin.bInput && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		PendingLinkPin = HoveredPin->Pin;
		DraggingNodeId.clear();
		SelectedNodeId.clear();
		SelectedLinkIndex = -1;
	}

	if (PendingLinkPin.IsValid())
	{
		if (const FPinDrawInfo* StartPin = FindDrawnPin(Pins, PendingLinkPin))
		{
			const ImVec2 P0(StartPin->X, StartPin->Y);
			const ImVec2 P3(IO.MousePos.x, IO.MousePos.y);
			const float Tangent = (std::max)(80.0f * CanvasZoom, std::fabs(P3.x - P0.x) * 0.5f);
			DrawList->AddBezierCubic(P0, ImVec2(P0.x + Tangent, P0.y), ImVec2(P3.x - Tangent, P3.y), P3, IM_COL32(245, 210, 90, 255), (std::max)(1.0f, 2.0f * CanvasZoom), 24);
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (HoveredPin && HoveredPin->Pin.bInput && CanCreateLink(Graph, PendingLinkPin, HoveredPin->Pin))
			{
				CreateLink(Graph, PendingLinkPin, HoveredPin->Pin);
				MarkDirty();
			}
			PendingLinkPin = FPinRef();
		}
	}

	if (bCanvasHovered && !bMouseOverNode && !HoveredPin && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ScreenToGraph(IO.MousePos, CanvasMin, PendingCreateGraphX, PendingCreateGraphY);
		ImGui::OpenPopup("AnimInstanceGraphContext");
	}

	if (ImGui::BeginPopup("AnimInstanceGraphContext"))
	{
		RenderSequenceCreateMenu(Asset, Graph);
		if (ImGui::MenuItem("Blend2 by Float"))
		{
			AddNode(Graph, static_cast<int32>(EAnimGraphNodeType::Blend2ByFloat), PendingCreateGraphX, PendingCreateGraphY);
		}
		const bool bHasOutput = FindNode(Graph, Graph.OutputNodeId) != nullptr;
		ImGui::BeginDisabled(bHasOutput);
		if (ImGui::MenuItem("Output"))
		{
			AddNode(Graph, static_cast<int32>(EAnimGraphNodeType::Output), PendingCreateGraphX, PendingCreateGraphY);
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	if (bCanvasHovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
	{
		bDraggingCanvas = true;
		CanvasPanX += IO.MouseDelta.x;
		CanvasPanY += IO.MouseDelta.y;
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
	{
		bDraggingCanvas = false;
	}

	if (!IO.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		if (!SelectedNodeId.empty())
		{
			DeleteSelectedNode(Graph);
		}
		else if (SelectedLinkIndex >= 0)
		{
			DeleteSelectedLink(Graph);
		}
	}

	DrawList->PopClipRect();
	ImGui::SetCursorScreenPos(ImVec2(CanvasMin.x, CanvasMax.y + 4.0f));
	ImGui::TextDisabled("LMB drag node | Wheel: zoom %.0f%% | RMB: add node | MMB drag: pan | Drag output pin to input pin | Delete: remove selected", CanvasZoom * 100.0f);
	ImGui::EndChild();
}

void FAnimInstanceEditorWidget::RenderStateMachineCanvas(UAnimInstanceAsset* Asset)
{
	if (!Asset || !Asset->HasStateMachine())
	{
		return;
	}

	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	const float DetailsWidth = 336.0f;
	ImGui::BeginChild("AnimInstanceStateMachineGraph", ImVec2(-DetailsWidth, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	RenderStateMachineHeader(Asset);
	ImGui::Separator();

	ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
	ImVec2 CanvasSize = ImGui::GetContentRegionAvail();
	CanvasSize.x = (std::max)(CanvasSize.x, 280.0f);
	CanvasSize.y = (std::max)(CanvasSize.y, 280.0f);
	const ImVec2 CanvasMax(CanvasMin.x + CanvasSize.x, CanvasMin.y + CanvasSize.y);
	ImGui::InvisibleButton("##AnimStateMachineCanvas", CanvasSize, ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bCanvasHovered = ImGui::IsItemHovered();
	const ImGuiIO& IO = ImGui::GetIO();
	if (bCanvasHovered && IO.MouseWheel != 0.0f && !IO.WantTextInput)
	{
		float MouseGraphX = 0.0f;
		float MouseGraphY = 0.0f;
		ScreenToGraph(IO.MousePos, CanvasMin, MouseGraphX, MouseGraphY);

		const float ZoomFactor = std::pow(1.12f, IO.MouseWheel);
		CanvasZoom = (std::max)(MinCanvasZoom, (std::min)(CanvasZoom * ZoomFactor, MaxCanvasZoom));
		CanvasPanX = IO.MousePos.x - CanvasMin.x - MouseGraphX * CanvasZoom;
		CanvasPanY = IO.MousePos.y - CanvasMin.y - MouseGraphY * CanvasZoom;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(CanvasMin, CanvasMax, true);
	DrawGraphGrid(CanvasMin, CanvasMax, DrawList);

	if (StateMachine.States.empty())
	{
		const char* EmptyText = "No states in this state machine";
		const ImVec2 TextSize = ImGui::CalcTextSize(EmptyText);
		DrawList->AddText(ImVec2(CanvasMin.x + (CanvasSize.x - TextSize.x) * 0.5f, CanvasMin.y + (CanvasSize.y - TextSize.y) * 0.5f), IM_COL32(120, 126, 138, 180), EmptyText);
	}

	for (int32 TransitionIndex = 0; TransitionIndex < static_cast<int32>(StateMachine.Transitions.size()); ++TransitionIndex)
	{
		DrawTransitionLink(Asset, StateMachine, StateMachine.Transitions[TransitionIndex], TransitionIndex, CanvasMin, DrawList);
	}

	TArray<FStatePinDrawInfo> StatePins;
	for (const FAnimStateData& State : StateMachine.States)
	{
		DrawStateNode(Asset, StateMachine, State, CanvasMin, DrawList, StatePins);
	}

	const FStatePinDrawInfo* HoveredPin = FindHoveredStatePin(StatePins, IO.MousePos);
	bool bMouseOverState = false;
	for (FAnimStateData& State : StateMachine.States)
	{
		const ImVec2 StateMin = StateToScreen(State, CanvasMin);
		const ImVec2 StateSize = GetStateNodeSize(State);
		const ImVec2 StateMax(StateMin.x + StateSize.x, StateMin.y + StateSize.y);
		const bool bInsideState = IsPointInsideRect(IO.MousePos, StateMin, StateMax);
		bMouseOverState |= bInsideState;

		ImGui::PushID(State.StateId.c_str());
		ImGui::SetCursorScreenPos(StateMin);
		ImGui::InvisibleButton("##AnimStateHit", StateSize, ImGuiButtonFlags_MouseButtonLeft);
		const float OpenIconSize = 18.0f * CanvasZoom;
		const float OpenIconMargin = 6.0f * CanvasZoom;
		const ImVec2 OpenIconMin(StateMax.x - OpenIconMargin - OpenIconSize, StateMin.y + OpenIconMargin);
		const ImVec2 OpenIconMax(OpenIconMin.x + OpenIconSize, OpenIconMin.y + OpenIconSize);
		const bool bInsideOpenIcon = IsPointInsideRect(IO.MousePos, OpenIconMin, OpenIconMax);
		if (bInsideOpenIcon)
		{
			ImGui::SetTooltip("Open State Graph");
		}
		if (bInsideOpenIcon && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			OpenStateGraph(State.StateId);
			ImGui::PopID();
			continue;
		}
		const bool bStateClicked = (ImGui::IsItemClicked(ImGuiMouseButton_Left) || (bInsideState && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) && !HoveredPin;
		if (bStateClicked)
		{
			SelectedStateId = State.StateId;
			SelectedTransitionId.clear();
			SelectedNodeId.clear();
			SelectedLinkIndex = -1;
			DraggingStateId = State.StateId;
		}
		if (bInsideState && !bInsideOpenIcon && !HoveredPin && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			OpenStateGraph(State.StateId);
		}
		if (DraggingStateId == State.StateId && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !PendingTransitionPin.IsValid())
		{
			if (IO.MouseDelta.x != 0.0f || IO.MouseDelta.y != 0.0f)
			{
				State.EditorPosX += IO.MouseDelta.x / CanvasZoom;
				State.EditorPosY += IO.MouseDelta.y / CanvasZoom;
				MarkDirty();
			}
		}
		ImGui::PopID();
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		DraggingStateId.clear();
	}

	const int32 HoveredTransitionIndex = (!HoveredPin && !bMouseOverState) ? FindHoveredTransition(StateMachine, IO.MousePos, CanvasMin) : -1;
	if (HoveredTransitionIndex >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		FAnimStateTransitionData& Transition = StateMachine.Transitions[HoveredTransitionIndex];
		SelectedTransitionId = Transition.TransitionId;
		SelectedStateId = Transition.FromStateId;
		SelectedNodeId.clear();
		SelectedLinkIndex = -1;
	}
	else if (bCanvasHovered && !HoveredPin && !bMouseOverState && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		SelectedTransitionId.clear();
		SelectedNodeId.clear();
		SelectedLinkIndex = -1;
	}

	if (HoveredPin && !HoveredPin->Pin.bInput && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		PendingTransitionPin = HoveredPin->Pin;
		DraggingStateId.clear();
		SelectedTransitionId.clear();
	}

	if (PendingTransitionPin.IsValid())
	{
		if (const FStatePinDrawInfo* StartPin = FindDrawnStatePin(StatePins, PendingTransitionPin))
		{
			const ImVec2 P0(StartPin->X, StartPin->Y);
			const ImVec2 P3(IO.MousePos.x, IO.MousePos.y);
			const float Tangent = (std::max)(80.0f * CanvasZoom, std::fabs(P3.x - P0.x) * 0.5f);
			DrawList->AddBezierCubic(P0, ImVec2(P0.x + Tangent, P0.y), ImVec2(P3.x - Tangent, P3.y), P3, IM_COL32(245, 210, 90, 255), (std::max)(1.0f, 2.0f * CanvasZoom), 24);
		}

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (HoveredPin && HoveredPin->Pin.bInput && CanCreateTransition(StateMachine, PendingTransitionPin, HoveredPin->Pin))
			{
				CreateTransition(StateMachine, PendingTransitionPin, HoveredPin->Pin);
			}
			PendingTransitionPin = FStatePinRef();
		}
	}

	if (bCanvasHovered && !bMouseOverState && !HoveredPin && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ScreenToGraph(IO.MousePos, CanvasMin, PendingCreateGraphX, PendingCreateGraphY);
		ImGui::OpenPopup("AnimStateMachineContext");
	}

	if (ImGui::BeginPopup("AnimStateMachineContext"))
	{
		if (ImGui::MenuItem("Add State"))
		{
			AddState(Asset, PendingCreateGraphX, PendingCreateGraphY, true);
		}
		ImGui::BeginDisabled(SelectedStateId.empty());
		if (ImGui::MenuItem("Open State Graph"))
		{
			OpenStateGraph(SelectedStateId);
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	if (bCanvasHovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
	{
		bDraggingCanvas = true;
		CanvasPanX += IO.MouseDelta.x;
		CanvasPanY += IO.MouseDelta.y;
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
	{
		bDraggingCanvas = false;
	}

	if (!IO.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		if (!SelectedTransitionId.empty())
		{
			DeleteSelectedTransition(Asset);
		}
		else if (!SelectedStateId.empty())
		{
			DeleteSelectedState(Asset);
		}
	}

	DrawList->PopClipRect();
	ImGui::SetCursorScreenPos(ImVec2(CanvasMin.x, CanvasMax.y + 4.0f));
	ImGui::TextDisabled("LMB drag state | Wheel: zoom %.0f%% | RMB: add state | MMB drag: pan | Drag state output to another state | Double-click/open icon: open state graph | Delete: remove selected", CanvasZoom * 100.0f);
	ImGui::EndChild();
}

void FAnimInstanceEditorWidget::RenderDetails(UAnimInstanceAsset* Asset)
{
	FAnimGraphData* EditableGraph = GetEditableGraph();
	if (!EditableGraph)
	{
		return;
	}

	FAnimGraphData& Graph = *EditableGraph;
	if (!SelectedNodeId.empty() && !FindNode(Graph, SelectedNodeId))
	{
		SelectedNodeId.clear();
	}
	if (SelectedLinkIndex >= static_cast<int32>(Graph.Links.size()))
	{
		SelectedLinkIndex = -1;
	}

	FString SaveReason;
	if (!CanSaveAsset(Asset, &SaveReason) && !SaveReason.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "%s", SaveReason.c_str());
		ImGui::Separator();
	}

	if (ImGui::BeginTabBar("AnimInstanceDetailsTabs"))
	{
		if (ImGui::BeginTabItem("Selection"))
		{
			if (CanvasMode == ECanvasMode::PoseGraph || !Asset->HasStateMachine())
			{
				RenderNodeDetails(Asset, Graph);
			}
			else
			{
				ImGui::TextUnformatted("Selection");
				ImGui::TextDisabled("Edit selected states and transitions in the State Machine tab.");
				ImGui::Spacing();
				ImGui::BeginDisabled(SelectedStateId.empty());
				if (ImGui::Button("Open Selected State Graph", ImVec2(-1.0f, 0.0f)))
				{
					OpenStateGraph(SelectedStateId);
				}
				ImGui::EndDisabled();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("State Machine"))
		{
			RenderStateMachineDetails(Asset);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Parameters"))
		{
			RenderParameters(Asset);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Asset"))
		{
			RenderAssetDetails(Asset);
			ImGui::Separator();
			RenderValidationSummary(Asset);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

void FAnimInstanceEditorWidget::RenderAssetDetails(UAnimInstanceAsset* Asset)
{
	ImGui::TextUnformatted("Asset");
	DrawFieldLabel("Skeleton");
	DrawWrappedValue(Asset->GetSkeletonPath(), "Not selected");

	FString PreviewMeshPath = Asset->GetPreviewMeshPath();
	DrawFieldLabel("Preview Mesh");
	ImGui::SetNextItemWidth(-1.0f);
	if (InputFString("##PreviewMesh", PreviewMeshPath))
	{
		Asset->SetPreviewMeshPath(PreviewMeshPath);
		MarkDirty();
	}
}

void FAnimInstanceEditorWidget::RenderValidationSummary(UAnimInstanceAsset* Asset)
{
	ImGui::TextUnformatted("Validation");
	FString SaveReason;
	if (CanSaveAsset(Asset, &SaveReason))
	{
		ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "Ready to save");
		return;
	}

	ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "%s", SaveReason.empty() ? "Cannot save this asset." : SaveReason.c_str());
	if (FAnimGraphData* Graph = GetEditableGraph())
	{
		const int32 BrokenLinkCount = CountBrokenLinks(*Graph);
		if (BrokenLinkCount > 0)
		{
			ImGui::TextDisabled("The current State Graph has broken or duplicate connections.");
			const FString ButtonLabel = "Clean Broken Links (" + std::to_string(BrokenLinkCount) + ")";
			if (ImGui::Button(ButtonLabel.c_str(), ImVec2(-1.0f, 0.0f)))
			{
				RemoveInvalidLinks(*Graph);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Deletes only broken Pose Graph links: missing nodes, invalid pins, cycles, and duplicate input connections.");
			}
		}
	}
}

void FAnimInstanceEditorWidget::RenderStateMachineDetails(UAnimInstanceAsset* Asset)
{
	ImGui::TextUnformatted("State Machine");
	if (!Asset->HasStateMachine())
	{
		ImGui::TextDisabled("Legacy root graph mode");
		if (ImGui::Button("Create State Machine from Current Graph", ImVec2(-1.0f, 0.0f)))
		{
			CreateStateMachineFromRootGraph(Asset);
			MarkDirty();
		}
		return;
	}

	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	if (!SelectedTransitionId.empty())
	{
		if (FAnimStateTransitionData* SelectedTransition = FindTransition(StateMachine, SelectedTransitionId))
		{
			SelectedStateId = SelectedTransition->FromStateId;
		}
		else
		{
			SelectedTransitionId.clear();
		}
	}

	ImGui::TextDisabled("%zu states | %zu transitions", StateMachine.States.size(), StateMachine.Transitions.size());
	if (StateMachine.States.empty())
	{
		if (ImGui::Button("Add Entry State", ImVec2(-1.0f, 0.0f)))
		{
			AddState(Asset);
		}
		return;
	}

	DrawFieldLabel("States");
	const float StateListHeight = (std::min)(150.0f, (std::max)(72.0f, 28.0f * static_cast<float>(StateMachine.States.size()) + 12.0f));
	ImGui::BeginChild("AnimInstanceStateList", ImVec2(0.0f, StateListHeight), true);
	for (FAnimStateData& State : StateMachine.States)
	{
		ImGui::PushID(State.StateId.c_str());
		FString Label = GetStateDisplayName(State);
		if (State.StateId == StateMachine.EntryStateId)
		{
			Label = "Entry | " + Label;
		}
		if (ImGui::Selectable(Label.c_str(), SelectedStateId == State.StateId))
		{
			SelectedStateId = State.StateId;
			SelectedTransitionId.clear();
			SelectedNodeId.clear();
			SelectedLinkIndex = -1;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%zu nodes, %d outgoing transitions", State.Graph.Nodes.size(), CountOutgoingTransitions(StateMachine, State.StateId));
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	if (ImGui::Button("Add State", ImVec2(-1.0f, 0.0f)))
	{
		AddState(Asset);
	}

	FAnimStateData* SelectedState = FindState(StateMachine, SelectedStateId);
	if (!SelectedState)
	{
		return;
	}

	ImGui::PushID(SelectedState->StateId.c_str());
	DrawFieldLabel("Selected State");
	ImGui::TextDisabled("%zu nodes | %zu links", SelectedState->Graph.Nodes.size(), SelectedState->Graph.Links.size());
	DrawFieldLabel("Display Name");
	ImGui::SetNextItemWidth(-1.0f);
	if (InputFString("##StateName", SelectedState->DisplayName, 128))
	{
		MarkDirty();
	}

	if (ImGui::Button("Open State Graph", ImVec2(-1.0f, 0.0f)))
	{
		OpenStateGraph(SelectedState->StateId);
	}

	const float HalfButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	ImGui::BeginDisabled(SelectedState->StateId == StateMachine.EntryStateId);
	if (ImGui::Button("Set Entry", ImVec2(HalfButtonWidth, 0.0f)))
	{
		StateMachine.EntryStateId = SelectedState->StateId;
		MarkDirty();
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(StateMachine.States.size() <= 1);
	if (ImGui::Button("Delete State", ImVec2(HalfButtonWidth, 0.0f)))
	{
		DeleteSelectedState(Asset);
		ImGui::EndDisabled();
		ImGui::PopID();
		return;
	}
	ImGui::EndDisabled();
	ImGui::PopID();

	RenderTransitionDetails(Asset, StateMachine);
}

void FAnimInstanceEditorWidget::RenderTransitionDetails(UAnimInstanceAsset* Asset, FAnimStateMachineData& StateMachine)
{
	FAnimStateData* SelectedState = FindState(StateMachine, SelectedStateId);
	if (!SelectedState)
	{
		return;
	}

	ImGui::Spacing();
	ImGui::TextUnformatted("Outgoing Transitions");
	ImGui::BeginDisabled(StateMachine.States.size() <= 1);
	if (ImGui::Button("Add Transition", ImVec2(-1.0f, 0.0f)))
	{
		AddTransitionFromSelectedState(Asset);
	}
	ImGui::EndDisabled();

	int32 OutgoingCount = 0;
	for (int32 Index = 0; Index < static_cast<int32>(StateMachine.Transitions.size()); ++Index)
	{
		FAnimStateTransitionData& Transition = StateMachine.Transitions[Index];
		if (Transition.FromStateId != SelectedState->StateId)
		{
			continue;
		}
		++OutgoingCount;

		ImGui::PushID(Transition.TransitionId.c_str());
		const FAnimStateData* TargetState = FindState(StateMachine, Transition.ToStateId);
		FString Header = "To ";
		Header += TargetState ? GetStateDisplayName(*TargetState) : FString("Invalid");
		if (Transition.Conditions.empty())
		{
			Header += " | no condition";
		}
		else
		{
			Header += " | ";
			for (int32 ConditionIndex = 0; ConditionIndex < static_cast<int32>(Transition.Conditions.size()); ++ConditionIndex)
			{
				if (ConditionIndex > 0)
				{
					Header += " && ";
				}
				Header += GetConditionSummary(Transition.Conditions[ConditionIndex], FindParameter(Asset, Transition.Conditions[ConditionIndex].ParameterName));
			}
		}

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_DefaultOpen;
		if (SelectedTransitionId == Transition.TransitionId)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}
		const bool bOpen = ImGui::TreeNodeEx("##Transition", Flags, "%s", Header.c_str());
		if (ImGui::IsItemClicked())
		{
			SelectedTransitionId = Transition.TransitionId;
			SelectedNodeId.clear();
			SelectedLinkIndex = -1;
			CanvasMode = ECanvasMode::StateMachine;
		}
		if (bOpen)
		{
			DrawFieldLabel("Target State");
			const FString PreviewText = TargetState ? GetStateDisplayName(*TargetState) : FString("Invalid");
			const char* Preview = PreviewText.c_str();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##TargetState", Preview))
			{
				for (const FAnimStateData& Candidate : StateMachine.States)
				{
					if (Candidate.StateId == Transition.FromStateId)
					{
						continue;
					}
					const FString Label = GetStateDisplayName(Candidate);
					if (ImGui::Selectable(Label.c_str(), Transition.ToStateId == Candidate.StateId))
					{
						Transition.ToStateId = Candidate.StateId;
						MarkDirty();
					}
				}
				ImGui::EndCombo();
			}

			DrawFieldLabel("Blend Duration");
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::DragFloat("##BlendDuration", &Transition.BlendDuration, 0.01f, 0.0f, 5.0f))
			{
				MarkDirty();
			}

			DrawFieldLabel("Conditions");
			if (ImGui::Button("Add Condition", ImVec2(-1.0f, 0.0f)))
			{
				FAnimTransitionCondition Condition;
				const FAnimGraphParameter* DefaultParameter = Asset && !Asset->GetParameters().empty() ? &Asset->GetParameters().front() : nullptr;
				if (DefaultParameter)
				{
					Condition.ParameterName = DefaultParameter->Name;
					Condition.Operator = DefaultParameter->ParameterType == EAnimGraphParameterType::Bool
						? EAnimTransitionConditionOperator::IsTrue
						: EAnimTransitionConditionOperator::Greater;
				}
				Transition.Conditions.push_back(Condition);
				MarkDirty();
			}

			for (int32 ConditionIndex = 0; ConditionIndex < static_cast<int32>(Transition.Conditions.size()); ++ConditionIndex)
			{
				ImGui::PushID(ConditionIndex);
				RenderTransitionCondition(Asset, Transition.Conditions[ConditionIndex]);
				if (ImGui::Button("Delete Condition", ImVec2(-1.0f, 0.0f)))
				{
					Transition.Conditions.erase(Transition.Conditions.begin() + ConditionIndex);
					MarkDirty();
					ImGui::PopID();
					break;
				}
				ImGui::Separator();
				ImGui::PopID();
			}

			if (Transition.Conditions.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.25f, 1.0f), "Transition needs at least one condition.");
			}

			if (ImGui::Button("Delete Transition", ImVec2(-1.0f, 0.0f)))
			{
				StateMachine.Transitions.erase(StateMachine.Transitions.begin() + Index);
				SelectedTransitionId.clear();
				MarkDirty();
				ImGui::TreePop();
				ImGui::PopID();
				break;
			}
			ImGui::TreePop();
		}
			ImGui::PopID();
	}

	if (OutgoingCount == 0)
	{
		ImGui::TextDisabled("No outgoing transitions from this state.");
	}
}

void FAnimInstanceEditorWidget::RenderTransitionCondition(UAnimInstanceAsset* Asset, FAnimTransitionCondition& Condition)
{
	const FAnimGraphParameter* Parameter = FindParameter(Asset, Condition.ParameterName);

	DrawFieldLabel("Parameter");
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##ConditionParameter", Parameter ? Condition.ParameterName.c_str() : "None"))
	{
		TSet<FString> AddedNames;
		if (Asset)
		{
			auto DrawParameterOption = [&Condition, &AddedNames, this](const FAnimGraphParameter& Candidate)
			{
				if (Candidate.Name.empty() || AddedNames.find(Candidate.Name) != AddedNames.end())
				{
					return;
				}
				AddedNames.insert(Candidate.Name);
				const FString Label = Candidate.Name + " (" + GetParameterTypeName(Candidate.ParameterType) + ")";
				if (ImGui::Selectable(Label.c_str(), Condition.ParameterName == Candidate.Name))
				{
					Condition.ParameterName = Candidate.Name;
					Condition.Operator = Candidate.ParameterType == EAnimGraphParameterType::Bool
						? EAnimTransitionConditionOperator::IsTrue
						: EAnimTransitionConditionOperator::Greater;
					MarkDirty();
				}
			};

			for (const FAnimGraphParameter& Candidate : Asset->GetParameters())
			{
				DrawParameterOption(Candidate);
			}
		}
		ImGui::EndCombo();
	}

	if (!Parameter)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Missing parameter");
		return;
	}

	DrawFieldLabel("Operator");
	ImGui::SetNextItemWidth(-1.0f);
	if (Parameter->ParameterType == EAnimGraphParameterType::Bool)
	{
		const char* Preview = GetBoolConditionOperatorName(Condition.Operator);
		if (ImGui::BeginCombo("##BoolOperator", Preview))
		{
			if (ImGui::Selectable("Is True", Condition.Operator == EAnimTransitionConditionOperator::IsTrue))
			{
				Condition.Operator = EAnimTransitionConditionOperator::IsTrue;
				Condition.bBoolExpected = true;
				MarkDirty();
			}
			if (ImGui::Selectable("Is False", Condition.Operator == EAnimTransitionConditionOperator::IsFalse))
			{
				Condition.Operator = EAnimTransitionConditionOperator::IsFalse;
				Condition.bBoolExpected = false;
				MarkDirty();
			}
			ImGui::EndCombo();
		}
	}
	else
	{
		const char* Preview = GetFloatConditionOperatorName(Condition.Operator);
		if (ImGui::BeginCombo("##FloatOperator", Preview))
		{
			const EAnimTransitionConditionOperator Operators[] =
			{
				EAnimTransitionConditionOperator::Greater,
				EAnimTransitionConditionOperator::GreaterEqual,
				EAnimTransitionConditionOperator::Less,
				EAnimTransitionConditionOperator::LessEqual,
				EAnimTransitionConditionOperator::Equal,
				EAnimTransitionConditionOperator::NotEqual,
			};
			for (EAnimTransitionConditionOperator Operator : Operators)
			{
				if (ImGui::Selectable(GetFloatConditionOperatorName(Operator), Condition.Operator == Operator))
				{
					Condition.Operator = Operator;
					MarkDirty();
				}
			}
			ImGui::EndCombo();
		}

		DrawFieldLabel("Value");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##ConditionValue", &Condition.FloatThreshold, 0.01f))
		{
			MarkDirty();
		}
	}
}

void FAnimInstanceEditorWidget::RenderNodeDetails(UAnimInstanceAsset* Asset, FAnimGraphData& Graph)
{
	ImGui::TextUnformatted("Selection");
	if (SelectedLinkIndex >= 0 && SelectedLinkIndex < static_cast<int32>(Graph.Links.size()))
	{
		FAnimGraphPinLink& Link = Graph.Links[SelectedLinkIndex];
		ImGui::TextUnformatted("Link");
		DrawFieldLabel("From");
		DrawWrappedValue(GetNodeDisplayLabel(Graph, Link.FromNodeId) + "." + Link.FromPinName);
		DrawFieldLabel("To");
		DrawWrappedValue(GetNodeDisplayLabel(Graph, Link.ToNodeId) + "." + Link.ToPinName);
		bool bDuplicateInput = false;
		for (int32 LinkIndex = 0; LinkIndex < static_cast<int32>(Graph.Links.size()); ++LinkIndex)
		{
			if (LinkIndex != SelectedLinkIndex
				&& Graph.Links[LinkIndex].ToNodeId == Link.ToNodeId
				&& Graph.Links[LinkIndex].ToPinName == Link.ToPinName)
			{
				bDuplicateInput = true;
				break;
			}
		}
		if (!IsValidLink(Graph, Link) || bDuplicateInput)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Invalid link");
		}
		if (ImGui::Button("Delete Link"))
		{
			DeleteSelectedLink(Graph);
		}
		return;
	}

	FAnimGraphNodeData* Node = FindNode(Graph, SelectedNodeId);
	if (!Node)
	{
		ImGui::TextDisabled("No node selected");
		return;
	}

	ImGui::PushID(Node->NodeId.c_str());
	ImGui::TextUnformatted("Node");
	const bool bSelectedSequenceMismatch = Node->NodeType == EAnimGraphNodeType::SequencePlayer
		&& IsSequenceNodeSkeletonMismatch(Asset, *Node);
	DrawFieldLabel("Name");
	ImGui::BeginDisabled(bSelectedSequenceMismatch);
	ImGui::SetNextItemWidth(-1.0f);
	if (InputFString("##NodeName", Node->DisplayName, 128))
	{
		MarkDirty();
	}
	ImGui::EndDisabled();
	DrawFieldLabel("Type");
	ImGui::TextUnformatted(GetNodeTypeName(Node->NodeType));
	ImGui::Spacing();

	if (Node->NodeType == EAnimGraphNodeType::SequencePlayer)
	{
		RenderSequencePlayerDetails(Asset, *Node);
		ImGui::BeginDisabled(bSelectedSequenceMismatch);
		DrawFieldLabel("Play Rate");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##PlayRate", &Node->PlayRate, 0.01f, -10.0f, 10.0f))
		{
			MarkDirty();
		}
		if (ImGui::Checkbox("Loop", &Node->bLoop))
		{
			MarkDirty();
		}
		ImGui::EndDisabled();
	}
	else if (Node->NodeType == EAnimGraphNodeType::Blend2ByFloat)
	{
		bool bHasFloatParameters = false;
		if (Asset)
		{
			for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
			{
				if (Parameter.ParameterType == EAnimGraphParameterType::Float)
				{
					bHasFloatParameters = true;
					break;
				}
			}
		}
		if (!bHasFloatParameters)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.25f, 1.0f), "No global float parameters");
			if (Asset && ImGui::Button("Create Global Float Parameter", ImVec2(-1.0f, 0.0f)))
			{
				FAnimGraphParameter Parameter;
				Parameter.Name = MakeUniqueParameterName(Asset, EAnimGraphParameterType::Float);
				Parameter.ParameterType = EAnimGraphParameterType::Float;
				Parameter.DefaultFloatValue = 0.0f;
				Asset->GetParameters().push_back(Parameter);
				Node->ParameterName = Parameter.Name;
				MarkDirty();
			}
		}
		else if (!Node->ParameterName.empty() && !HasFloatParameter(Asset, Node->ParameterName))
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Missing global float parameter: %s", Node->ParameterName.c_str());
		}
		DrawFieldLabel("Global Float Parameter");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##ParameterList", Node->ParameterName.empty() ? "None" : Node->ParameterName.c_str()))
		{
			if (ImGui::Selectable("None", Node->ParameterName.empty()))
			{
				Node->ParameterName.clear();
				MarkDirty();
			}
			if (Asset)
			{
				for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
				{
					if (Parameter.ParameterType != EAnimGraphParameterType::Float)
					{
						continue;
					}
					if (ImGui::Selectable(Parameter.Name.c_str(), Node->ParameterName == Parameter.Name))
					{
						Node->ParameterName = Parameter.Name;
						MarkDirty();
					}
				}
			}
			ImGui::EndCombo();
		}
		DrawFieldLabel("Min Value");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##MinValue", &Node->MinValue, 0.01f))
		{
			MarkDirty();
		}
		DrawFieldLabel("Max Value");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##MaxValue", &Node->MaxValue, 0.01f))
		{
			MarkDirty();
		}
	}
	else if (Node->NodeType == EAnimGraphNodeType::Output)
	{
		ImGui::TextWrapped("Output node accepts one Pose input.");
	}

	ImGui::Separator();
	ImGui::BeginDisabled(Node->NodeType == EAnimGraphNodeType::Output);
	if (ImGui::Button("Delete Node"))
	{
		DeleteSelectedNode(Graph);
	}
	ImGui::EndDisabled();
	ImGui::PopID();
}

void FAnimInstanceEditorWidget::RenderSequencePlayerDetails(UAnimInstanceAsset* Asset, FAnimGraphNodeData& Node)
{
	const TArray<FEditorAnimationAssetListItem>& Options = GetAnimSequenceOptions(Asset);
	FString PreviewText = Node.AnimSequencePath.empty() ? FString("None (draft)") : GetPathDisplayName(Node.AnimSequencePath);
	if (PreviewText.empty())
	{
		PreviewText = Node.AnimSequencePath;
	}

	if (Node.AnimSequencePath.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.25f, 1.0f), "AnimSequence is not assigned.");
	}
	else
	{
		DrawFieldLabel("Current Path");
		DrawWrappedValue(Node.AnimSequencePath);

		FString ActualSkeletonPath;
		if (IsSequenceNodeSkeletonMismatch(Asset, Node, &ActualSkeletonPath))
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Skeleton mismatch");
			DrawFieldLabel("Sequence Skeleton");
			DrawWrappedValue(ActualSkeletonPath, "Unknown");
		}
	}

	DrawFieldLabel("Anim Sequence");
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##AnimSequence", PreviewText.c_str()))
	{
		if (ImGui::Selectable("None (draft)", Node.AnimSequencePath.empty()))
		{
			AssignSequenceToNode(Node, FString());
		}

		if (Options.empty())
		{
			ImGui::TextDisabled("No compatible AnimSequence assets");
		}

		for (const FEditorAnimationAssetListItem& Item : Options)
		{
			const bool bSelected = Node.AnimSequencePath == Item.FullPath;
			if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
			{
				AssignSequenceToNode(Node, Item.FullPath);
			}
			DrawWrappedValue(Item.FullPath);
		}
		ImGui::EndCombo();
	}

	ImGui::Button("Drop AnimSequence", ImVec2(-1.0f, 0.0f));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("AnimSequenceContentItem"))
		{
			FString SequencePath;
			if (TryGetAnimSequencePathFromPayload(Payload, SequencePath))
			{
				if (IsSequenceCompatibleWithAsset(Asset, SequencePath))
				{
					AssignSequenceToNode(Node, SequencePath);
				}
				else
				{
					SetStatusMessage("AnimSequence drop rejected: Skeleton mismatch or invalid AnimSequence.");
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
}

void FAnimInstanceEditorWidget::RenderSequenceCreateMenu(UAnimInstanceAsset* Asset, FAnimGraphData& Graph)
{
	if (!ImGui::BeginMenu("Sequence Player"))
	{
		return;
	}

	if (ImGui::MenuItem("Empty Draft"))
	{
		AddNode(Graph, static_cast<int32>(EAnimGraphNodeType::SequencePlayer), PendingCreateGraphX, PendingCreateGraphY);
	}

	const TArray<FEditorAnimationAssetListItem>& Options = GetAnimSequenceOptions(Asset);
	if (Options.empty())
	{
		ImGui::TextDisabled("No compatible AnimSequences");
	}

	for (const FEditorAnimationAssetListItem& Item : Options)
	{
		if (ImGui::MenuItem(Item.DisplayName.c_str(), Item.FullPath.c_str()))
		{
			AddNode(Graph, static_cast<int32>(EAnimGraphNodeType::SequencePlayer), PendingCreateGraphX, PendingCreateGraphY, Item.FullPath);
		}
	}

	ImGui::EndMenu();
}

void FAnimInstanceEditorWidget::RenderParameters(UAnimInstanceAsset* Asset)
{
	ImGui::TextUnformatted("AnimInstance Parameters");
	ImGui::TextDisabled("Global runtime inputs used by Blend nodes and State transitions.");
	if (!Asset)
	{
		return;
	}

	const float HalfButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Add Float", ImVec2(HalfButtonWidth, 0.0f)))
	{
		FAnimGraphParameter Parameter;
		Parameter.ParameterType = EAnimGraphParameterType::Float;
		Parameter.Name = MakeUniqueParameterName(Asset, Parameter.ParameterType);
		Parameter.DefaultFloatValue = 0.0f;
		Asset->GetParameters().push_back(Parameter);
		MarkDirty();
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Bool", ImVec2(HalfButtonWidth, 0.0f)))
	{
		FAnimGraphParameter Parameter;
		Parameter.ParameterType = EAnimGraphParameterType::Bool;
		Parameter.Name = MakeUniqueParameterName(Asset, Parameter.ParameterType);
		Parameter.DefaultBoolValue = false;
		Asset->GetParameters().push_back(Parameter);
		MarkDirty();
	}

	TArray<FAnimGraphParameter>& Parameters = Asset->GetParameters();
	if (Parameters.empty())
	{
		ImGui::TextDisabled("No AnimInstance parameters.");
	}

	for (int32 Index = 0; Index < static_cast<int32>(Parameters.size()); ++Index)
	{
		FAnimGraphParameter& Parameter = Parameters[Index];
		ImGui::PushID(Index);
		const FString OldName = Parameter.Name;
		DrawFieldLabel("Name");
		ImGui::SetNextItemWidth(-1.0f);
		if (InputFString("##ParameterName", Parameter.Name, 128))
		{
			RenameParameterReferences(Asset->GetGraph(), OldName, Parameter.Name);
			RenameTransitionParameterReferences(Asset, OldName, Parameter.Name);
			for (FAnimStateData& State : Asset->GetStateMachine().States)
			{
				RenameParameterReferences(State.Graph, OldName, Parameter.Name);
			}
			MarkDirty();
		}

		DrawFieldLabel("Type");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##ParameterType", GetParameterTypeName(Parameter.ParameterType)))
		{
			if (ImGui::Selectable("Float", Parameter.ParameterType == EAnimGraphParameterType::Float))
			{
				Parameter.ParameterType = EAnimGraphParameterType::Float;
				MarkDirty();
			}
			if (ImGui::Selectable("Bool", Parameter.ParameterType == EAnimGraphParameterType::Bool))
			{
				Parameter.ParameterType = EAnimGraphParameterType::Bool;
				RemoveParameterReferences(Asset->GetGraph(), Parameter.Name);
				for (FAnimStateData& State : Asset->GetStateMachine().States)
				{
					RemoveParameterReferences(State.Graph, Parameter.Name);
				}
				MarkDirty();
			}
			ImGui::EndCombo();
		}

		DrawFieldLabel("Default Value");
		ImGui::SetNextItemWidth(-1.0f);
		if (Parameter.ParameterType == EAnimGraphParameterType::Bool)
		{
			if (ImGui::Checkbox("##ParameterDefaultBool", &Parameter.DefaultBoolValue))
			{
				MarkDirty();
			}
		}
		else if (ImGui::DragFloat("##ParameterDefault", &Parameter.DefaultFloatValue, 0.01f))
		{
			MarkDirty();
		}
		if (HasParameterTypeConflict(Asset, Parameter.Name, Parameter.ParameterType))
		{
			ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "Parameter name is used with another type.");
		}
		if (ImGui::Button("Delete Parameter", ImVec2(-1.0f, 0.0f)))
		{
			RemoveParameterReferences(Asset->GetGraph(), Parameter.Name);
			for (FAnimStateData& State : Asset->GetStateMachine().States)
			{
				RemoveParameterReferences(State.Graph, Parameter.Name);
			}
			RemoveTransitionParameterReferences(Asset, Parameter.Name);
			Parameters.erase(Parameters.begin() + Index);
			MarkDirty();
			ImGui::PopID();
			break;
		}
		ImGui::Separator();
		ImGui::PopID();
	}
}

FAnimGraphNodeData* FAnimInstanceEditorWidget::FindNode(FAnimGraphData& Graph, const FString& NodeId) const
{
	for (FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (Node.NodeId == NodeId)
		{
			return &Node;
		}
	}
	return nullptr;
}

const FAnimGraphNodeData* FAnimInstanceEditorWidget::FindNode(const FAnimGraphData& Graph, const FString& NodeId) const
{
	for (const FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (Node.NodeId == NodeId)
		{
			return &Node;
		}
	}
	return nullptr;
}

FAnimStateData* FAnimInstanceEditorWidget::FindState(FAnimStateMachineData& StateMachine, const FString& StateId) const
{
	for (FAnimStateData& State : StateMachine.States)
	{
		if (State.StateId == StateId)
		{
			return &State;
		}
	}
	return nullptr;
}

const FAnimStateData* FAnimInstanceEditorWidget::FindState(const FAnimStateMachineData& StateMachine, const FString& StateId) const
{
	for (const FAnimStateData& State : StateMachine.States)
	{
		if (State.StateId == StateId)
		{
			return &State;
		}
	}
	return nullptr;
}

FAnimStateTransitionData* FAnimInstanceEditorWidget::FindTransition(FAnimStateMachineData& StateMachine, const FString& TransitionId) const
{
	for (FAnimStateTransitionData& Transition : StateMachine.Transitions)
	{
		if (Transition.TransitionId == TransitionId)
		{
			return &Transition;
		}
	}
	return nullptr;
}

const FAnimStateTransitionData* FAnimInstanceEditorWidget::FindTransition(const FAnimStateMachineData& StateMachine, const FString& TransitionId) const
{
	for (const FAnimStateTransitionData& Transition : StateMachine.Transitions)
	{
		if (Transition.TransitionId == TransitionId)
		{
			return &Transition;
		}
	}
	return nullptr;
}

int32 FAnimInstanceEditorWidget::FindNodeIndex(const FAnimGraphData& Graph, const FString& NodeId) const
{
	for (int32 Index = 0; Index < static_cast<int32>(Graph.Nodes.size()); ++Index)
	{
		if (Graph.Nodes[Index].NodeId == NodeId)
		{
			return Index;
		}
	}
	return -1;
}

int32 FAnimInstanceEditorWidget::FindStateIndex(const FAnimStateMachineData& StateMachine, const FString& StateId) const
{
	for (int32 Index = 0; Index < static_cast<int32>(StateMachine.States.size()); ++Index)
	{
		if (StateMachine.States[Index].StateId == StateId)
		{
			return Index;
		}
	}
	return -1;
}

int32 FAnimInstanceEditorWidget::FindTransitionIndex(const FAnimStateMachineData& StateMachine, const FString& TransitionId) const
{
	for (int32 Index = 0; Index < static_cast<int32>(StateMachine.Transitions.size()); ++Index)
	{
		if (StateMachine.Transitions[Index].TransitionId == TransitionId)
		{
			return Index;
		}
	}
	return -1;
}

int32 FAnimInstanceEditorWidget::FindLinkIndex(const FAnimGraphData& Graph, const FAnimGraphPinLink& Link) const
{
	for (int32 Index = 0; Index < static_cast<int32>(Graph.Links.size()); ++Index)
	{
		const FAnimGraphPinLink& Candidate = Graph.Links[Index];
		if (Candidate.FromNodeId == Link.FromNodeId
			&& Candidate.FromPinName == Link.FromPinName
			&& Candidate.ToNodeId == Link.ToNodeId
			&& Candidate.ToPinName == Link.ToPinName)
		{
			return Index;
		}
	}
	return -1;
}

int32 FAnimInstanceEditorWidget::CountOutgoingTransitions(const FAnimStateMachineData& StateMachine, const FString& StateId) const
{
	int32 Count = 0;
	for (const FAnimStateTransitionData& Transition : StateMachine.Transitions)
	{
		if (Transition.FromStateId == StateId)
		{
			++Count;
		}
	}
	return Count;
}

bool FAnimInstanceEditorWidget::HasParameter(const FAnimGraphData& Graph, const FString& ParameterName) const
{
	if (ParameterName.empty())
	{
		return false;
	}

	for (const FAnimGraphParameter& Parameter : Graph.Parameters)
	{
		if (Parameter.Name == ParameterName)
		{
			return true;
		}
	}
	return false;
}

bool FAnimInstanceEditorWidget::HasFloatParameter(const UAnimInstanceAsset* Asset, const FString& ParameterName) const
{
	if (!Asset || ParameterName.empty())
	{
		return false;
	}

	for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
	{
		if (Parameter.Name == ParameterName && Parameter.ParameterType == EAnimGraphParameterType::Float)
		{
			return true;
		}
	}
	return false;
}

const FAnimGraphParameter* FAnimInstanceEditorWidget::FindParameter(const UAnimInstanceAsset* Asset, const FString& ParameterName) const
{
	if (!Asset || ParameterName.empty())
	{
		return nullptr;
	}

	for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
	{
		if (Parameter.Name == ParameterName)
		{
			return &Parameter;
		}
	}

	return nullptr;
}

bool FAnimInstanceEditorWidget::HasParameterTypeConflict(const UAnimInstanceAsset* Asset, const FString& ParameterName, EAnimGraphParameterType ParameterType) const
{
	if (!Asset || ParameterName.empty())
	{
		return false;
	}

	for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
	{
		if (Parameter.Name == ParameterName && Parameter.ParameterType != ParameterType)
		{
			return true;
		}
	}
	return false;
}

FString FAnimInstanceEditorWidget::MakeUniqueNodeId(const FAnimGraphData& Graph, const FString& Prefix) const
{
	for (int32 Index = 0;; ++Index)
	{
		FString Candidate = Prefix + "_" + std::to_string(Index);
		if (!FindNode(Graph, Candidate))
		{
			return Candidate;
		}
	}
}

FString FAnimInstanceEditorWidget::MakeUniqueStateId(const FAnimStateMachineData& StateMachine, const FString& Prefix) const
{
	for (int32 Index = 0;; ++Index)
	{
		FString Candidate = Prefix + "_" + std::to_string(Index);
		if (!FindState(StateMachine, Candidate))
		{
			return Candidate;
		}
	}
}

FString FAnimInstanceEditorWidget::MakeUniqueTransitionId(const FAnimStateMachineData& StateMachine) const
{
	for (int32 Index = 0;; ++Index)
	{
		FString Candidate = "Transition_" + std::to_string(Index);
		bool bExists = false;
		for (const FAnimStateTransitionData& Transition : StateMachine.Transitions)
		{
			if (Transition.TransitionId == Candidate)
			{
				bExists = true;
				break;
			}
		}
		if (!bExists)
		{
			return Candidate;
		}
	}
}

FString FAnimInstanceEditorWidget::MakeUniqueParameterName(const UAnimInstanceAsset* Asset, EAnimGraphParameterType Type) const
{
	const FString BaseName = Type == EAnimGraphParameterType::Bool ? FString("bIsActive") : FString("Speed");
	for (int32 Index = 0;; ++Index)
	{
		FString Candidate = Index == 0 ? BaseName : BaseName + std::to_string(Index);
		bool bExists = false;
		if (Asset)
		{
			for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
			{
				if (Parameter.Name == Candidate)
				{
					bExists = true;
					break;
				}
			}
		}
		if (!bExists)
		{
			return Candidate;
		}
	}
}

void FAnimInstanceEditorWidget::AddNode(FAnimGraphData& Graph, int32 NodeTypeValue, float GraphX, float GraphY, const FString& SequencePath)
{
	const EAnimGraphNodeType NodeType = static_cast<EAnimGraphNodeType>(NodeTypeValue);
	if (NodeType == EAnimGraphNodeType::Output && FindNode(Graph, Graph.OutputNodeId))
	{
		return;
	}

	FAnimGraphNodeData Node;
	const FString Prefix = GetNodeIdPrefix(NodeType);
	Node.NodeId = MakeUniqueNodeId(Graph, Prefix);
	Node.DisplayName = GetNodeTypeName(NodeType);
	Node.NodeType = NodeType;
	Node.EditorPosX = GraphX;
	Node.EditorPosY = GraphY;
	Node.AnimSequencePath = FPaths::MakeProjectRelative(SequencePath);
	if (NodeType == EAnimGraphNodeType::Blend2ByFloat)
	{
		if (UAnimInstanceAsset* Asset = GetAsset())
		{
			for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
			{
				if (Parameter.ParameterType == EAnimGraphParameterType::Float)
				{
					Node.ParameterName = Parameter.Name;
					break;
				}
			}
		}
	}
	if (NodeType == EAnimGraphNodeType::Output)
	{
		Graph.OutputNodeId = Node.NodeId;
	}

	SelectedNodeId = Node.NodeId;
	SelectedLinkIndex = -1;
	Graph.Nodes.push_back(Node);
	MarkDirty();
}

void FAnimInstanceEditorWidget::CreateStateMachineFromRootGraph(UAnimInstanceAsset* Asset)
{
	if (!Asset)
	{
		return;
	}

	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	if (StateMachine.HasStates())
	{
		return;
	}

	FAnimStateData State;
	State.StateId = MakeUniqueStateId(StateMachine, "State");
	State.DisplayName = "Default";
	State.Graph = Asset->GetGraph();
	State.EditorPosX = 64.0f;
	State.EditorPosY = 64.0f;
	EnsureGraphInvariant(State.Graph);

	StateMachine.EntryStateId = State.StateId;
	StateMachine.States.push_back(State);
	SelectedStateId = State.StateId;
	SelectedNodeId = State.Graph.OutputNodeId;
	SelectedLinkIndex = -1;
	SelectedTransitionId.clear();
	CanvasMode = ECanvasMode::StateMachine;
}

void FAnimInstanceEditorWidget::AddState(UAnimInstanceAsset* Asset, float GraphX, float GraphY, bool bUseExplicitPosition)
{
	if (!Asset)
	{
		return;
	}

	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	FAnimStateData State;
	State.StateId = MakeUniqueStateId(StateMachine, "State");
	State.DisplayName = "State";
	State.EditorPosX = bUseExplicitPosition ? GraphX : 80.0f + static_cast<float>(StateMachine.States.size()) * 32.0f;
	State.EditorPosY = bUseExplicitPosition ? GraphY : 80.0f;
	EnsureGraphInvariant(State.Graph);

	if (StateMachine.EntryStateId.empty())
	{
		StateMachine.EntryStateId = State.StateId;
	}

	SelectedStateId = State.StateId;
	SelectedTransitionId.clear();
	SelectedNodeId = State.Graph.OutputNodeId;
	SelectedLinkIndex = -1;
	StateMachine.States.push_back(State);
	CanvasMode = ECanvasMode::StateMachine;
	MarkDirty();
}

void FAnimInstanceEditorWidget::DeleteSelectedState(UAnimInstanceAsset* Asset)
{
	if (!Asset || !Asset->HasStateMachine())
	{
		return;
	}

	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	const int32 StateIndex = FindStateIndex(StateMachine, SelectedStateId);
	if (StateIndex < 0 || StateMachine.States.size() <= 1)
	{
		return;
	}

	const FString RemovedStateId = StateMachine.States[StateIndex].StateId;
	StateMachine.States.erase(StateMachine.States.begin() + StateIndex);
	StateMachine.Transitions.erase(std::remove_if(StateMachine.Transitions.begin(), StateMachine.Transitions.end(),
		[&RemovedStateId](const FAnimStateTransitionData& Transition)
		{
			return Transition.FromStateId == RemovedStateId || Transition.ToStateId == RemovedStateId;
		}),
		StateMachine.Transitions.end());

	if (StateMachine.EntryStateId == RemovedStateId)
	{
		StateMachine.EntryStateId = StateMachine.States.front().StateId;
	}

	SelectedStateId = StateMachine.EntryStateId;
	SelectedTransitionId.clear();
	SelectedNodeId.clear();
	SelectedLinkIndex = -1;
	PendingTransitionPin = FStatePinRef();
	DraggingStateId.clear();
	CanvasMode = ECanvasMode::StateMachine;
	MarkDirty();
}

void FAnimInstanceEditorWidget::AddTransitionFromSelectedState(UAnimInstanceAsset* Asset)
{
	if (!Asset || !Asset->HasStateMachine())
	{
		return;
	}

	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	FAnimStateData* FromState = FindState(StateMachine, SelectedStateId);
	if (!FromState || StateMachine.States.size() <= 1)
	{
		return;
	}

	const FAnimStateData* TargetState = nullptr;
	for (const FAnimStateData& Candidate : StateMachine.States)
	{
		if (Candidate.StateId != FromState->StateId)
		{
			TargetState = &Candidate;
			break;
		}
	}
	if (!TargetState)
	{
		return;
	}

	FAnimStateTransitionData Transition;
	Transition.TransitionId = MakeUniqueTransitionId(StateMachine);
	Transition.FromStateId = FromState->StateId;
	Transition.ToStateId = TargetState->StateId;
	Transition.BlendDuration = 0.2f;

	if (!Asset->GetParameters().empty())
	{
		FAnimTransitionCondition Condition;
		Condition.ParameterName = Asset->GetParameters().front().Name;
		Condition.Operator = Asset->GetParameters().front().ParameterType == EAnimGraphParameterType::Bool
			? EAnimTransitionConditionOperator::IsTrue
			: EAnimTransitionConditionOperator::Greater;
		Transition.Conditions.push_back(Condition);
	}

	SelectedTransitionId = Transition.TransitionId;
	SelectedNodeId.clear();
	SelectedLinkIndex = -1;
	CanvasMode = ECanvasMode::StateMachine;
	StateMachine.Transitions.push_back(Transition);
	MarkDirty();
}

void FAnimInstanceEditorWidget::DeleteSelectedTransition(UAnimInstanceAsset* Asset)
{
	if (!Asset || !Asset->HasStateMachine() || SelectedTransitionId.empty())
	{
		return;
	}

	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	const int32 TransitionIndex = FindTransitionIndex(StateMachine, SelectedTransitionId);
	if (TransitionIndex < 0)
	{
		SelectedTransitionId.clear();
		return;
	}

	StateMachine.Transitions.erase(StateMachine.Transitions.begin() + TransitionIndex);
	SelectedTransitionId.clear();
	PendingTransitionPin = FStatePinRef();
	MarkDirty();
}

void FAnimInstanceEditorWidget::OpenStateGraph(const FString& StateId)
{
	UAnimInstanceAsset* Asset = GetAsset();
	if (!Asset || !Asset->HasStateMachine())
	{
		return;
	}

	FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	FAnimStateData* State = FindState(StateMachine, StateId);
	if (!State)
	{
		return;
	}

	SelectedStateId = State->StateId;
	SelectedTransitionId.clear();
	SelectedNodeId = State->Graph.OutputNodeId;
	SelectedLinkIndex = -1;
	PendingTransitionPin = FStatePinRef();
	DraggingStateId.clear();
	CanvasMode = ECanvasMode::PoseGraph;
}

void FAnimInstanceEditorWidget::ReturnToStateMachine()
{
	CanvasMode = ECanvasMode::StateMachine;
	SelectedNodeId.clear();
	SelectedLinkIndex = -1;
	PendingLinkPin = FPinRef();
	DraggingNodeId.clear();
}

void FAnimInstanceEditorWidget::DeleteSelectedNode(FAnimGraphData& Graph)
{
	const int32 NodeIndex = FindNodeIndex(Graph, SelectedNodeId);
	if (NodeIndex < 0 || Graph.Nodes[NodeIndex].NodeType == EAnimGraphNodeType::Output)
	{
		return;
	}

	const FString RemovedNodeId = Graph.Nodes[NodeIndex].NodeId;
	Graph.Nodes.erase(Graph.Nodes.begin() + NodeIndex);
	Graph.Links.erase(std::remove_if(Graph.Links.begin(), Graph.Links.end(),
		[&RemovedNodeId](const FAnimGraphPinLink& Link)
		{
			return Link.FromNodeId == RemovedNodeId || Link.ToNodeId == RemovedNodeId;
		}),
		Graph.Links.end());
	SelectedNodeId.clear();
	SelectedLinkIndex = -1;
	MarkDirty();
}

void FAnimInstanceEditorWidget::DeleteSelectedLink(FAnimGraphData& Graph)
{
	if (SelectedLinkIndex < 0 || SelectedLinkIndex >= static_cast<int32>(Graph.Links.size()))
	{
		return;
	}

	Graph.Links.erase(Graph.Links.begin() + SelectedLinkIndex);
	SelectedLinkIndex = -1;
	MarkDirty();
}

int32 FAnimInstanceEditorWidget::CountBrokenLinks(const FAnimGraphData& Graph) const
{
	int32 BrokenCount = 0;
	TSet<FString> LinkedInputs;
	for (const FAnimGraphPinLink& Link : Graph.Links)
	{
		if (!IsValidLink(Graph, Link))
		{
			++BrokenCount;
			continue;
		}

		const FString InputKey = Link.ToNodeId + "." + Link.ToPinName;
		if (LinkedInputs.find(InputKey) != LinkedInputs.end())
		{
			++BrokenCount;
			continue;
		}
		LinkedInputs.insert(InputKey);
	}
	return BrokenCount;
}

void FAnimInstanceEditorWidget::RemoveInvalidLinks(FAnimGraphData& Graph)
{
	const size_t OldCount = Graph.Links.size();
	TSet<FString> LinkedInputs;
	Graph.Links.erase(std::remove_if(Graph.Links.begin(), Graph.Links.end(),
		[this, &Graph, &LinkedInputs](const FAnimGraphPinLink& Link)
		{
			if (!IsValidLink(Graph, Link))
			{
				return true;
			}

			const FString InputKey = Link.ToNodeId + "." + Link.ToPinName;
			if (LinkedInputs.find(InputKey) != LinkedInputs.end())
			{
				return true;
			}
			LinkedInputs.insert(InputKey);
			return false;
		}),
		Graph.Links.end());
	if (Graph.Links.size() != OldCount)
	{
		const int32 RemovedCount = static_cast<int32>(OldCount - Graph.Links.size());
		SelectedLinkIndex = -1;
		MarkDirty();
		SetStatusMessage("Cleaned " + std::to_string(RemovedCount) + " broken graph link(s).");
	}
}

void FAnimInstanceEditorWidget::RenameParameterReferences(FAnimGraphData& Graph, const FString& OldName, const FString& NewName)
{
	if (OldName.empty() || OldName == NewName)
	{
		return;
	}

	for (FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat && Node.ParameterName == OldName)
		{
			Node.ParameterName = NewName;
		}
	}
}

void FAnimInstanceEditorWidget::RenameTransitionParameterReferences(UAnimInstanceAsset* Asset, const FString& OldName, const FString& NewName)
{
	if (!Asset || OldName.empty() || OldName == NewName)
	{
		return;
	}

	for (FAnimStateTransitionData& Transition : Asset->GetStateMachine().Transitions)
	{
		for (FAnimTransitionCondition& Condition : Transition.Conditions)
		{
			if (Condition.ParameterName == OldName)
			{
				Condition.ParameterName = NewName;
			}
		}
	}
}

void FAnimInstanceEditorWidget::RemoveParameterReferences(FAnimGraphData& Graph, const FString& ParameterName)
{
	if (ParameterName.empty())
	{
		return;
	}

	for (FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat && Node.ParameterName == ParameterName)
		{
			Node.ParameterName.clear();
		}
	}
}

void FAnimInstanceEditorWidget::RemoveTransitionParameterReferences(UAnimInstanceAsset* Asset, const FString& ParameterName)
{
	if (!Asset || ParameterName.empty())
	{
		return;
	}

	for (FAnimStateTransitionData& Transition : Asset->GetStateMachine().Transitions)
	{
		for (FAnimTransitionCondition& Condition : Transition.Conditions)
		{
			if (Condition.ParameterName == ParameterName)
			{
				Condition.ParameterName.clear();
			}
		}
	}
}

void FAnimInstanceEditorWidget::RefreshSkeletonOptions()
{
	SkeletonOptions = FEditorAnimationAssetLibrary::ScanSkeletonAssets();
	bSkeletonOptionsLoaded = true;
}

void FAnimInstanceEditorWidget::RefreshAnimSequenceOptions(const FString& SkeletonPath)
{
	CachedAnimSequenceSkeletonPath = FPaths::MakeProjectRelative(SkeletonPath);
	AnimSequenceOptions = FEditorAnimationAssetLibrary::ScanAnimSequencesForSkeleton(CachedAnimSequenceSkeletonPath);
}

const TArray<FEditorAnimationAssetListItem>& FAnimInstanceEditorWidget::GetAnimSequenceOptions(UAnimInstanceAsset* Asset)
{
	const FString SkeletonPath = Asset ? FPaths::MakeProjectRelative(Asset->GetSkeletonPath()) : FString();
	if (SkeletonPath != CachedAnimSequenceSkeletonPath)
	{
		RefreshAnimSequenceOptions(SkeletonPath);
	}
	return AnimSequenceOptions;
}

void FAnimInstanceEditorWidget::AssignSequenceToNode(FAnimGraphNodeData& Node, const FString& SequencePath)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(SequencePath);
	if (Node.AnimSequencePath == NormalizedPath)
	{
		return;
	}

	Node.AnimSequencePath = NormalizedPath;
	MarkDirty();
}

void FAnimInstanceEditorWidget::SetStatusMessage(const FString& Message)
{
	StatusMessage = Message;
}

FString FAnimInstanceEditorWidget::GetCurrentGraphLabel(const UAnimInstanceAsset* Asset) const
{
	if (!Asset)
	{
		return "No graph";
	}

	if (!Asset->HasStateMachine())
	{
		return "Root Graph";
	}

	const FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	const FAnimStateData* State = FindState(StateMachine, SelectedStateId);
	if (!State && !StateMachine.EntryStateId.empty())
	{
		State = FindState(StateMachine, StateMachine.EntryStateId);
	}
	if (!State && !StateMachine.States.empty())
	{
		State = &StateMachine.States.front();
	}

	return State ? FString("State Graph: ") + GetStateDisplayName(*State) : FString("State Graph");
}

FString FAnimInstanceEditorWidget::GetNodeDisplayLabel(const FAnimGraphData& Graph, const FString& NodeId) const
{
	const FAnimGraphNodeData* Node = FindNode(Graph, NodeId);
	if (!Node)
	{
		return NodeId.empty() ? FString("Missing Node") : FString("Missing: ") + NodeId;
	}

	if (!Node->DisplayName.empty())
	{
		return Node->DisplayName;
	}
	return GetNodeTypeName(Node->NodeType);
}

bool FAnimInstanceEditorWidget::IsValidInputPin(const FAnimGraphNodeData& Node, const FString& PinName) const
{
	if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat)
	{
		return PinName == PinA || PinName == PinB;
	}
	if (Node.NodeType == EAnimGraphNodeType::Output)
	{
		return PinName == PinPose;
	}
	return false;
}

bool FAnimInstanceEditorWidget::IsValidOutputPin(const FAnimGraphNodeData& Node, const FString& PinName) const
{
	return PinName == PinPose
		&& (Node.NodeType == EAnimGraphNodeType::SequencePlayer || Node.NodeType == EAnimGraphNodeType::Blend2ByFloat);
}

bool FAnimInstanceEditorWidget::IsValidLink(const FAnimGraphData& Graph, const FAnimGraphPinLink& Link) const
{
	const FAnimGraphNodeData* FromNode = FindNode(Graph, Link.FromNodeId);
	const FAnimGraphNodeData* ToNode = FindNode(Graph, Link.ToNodeId);
	return FromNode
		&& ToNode
		&& FromNode != ToNode
		&& IsValidOutputPin(*FromNode, Link.FromPinName)
		&& IsValidInputPin(*ToNode, Link.ToPinName)
		&& !DoesPathReachNode(Graph, Link.ToNodeId, Link.FromNodeId);
}

bool FAnimInstanceEditorWidget::CanCreateLink(const FAnimGraphData& Graph, const FPinRef& FromPin, const FPinRef& ToPin) const
{
	if (!FromPin.IsValid() || !ToPin.IsValid() || FromPin.bInput || !ToPin.bInput || FromPin.NodeId == ToPin.NodeId)
	{
		return false;
	}

	FAnimGraphPinLink Link;
	Link.FromNodeId = FromPin.NodeId;
	Link.FromPinName = FromPin.PinName;
	Link.ToNodeId = ToPin.NodeId;
	Link.ToPinName = ToPin.PinName;
	return IsValidLink(Graph, Link)
		&& !DoesPathReachNode(Graph, ToPin.NodeId, FromPin.NodeId);
}

bool FAnimInstanceEditorWidget::CanCreateTransition(const FAnimStateMachineData& StateMachine, const FStatePinRef& FromPin, const FStatePinRef& ToPin) const
{
	return FromPin.IsValid()
		&& ToPin.IsValid()
		&& !FromPin.bInput
		&& ToPin.bInput
		&& FromPin.StateId != ToPin.StateId
		&& FindState(StateMachine, FromPin.StateId)
		&& FindState(StateMachine, ToPin.StateId);
}

bool FAnimInstanceEditorWidget::IsTransitionInvalid(const UAnimInstanceAsset* Asset, const FAnimStateMachineData& StateMachine, const FAnimStateTransitionData& Transition) const
{
	if (!FindState(StateMachine, Transition.FromStateId)
		|| !FindState(StateMachine, Transition.ToStateId)
		|| Transition.FromStateId == Transition.ToStateId
		|| Transition.Conditions.empty())
	{
		return true;
	}

	for (const FAnimTransitionCondition& Condition : Transition.Conditions)
	{
		const FAnimGraphParameter* Parameter = FindParameter(Asset, Condition.ParameterName);
		if (!Parameter)
		{
			return true;
		}

		if (Parameter->ParameterType == EAnimGraphParameterType::Bool
			&& Condition.Operator != EAnimTransitionConditionOperator::IsTrue
			&& Condition.Operator != EAnimTransitionConditionOperator::IsFalse)
		{
			return true;
		}

		if (Parameter->ParameterType == EAnimGraphParameterType::Float
			&& (Condition.Operator == EAnimTransitionConditionOperator::IsTrue
				|| Condition.Operator == EAnimTransitionConditionOperator::IsFalse))
		{
			return true;
		}
	}

	return false;
}

bool FAnimInstanceEditorWidget::DoesPathReachNode(const FAnimGraphData& Graph, const FString& StartNodeId, const FString& TargetNodeId) const
{
	if (StartNodeId.empty() || TargetNodeId.empty())
	{
		return false;
	}

	TArray<FString> Stack;
	TSet<FString> Visited;
	Stack.push_back(StartNodeId);

	while (!Stack.empty())
	{
		const FString CurrentNodeId = Stack.back();
		Stack.pop_back();

		if (CurrentNodeId == TargetNodeId)
		{
			return true;
		}

		if (Visited.find(CurrentNodeId) != Visited.end())
		{
			continue;
		}
		Visited.insert(CurrentNodeId);

		for (const FAnimGraphPinLink& Link : Graph.Links)
		{
			if (Link.FromNodeId == CurrentNodeId)
			{
				Stack.push_back(Link.ToNodeId);
			}
		}
	}

	return false;
}

bool FAnimInstanceEditorWidget::HasSkeleton(const UAnimInstanceAsset* Asset) const
{
	return Asset && !Asset->GetSkeletonPath().empty();
}

bool FAnimInstanceEditorWidget::CanSaveAsset(UAnimInstanceAsset* Asset, FString* OutReason) const
{
	if (OutReason)
	{
		OutReason->clear();
	}

	if (!Asset)
	{
		if (OutReason)
		{
			*OutReason = "No AnimInstance asset.";
		}
		return false;
	}

	if (!HasSkeleton(Asset))
	{
		if (OutReason)
		{
			*OutReason = "Select a Skeleton first.";
		}
		return false;
	}

	for (const FAnimGraphParameter& Parameter : Asset->GetParameters())
	{
		if (HasParameterTypeConflict(Asset, Parameter.Name, Parameter.ParameterType))
		{
			if (OutReason)
			{
				*OutReason = "Parameter name is used with multiple types.";
			}
			return false;
		}
	}

	if (Asset->HasStateMachine())
	{
		return CanSaveStateMachine(Asset, OutReason);
	}

	return CanSaveGraph(Asset, Asset->GetGraph(), OutReason);
}

bool FAnimInstanceEditorWidget::CanSaveGraph(const UAnimInstanceAsset* Asset, const FAnimGraphData& Graph, FString* OutReason) const
{
	TSet<FString> LinkedInputs;
	for (const FAnimGraphPinLink& Link : Graph.Links)
	{
		if (!IsValidLink(Graph, Link))
		{
			if (OutReason)
			{
				*OutReason = "Invalid link or cycle exists.";
			}
			return false;
		}

		const FString InputKey = Link.ToNodeId + "." + Link.ToPinName;
		if (LinkedInputs.find(InputKey) != LinkedInputs.end())
		{
			if (OutReason)
			{
				*OutReason = "Multiple links target the same input pin.";
			}
			return false;
		}
		LinkedInputs.insert(InputKey);
	}

	for (const FAnimGraphNodeData& Node : Graph.Nodes)
	{
		if (IsSequenceNodeSkeletonMismatch(Asset, Node))
		{
			if (OutReason)
			{
				*OutReason = "Fix Skeleton-mismatched AnimSequence nodes.";
			}
			return false;
		}

		if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat)
		{
			if (Node.ParameterName.empty())
			{
				if (OutReason)
				{
					*OutReason = "Blend2 node needs a global float parameter.";
				}
				return false;
			}

			if (!HasFloatParameter(Asset, Node.ParameterName))
			{
				if (OutReason)
				{
					*OutReason = "Blend2 node references a missing global float parameter.";
				}
				return false;
			}
		}
	}

	return true;
}

bool FAnimInstanceEditorWidget::CanSaveStateMachine(const UAnimInstanceAsset* Asset, FString* OutReason) const
{
	if (!Asset)
	{
		return false;
	}

	const FAnimStateMachineData& StateMachine = Asset->GetStateMachine();
	if (StateMachine.States.empty())
	{
		if (OutReason)
		{
			*OutReason = "State Machine has no states.";
		}
		return false;
	}

	if (!FindState(StateMachine, StateMachine.EntryStateId))
	{
		if (OutReason)
		{
			*OutReason = "State Machine entry state is invalid.";
		}
		return false;
	}

	for (const FAnimStateData& State : StateMachine.States)
	{
		if (!CanSaveGraph(Asset, State.Graph, OutReason))
		{
			return false;
		}
	}

	for (const FAnimStateTransitionData& Transition : StateMachine.Transitions)
	{
		if (!FindState(StateMachine, Transition.FromStateId) || !FindState(StateMachine, Transition.ToStateId))
		{
			if (OutReason)
			{
				*OutReason = "Transition references a missing state.";
			}
			return false;
		}

		if (Transition.FromStateId == Transition.ToStateId)
		{
			if (OutReason)
			{
				*OutReason = "Self transitions are not supported.";
			}
			return false;
		}

		if (Transition.Conditions.empty())
		{
			if (OutReason)
			{
				*OutReason = "Transition has no conditions.";
			}
			return false;
		}

		for (const FAnimTransitionCondition& Condition : Transition.Conditions)
		{
			const FAnimGraphParameter* Parameter = FindParameter(Asset, Condition.ParameterName);
			if (!Parameter)
			{
				if (OutReason)
				{
					*OutReason = "Transition condition references a missing parameter.";
				}
				return false;
			}

			if (Parameter->ParameterType == EAnimGraphParameterType::Bool
				&& Condition.Operator != EAnimTransitionConditionOperator::IsTrue
				&& Condition.Operator != EAnimTransitionConditionOperator::IsFalse)
			{
				if (OutReason)
				{
					*OutReason = "Bool transition condition has an invalid operator.";
				}
				return false;
			}

			if (Parameter->ParameterType == EAnimGraphParameterType::Float
				&& (Condition.Operator == EAnimTransitionConditionOperator::IsTrue
					|| Condition.Operator == EAnimTransitionConditionOperator::IsFalse))
			{
				if (OutReason)
				{
					*OutReason = "Float transition condition has an invalid operator.";
				}
				return false;
			}
		}
	}

	return true;
}

bool FAnimInstanceEditorWidget::IsSequenceCompatibleWithAsset(const UAnimInstanceAsset* Asset, const FString& SequencePath, FString* OutActualSkeletonPath) const
{
	if (SequencePath.empty())
	{
		if (OutActualSkeletonPath)
		{
			OutActualSkeletonPath->clear();
		}
		return true;
	}

	return Asset
		&& FEditorAnimationAssetLibrary::IsAnimSequenceCompatibleWithSkeleton(SequencePath, Asset->GetSkeletonPath(), OutActualSkeletonPath);
}

bool FAnimInstanceEditorWidget::IsSequenceNodeSkeletonMismatch(const UAnimInstanceAsset* Asset, const FAnimGraphNodeData& Node, FString* OutActualSkeletonPath) const
{
	if (OutActualSkeletonPath)
	{
		OutActualSkeletonPath->clear();
	}

	if (Node.NodeType != EAnimGraphNodeType::SequencePlayer || Node.AnimSequencePath.empty())
	{
		return false;
	}

	return !IsSequenceCompatibleWithAsset(Asset, Node.AnimSequencePath, OutActualSkeletonPath);
}

bool FAnimInstanceEditorWidget::TryGetAnimSequencePathFromPayload(const ImGuiPayload* Payload, FString& OutPath) const
{
	OutPath.clear();
	if (!Payload || !Payload->Data || Payload->DataSize < static_cast<int>(sizeof(FContentItem)))
	{
		return false;
	}

	const FContentItem* Item = static_cast<const FContentItem*>(Payload->Data);
	OutPath = FPaths::MakeProjectRelative(FPaths::ToUtf8(Item->Path.lexically_normal().generic_wstring()));
	return !OutPath.empty();
}

bool FAnimInstanceEditorWidget::MatchesFilter(const FString& Text, const char* Filter) const
{
	if (!Filter || Filter[0] == '\0')
	{
		return true;
	}

	FString LowerText = Text;
	FString LowerFilter = Filter;
	std::transform(LowerText.begin(), LowerText.end(), LowerText.begin(),
		[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
	std::transform(LowerFilter.begin(), LowerFilter.end(), LowerFilter.begin(),
		[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
	return LowerText.find(LowerFilter) != FString::npos;
}

void FAnimInstanceEditorWidget::CreateLink(FAnimGraphData& Graph, const FPinRef& FromPin, const FPinRef& ToPin)
{
	Graph.Links.erase(std::remove_if(Graph.Links.begin(), Graph.Links.end(),
		[&ToPin](const FAnimGraphPinLink& Link)
		{
			return Link.ToNodeId == ToPin.NodeId && Link.ToPinName == ToPin.PinName;
		}),
		Graph.Links.end());

	FAnimGraphPinLink Link;
	Link.FromNodeId = FromPin.NodeId;
	Link.FromPinName = FromPin.PinName;
	Link.ToNodeId = ToPin.NodeId;
	Link.ToPinName = ToPin.PinName;
	Graph.Links.push_back(Link);
}

void FAnimInstanceEditorWidget::DrawGraphGrid(const ImVec2& CanvasMin, const ImVec2& CanvasMax, ImDrawList* DrawList) const
{
	DrawList->AddRectFilled(CanvasMin, CanvasMax, IM_COL32(22, 23, 27, 255));
	const float Step = (std::max)(8.0f, GridStep * CanvasZoom);
	const float OffsetX = std::fmod(CanvasPanX, Step);
	const float OffsetY = std::fmod(CanvasPanY, Step);
	for (float X = CanvasMin.x + OffsetX; X < CanvasMax.x; X += Step)
	{
		DrawList->AddLine(ImVec2(X, CanvasMin.y), ImVec2(X, CanvasMax.y), IM_COL32(42, 44, 50, 255));
	}
	for (float Y = CanvasMin.y + OffsetY; Y < CanvasMax.y; Y += Step)
	{
		DrawList->AddLine(ImVec2(CanvasMin.x, Y), ImVec2(CanvasMax.x, Y), IM_COL32(42, 44, 50, 255));
	}
	DrawList->AddRect(CanvasMin, CanvasMax, IM_COL32(68, 72, 82, 255));
}

void FAnimInstanceEditorWidget::DrawNode(const FAnimGraphNodeData& Node, const ImVec2& CanvasMin, ImDrawList* DrawList, TArray<FPinDrawInfo>& OutPins) const
{
	(void)OutPins;
	const ImVec2 Pos = GraphToScreen(Node, CanvasMin);
	const ImVec2 Size = GetNodeSize(Node);
	const ImVec2 Max(Pos.x + Size.x, Pos.y + Size.y);
	const bool bSelected = SelectedNodeId == Node.NodeId;
	const float Zoom = CanvasZoom;
	const float TextLineHeight = ImGui::GetTextLineHeight() * Zoom;
	const float ContentLeft = Pos.x + NodePaddingX * Zoom;
	const float ContentRight = Max.x - NodePaddingX * Zoom;
	const float ContentWidth = ContentRight - ContentLeft;

	DrawList->AddRectFilled(Pos, Max, IM_COL32(35, 37, 43, 255), NodeRounding * Zoom);
	DrawList->AddRectFilled(Pos, ImVec2(Max.x, Pos.y + NodeHeaderHeight * Zoom), GetNodeAccentColor(Node.NodeType), NodeRounding * Zoom, ImDrawFlags_RoundCornersTop);
	DrawList->AddRect(Pos, Max, bSelected ? IM_COL32(255, 230, 100, 255) : IM_COL32(82, 86, 96, 255), NodeRounding * Zoom, 0, (std::max)(1.0f, (bSelected ? 2.0f : 1.0f) * Zoom));

	const FString Title = Node.DisplayName.empty() ? FString(GetNodeTypeName(Node.NodeType)) : Node.DisplayName;
	DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + (NodeHeaderHeight * Zoom - TextLineHeight) * 0.5f), ContentWidth, IM_COL32(20, 22, 26, 255), Title, Zoom);
	DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + 38.0f * Zoom), ContentWidth, IM_COL32(185, 190, 200, 255), GetNodeTypeName(Node.NodeType), Zoom);
	DrawList->AddLine(ImVec2(ContentLeft, Pos.y + 58.0f * Zoom), ImVec2(ContentRight, Pos.y + 58.0f * Zoom), IM_COL32(54, 58, 66, 255), (std::max)(1.0f, Zoom));

	auto DrawPin = [&](const FString& PinName, bool bInput)
	{
		const ImVec2 PinPos = GetPinScreenPosition(Node, PinName, bInput, CanvasMin);
		const ImU32 Color = bInput ? IM_COL32(95, 170, 245, 255) : IM_COL32(105, 210, 130, 255);
		DrawList->AddCircleFilled(PinPos, 5.0f * Zoom, Color);
		DrawList->AddCircle(PinPos, 5.0f * Zoom, IM_COL32(12, 14, 18, 255), 0, (std::max)(1.0f, Zoom));

		const float LabelY = PinPos.y - TextLineHeight * 0.5f;
		if (bInput)
		{
			const float LabelX = PinPos.x + 14.0f * Zoom;
			const float LabelMaxWidth = ContentRight - LabelX;
			DrawClippedText(DrawList, ImVec2(LabelX, LabelY), LabelMaxWidth, IM_COL32(220, 224, 232, 255), PinName, Zoom);
		}
		else
		{
			const float LabelRight = PinPos.x - 14.0f * Zoom;
			const float LabelMaxWidth = LabelRight - ContentLeft;
			const FString FittedPinName = FitTextToWidth(PinName, LabelMaxWidth, Zoom);
			if (!FittedPinName.empty())
			{
				const float LabelX = LabelRight - ImGui::CalcTextSize(FittedPinName.c_str()).x * Zoom;
				DrawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * Zoom, ImVec2(LabelX, LabelY), IM_COL32(220, 224, 232, 255), FittedPinName.c_str());
			}
		}
	};

	if (Node.NodeType == EAnimGraphNodeType::SequencePlayer)
	{
		DrawPin(PinPose, false);
		FString SequenceName = Node.AnimSequencePath.empty() ? FString("None") : GetPathDisplayName(Node.AnimSequencePath);
		if (SequenceName.empty())
		{
			SequenceName = Node.AnimSequencePath;
		}
		DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + 100.0f * Zoom), ContentWidth, IM_COL32(150, 155, 165, 255), FString("Sequence: ") + SequenceName, Zoom);
	}
	else if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat)
	{
		DrawPin(PinA, true);
		DrawPin(PinB, true);
		DrawPin(PinPose, false);
		const FString ParamText = Node.ParameterName.empty() ? FString("Param: None") : FString("Param: ") + Node.ParameterName;
		DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + 124.0f * Zoom), ContentWidth, IM_COL32(150, 155, 165, 255), ParamText, Zoom);
	}
	else if (Node.NodeType == EAnimGraphNodeType::Output)
	{
		DrawPin(PinPose, true);
		DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + 72.0f * Zoom), ContentWidth, IM_COL32(150, 155, 165, 255), "Final Pose", Zoom);
	}
}

void FAnimInstanceEditorWidget::DrawLink(const FAnimGraphData& Graph, const FAnimGraphPinLink& Link, int32 LinkIndex, const TArray<FPinDrawInfo>& Pins, ImDrawList* DrawList, bool bInvalid) const
{
	(void)Graph;
	const FPinRef FromPin{ Link.FromNodeId, Link.FromPinName, false };
	const FPinRef ToPin{ Link.ToNodeId, Link.ToPinName, true };
	const FPinDrawInfo* From = FindDrawnPin(Pins, FromPin);
	const FPinDrawInfo* To = FindDrawnPin(Pins, ToPin);
	if (!From || !To)
	{
		return;
	}

	const ImVec2 P0(From->X, From->Y);
	const ImVec2 P3(To->X, To->Y);
	const float Tangent = (std::max)(80.0f * CanvasZoom, std::fabs(P3.x - P0.x) * 0.5f);
	const ImU32 Color = bInvalid
		? IM_COL32(245, 90, 75, 210)
		: (SelectedLinkIndex == LinkIndex ? IM_COL32(255, 230, 100, 255) : IM_COL32(150, 160, 175, 220));
	DrawList->AddBezierCubic(P0, ImVec2(P0.x + Tangent, P0.y), ImVec2(P3.x - Tangent, P3.y), P3, Color, (std::max)(1.0f, (SelectedLinkIndex == LinkIndex ? 3.0f : 2.0f) * CanvasZoom), 24);
}

void FAnimInstanceEditorWidget::DrawStateNode(const UAnimInstanceAsset* Asset, const FAnimStateMachineData& StateMachine, const FAnimStateData& State, const ImVec2& CanvasMin, ImDrawList* DrawList, TArray<FStatePinDrawInfo>& OutPins) const
{
	const ImVec2 Pos = StateToScreen(State, CanvasMin);
	const ImVec2 Size = GetStateNodeSize(State);
	const ImVec2 Max(Pos.x + Size.x, Pos.y + Size.y);
	const bool bEntry = State.StateId == StateMachine.EntryStateId;
	const bool bSelected = SelectedStateId == State.StateId && SelectedTransitionId.empty();
	const FAnimStateTransitionData* SelectedTransition = SelectedTransitionId.empty() ? nullptr : FindTransition(StateMachine, SelectedTransitionId);
	const bool bTransitionEndpoint = SelectedTransition
		&& (SelectedTransition->FromStateId == State.StateId || SelectedTransition->ToStateId == State.StateId);
	const bool bGraphInvalid = Asset && !CanSaveGraph(Asset, State.Graph, nullptr);
	const float Zoom = CanvasZoom;
	const float TextLineHeight = ImGui::GetTextLineHeight() * Zoom;
	const float ContentLeft = Pos.x + NodePaddingX * Zoom;
	const float ContentRight = Max.x - NodePaddingX * Zoom;
	const float ContentWidth = ContentRight - ContentLeft;

	const ImU32 HeaderColor = bEntry ? IM_COL32(92, 190, 122, 255) : IM_COL32(82, 132, 210, 255);
	ImU32 BorderColor = IM_COL32(82, 86, 96, 255);
	if (bSelected)
	{
		BorderColor = IM_COL32(255, 230, 100, 255);
	}
	else if (bTransitionEndpoint)
	{
		BorderColor = IM_COL32(125, 190, 255, 255);
	}
	else if (bGraphInvalid)
	{
		BorderColor = IM_COL32(245, 90, 75, 255);
	}

	DrawList->AddRectFilled(Pos, Max, IM_COL32(35, 37, 43, 255), NodeRounding * Zoom);
	DrawList->AddRectFilled(Pos, ImVec2(Max.x, Pos.y + NodeHeaderHeight * Zoom), HeaderColor, NodeRounding * Zoom, ImDrawFlags_RoundCornersTop);
	DrawList->AddRect(Pos, Max, BorderColor, NodeRounding * Zoom, 0, (std::max)(1.0f, (bSelected || bTransitionEndpoint || bGraphInvalid ? 2.0f : 1.0f) * Zoom));

	const float TitleWidth = (std::max)(20.0f * Zoom, ContentWidth - 30.0f * Zoom);
	DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + (NodeHeaderHeight * Zoom - TextLineHeight) * 0.5f), TitleWidth, IM_COL32(20, 22, 26, 255), GetStateDisplayName(State), Zoom);

	const float OpenIconSize = 18.0f * Zoom;
	const float OpenIconMargin = 6.0f * Zoom;
	const ImVec2 OpenIconMin(Max.x - OpenIconMargin - OpenIconSize, Pos.y + OpenIconMargin);
	const ImVec2 OpenIconMax(OpenIconMin.x + OpenIconSize, OpenIconMin.y + OpenIconSize);
	const ImU32 OpenIconColor = bSelected ? IM_COL32(255, 238, 150, 255) : IM_COL32(24, 28, 34, 210);
	const ImU32 OpenIconLineColor = bSelected ? IM_COL32(20, 22, 26, 255) : IM_COL32(230, 236, 245, 255);
	DrawList->AddRectFilled(OpenIconMin, OpenIconMax, OpenIconColor, 3.0f * Zoom);
	DrawList->AddRect(OpenIconMin, OpenIconMax, IM_COL32(20, 22, 26, 255), 3.0f * Zoom, 0, (std::max)(1.0f, Zoom));
	const ImVec2 ArrowStart(OpenIconMin.x + 5.0f * Zoom, OpenIconMax.y - 5.0f * Zoom);
	const ImVec2 ArrowEnd(OpenIconMax.x - 5.0f * Zoom, OpenIconMin.y + 5.0f * Zoom);
	DrawList->AddLine(ArrowStart, ArrowEnd, OpenIconLineColor, (std::max)(1.0f, 1.4f * Zoom));
	DrawList->AddLine(ArrowEnd, ImVec2(ArrowEnd.x - 5.0f * Zoom, ArrowEnd.y), OpenIconLineColor, (std::max)(1.0f, 1.4f * Zoom));
	DrawList->AddLine(ArrowEnd, ImVec2(ArrowEnd.x, ArrowEnd.y + 5.0f * Zoom), OpenIconLineColor, (std::max)(1.0f, 1.4f * Zoom));

	DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + 40.0f * Zoom), ContentWidth, IM_COL32(185, 190, 200, 255), bEntry ? "Entry State" : "State", Zoom);

	const FString Stats = FString("Nodes ") + std::to_string(State.Graph.Nodes.size())
		+ " | Links " + std::to_string(State.Graph.Links.size())
		+ " | Out " + std::to_string(CountOutgoingTransitions(StateMachine, State.StateId));
	DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + 64.0f * Zoom), ContentWidth, IM_COL32(150, 155, 165, 255), Stats, Zoom);
	if (bGraphInvalid)
	{
		DrawClippedText(DrawList, ImVec2(ContentLeft, Pos.y + 88.0f * Zoom), ContentWidth, IM_COL32(245, 110, 95, 255), "Pose graph has validation issues", Zoom);
	}

	const ImVec2 InPin = GetStatePinScreenPosition(State, true, CanvasMin);
	const ImVec2 OutPin = GetStatePinScreenPosition(State, false, CanvasMin);
	OutPins.push_back({ { State.StateId, true }, InPin.x, InPin.y });
	OutPins.push_back({ { State.StateId, false }, OutPin.x, OutPin.y });

	auto DrawPin = [&](const ImVec2& PinPos, bool bInput)
	{
		const ImU32 Color = bInput ? IM_COL32(95, 170, 245, 255) : IM_COL32(105, 210, 130, 255);
		DrawList->AddCircleFilled(PinPos, 5.5f * Zoom, Color);
		DrawList->AddCircle(PinPos, 5.5f * Zoom, IM_COL32(12, 14, 18, 255), 0, (std::max)(1.0f, Zoom));
	};
	DrawPin(InPin, true);
	DrawPin(OutPin, false);
}

void FAnimInstanceEditorWidget::DrawTransitionLink(const UAnimInstanceAsset* Asset, const FAnimStateMachineData& StateMachine, const FAnimStateTransitionData& Transition, int32 TransitionIndex, const ImVec2& CanvasMin, ImDrawList* DrawList) const
{
	const FAnimStateData* FromState = FindState(StateMachine, Transition.FromStateId);
	const FAnimStateData* ToState = FindState(StateMachine, Transition.ToStateId);
	if (!FromState || !ToState)
	{
		return;
	}

	const ImVec2 P0 = GetStatePinScreenPosition(*FromState, false, CanvasMin);
	const ImVec2 P3 = GetStatePinScreenPosition(*ToState, true, CanvasMin);
	const float Tangent = (std::max)(90.0f * CanvasZoom, std::fabs(P3.x - P0.x) * 0.5f);
	const ImVec2 P1(P0.x + Tangent, P0.y);
	const ImVec2 P2(P3.x - Tangent, P3.y);
	const bool bSelected = SelectedTransitionId == Transition.TransitionId;
	const bool bInvalid = IsTransitionInvalid(Asset, StateMachine, Transition);
	const ImU32 Color = bInvalid
		? IM_COL32(245, 90, 75, 220)
		: (bSelected ? IM_COL32(255, 230, 100, 255) : IM_COL32(150, 160, 175, 220));
	DrawList->AddBezierCubic(P0, P1, P2, P3, Color, (std::max)(1.0f, (bSelected ? 3.0f : 2.0f) * CanvasZoom), 24);

	const ImVec2 ArrowFrom = BezierPoint(P0, P1, P2, P3, 0.93f);
	ImVec2 Direction(P3.x - ArrowFrom.x, P3.y - ArrowFrom.y);
	const float Length = std::sqrt(Direction.x * Direction.x + Direction.y * Direction.y);
	if (Length > 0.001f)
	{
		Direction.x /= Length;
		Direction.y /= Length;
		const ImVec2 Perp(-Direction.y, Direction.x);
		const float ArrowSize = 9.0f * CanvasZoom;
		const ImVec2 Tip = P3;
		const ImVec2 Left(Tip.x - Direction.x * ArrowSize + Perp.x * ArrowSize * 0.55f, Tip.y - Direction.y * ArrowSize + Perp.y * ArrowSize * 0.55f);
		const ImVec2 Right(Tip.x - Direction.x * ArrowSize - Perp.x * ArrowSize * 0.55f, Tip.y - Direction.y * ArrowSize - Perp.y * ArrowSize * 0.55f);
		DrawList->AddTriangleFilled(Tip, Left, Right, Color);
	}

	const FString Summary = GetTransitionSummary(Asset, Transition);
	const FString Label = bInvalid ? FString("Invalid: ") + Summary : Summary;
	const FString FittedLabel = FitTextToWidth(Label, 180.0f * CanvasZoom, CanvasZoom);
	if (!FittedLabel.empty())
	{
		const ImVec2 Mid = BezierPoint(P0, P1, P2, P3, 0.5f);
		const float LabelWidth = ImGui::CalcTextSize(FittedLabel.c_str()).x * CanvasZoom;
		const float LabelHeight = ImGui::GetTextLineHeight() * CanvasZoom;
		const float Pad = 4.0f * CanvasZoom;
		const ImVec2 LabelMin(Mid.x - LabelWidth * 0.5f - Pad, Mid.y - LabelHeight * 0.5f - Pad);
		const ImVec2 LabelMax(Mid.x + LabelWidth * 0.5f + Pad, Mid.y + LabelHeight * 0.5f + Pad);
		DrawList->AddRectFilled(LabelMin, LabelMax, IM_COL32(24, 26, 31, 230), 4.0f * CanvasZoom);
		DrawList->AddRect(LabelMin, LabelMax, Color, 4.0f * CanvasZoom);
		DrawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * CanvasZoom, ImVec2(Mid.x - LabelWidth * 0.5f, Mid.y - LabelHeight * 0.5f), Color, FittedLabel.c_str());
	}

	(void)TransitionIndex;
}

const FAnimInstanceEditorWidget::FPinDrawInfo* FAnimInstanceEditorWidget::FindDrawnPin(const TArray<FPinDrawInfo>& Pins, const FPinRef& Pin) const
{
	for (const FPinDrawInfo& DrawnPin : Pins)
	{
		if (DrawnPin.Pin.NodeId == Pin.NodeId && DrawnPin.Pin.PinName == Pin.PinName && DrawnPin.Pin.bInput == Pin.bInput)
		{
			return &DrawnPin;
		}
	}
	return nullptr;
}

const FAnimInstanceEditorWidget::FPinDrawInfo* FAnimInstanceEditorWidget::FindHoveredPin(const TArray<FPinDrawInfo>& Pins, const ImVec2& MousePos) const
{
	for (const FPinDrawInfo& Pin : Pins)
	{
		const float Radius = (std::max)(6.0f, PinHitRadius * CanvasZoom);
		if (DistanceSquared(MousePos, ImVec2(Pin.X, Pin.Y)) <= Radius * Radius)
		{
			return &Pin;
		}
	}
	return nullptr;
}

const FAnimInstanceEditorWidget::FStatePinDrawInfo* FAnimInstanceEditorWidget::FindDrawnStatePin(const TArray<FStatePinDrawInfo>& Pins, const FStatePinRef& Pin) const
{
	for (const FStatePinDrawInfo& DrawnPin : Pins)
	{
		if (DrawnPin.Pin.StateId == Pin.StateId && DrawnPin.Pin.bInput == Pin.bInput)
		{
			return &DrawnPin;
		}
	}
	return nullptr;
}

const FAnimInstanceEditorWidget::FStatePinDrawInfo* FAnimInstanceEditorWidget::FindHoveredStatePin(const TArray<FStatePinDrawInfo>& Pins, const ImVec2& MousePos) const
{
	for (const FStatePinDrawInfo& Pin : Pins)
	{
		const float Radius = (std::max)(6.0f, PinHitRadius * CanvasZoom);
		if (DistanceSquared(MousePos, ImVec2(Pin.X, Pin.Y)) <= Radius * Radius)
		{
			return &Pin;
		}
	}
	return nullptr;
}

int32 FAnimInstanceEditorWidget::FindHoveredLink(const FAnimGraphData& Graph, const TArray<FPinDrawInfo>& Pins, const ImVec2& MousePos) const
{
	int32 HitIndex = -1;
	const float HitDistance = (std::max)(6.0f, 8.0f * CanvasZoom);
	float BestDistanceSq = HitDistance * HitDistance;
	for (int32 LinkIndex = 0; LinkIndex < static_cast<int32>(Graph.Links.size()); ++LinkIndex)
	{
		const FAnimGraphPinLink& Link = Graph.Links[LinkIndex];
		const FPinDrawInfo* From = FindDrawnPin(Pins, { Link.FromNodeId, Link.FromPinName, false });
		const FPinDrawInfo* To = FindDrawnPin(Pins, { Link.ToNodeId, Link.ToPinName, true });
		if (!From || !To)
		{
			continue;
		}

		const ImVec2 P0(From->X, From->Y);
		const ImVec2 P3(To->X, To->Y);
		const float Tangent = (std::max)(80.0f * CanvasZoom, std::fabs(P3.x - P0.x) * 0.5f);
		const ImVec2 P1(P0.x + Tangent, P0.y);
		const ImVec2 P2(P3.x - Tangent, P3.y);

		ImVec2 Prev = P0;
		for (int32 Segment = 1; Segment <= 24; ++Segment)
		{
			const float T = static_cast<float>(Segment) / 24.0f;
			const ImVec2 Current = BezierPoint(P0, P1, P2, P3, T);
			const float DistSq = DistanceToSegmentSquared(MousePos, Prev, Current);
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				HitIndex = LinkIndex;
			}
			Prev = Current;
		}
	}
	return HitIndex;
}

int32 FAnimInstanceEditorWidget::FindHoveredTransition(const FAnimStateMachineData& StateMachine, const ImVec2& MousePos, const ImVec2& CanvasMin) const
{
	int32 HitIndex = -1;
	const float HitDistance = (std::max)(6.0f, 8.0f * CanvasZoom);
	float BestDistanceSq = HitDistance * HitDistance;
	for (int32 TransitionIndex = 0; TransitionIndex < static_cast<int32>(StateMachine.Transitions.size()); ++TransitionIndex)
	{
		const FAnimStateTransitionData& Transition = StateMachine.Transitions[TransitionIndex];
		const FAnimStateData* FromState = FindState(StateMachine, Transition.FromStateId);
		const FAnimStateData* ToState = FindState(StateMachine, Transition.ToStateId);
		if (!FromState || !ToState)
		{
			continue;
		}

		const ImVec2 P0 = GetStatePinScreenPosition(*FromState, false, CanvasMin);
		const ImVec2 P3 = GetStatePinScreenPosition(*ToState, true, CanvasMin);
		const float Tangent = (std::max)(90.0f * CanvasZoom, std::fabs(P3.x - P0.x) * 0.5f);
		const ImVec2 P1(P0.x + Tangent, P0.y);
		const ImVec2 P2(P3.x - Tangent, P3.y);

		ImVec2 Prev = P0;
		for (int32 Segment = 1; Segment <= 24; ++Segment)
		{
			const float T = static_cast<float>(Segment) / 24.0f;
			const ImVec2 Current = BezierPoint(P0, P1, P2, P3, T);
			const float DistSq = DistanceToSegmentSquared(MousePos, Prev, Current);
			if (DistSq < BestDistanceSq)
			{
				BestDistanceSq = DistSq;
				HitIndex = TransitionIndex;
			}
			Prev = Current;
		}
	}
	return HitIndex;
}

ImVec2 FAnimInstanceEditorWidget::GraphToScreen(const FAnimGraphNodeData& Node, const ImVec2& CanvasMin) const
{
	return ImVec2(CanvasMin.x + CanvasPanX + Node.EditorPosX * CanvasZoom, CanvasMin.y + CanvasPanY + Node.EditorPosY * CanvasZoom);
}

ImVec2 FAnimInstanceEditorWidget::StateToScreen(const FAnimStateData& State, const ImVec2& CanvasMin) const
{
	return ImVec2(CanvasMin.x + CanvasPanX + State.EditorPosX * CanvasZoom, CanvasMin.y + CanvasPanY + State.EditorPosY * CanvasZoom);
}

void FAnimInstanceEditorWidget::ScreenToGraph(const ImVec2& ScreenPos, const ImVec2& CanvasMin, float& OutX, float& OutY) const
{
	const float SafeZoom = (std::max)(0.01f, CanvasZoom);
	OutX = (ScreenPos.x - CanvasMin.x - CanvasPanX) / SafeZoom;
	OutY = (ScreenPos.y - CanvasMin.y - CanvasPanY) / SafeZoom;
}

ImVec2 FAnimInstanceEditorWidget::GetNodeSize(const FAnimGraphNodeData& Node) const
{
	switch (Node.NodeType)
	{
	case EAnimGraphNodeType::Blend2ByFloat:
		return ImVec2(DefaultNodeWidth * CanvasZoom, 152.0f * CanvasZoom);
	case EAnimGraphNodeType::Output:
		return ImVec2(OutputNodeWidth * CanvasZoom, 96.0f * CanvasZoom);
	case EAnimGraphNodeType::SequencePlayer:
	default:
		return ImVec2(DefaultNodeWidth * CanvasZoom, 126.0f * CanvasZoom);
	}
}

ImVec2 FAnimInstanceEditorWidget::GetPinScreenPosition(const FAnimGraphNodeData& Node, const FString& PinName, bool bInput, const ImVec2& CanvasMin) const
{
	const ImVec2 Pos = GraphToScreen(Node, CanvasMin);
	const ImVec2 Size = GetNodeSize(Node);
	float Y = Pos.y + 76.0f * CanvasZoom;
	if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat && PinName == PinA)
	{
		Y = Pos.y + 72.0f * CanvasZoom;
	}
	else if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat && PinName == PinB)
	{
		Y = Pos.y + 98.0f * CanvasZoom;
	}
	else if (Node.NodeType == EAnimGraphNodeType::Blend2ByFloat && PinName == PinPose)
	{
		Y = Pos.y + 85.0f * CanvasZoom;
	}
	else if (Node.NodeType == EAnimGraphNodeType::Output)
	{
		Y = Pos.y + 62.0f * CanvasZoom;
	}

	return ImVec2(bInput ? Pos.x : Pos.x + Size.x, Y);
}

ImVec2 FAnimInstanceEditorWidget::GetStateNodeSize(const FAnimStateData& State) const
{
	(void)State;
	return ImVec2(StateNodeWidth * CanvasZoom, StateNodeHeight * CanvasZoom);
}

ImVec2 FAnimInstanceEditorWidget::GetStatePinScreenPosition(const FAnimStateData& State, bool bInput, const ImVec2& CanvasMin) const
{
	const ImVec2 Pos = StateToScreen(State, CanvasMin);
	const ImVec2 Size = GetStateNodeSize(State);
	return ImVec2(bInput ? Pos.x : Pos.x + Size.x, Pos.y + Size.y * 0.5f);
}

FString FAnimInstanceEditorWidget::GetTransitionSummary(const UAnimInstanceAsset* Asset, const FAnimStateTransitionData& Transition) const
{
	if (Transition.Conditions.empty())
	{
		return "No condition";
	}

	FString Summary;
	for (int32 ConditionIndex = 0; ConditionIndex < static_cast<int32>(Transition.Conditions.size()); ++ConditionIndex)
	{
		if (ConditionIndex > 0)
		{
			Summary += " && ";
		}
		const FAnimTransitionCondition& Condition = Transition.Conditions[ConditionIndex];
		Summary += GetConditionSummary(Condition, FindParameter(Asset, Condition.ParameterName));
	}
	return Summary;
}

void FAnimInstanceEditorWidget::CreateTransition(FAnimStateMachineData& StateMachine, const FStatePinRef& FromPin, const FStatePinRef& ToPin)
{
	if (!CanCreateTransition(StateMachine, FromPin, ToPin))
	{
		return;
	}

	for (const FAnimStateTransitionData& ExistingTransition : StateMachine.Transitions)
	{
		if (ExistingTransition.FromStateId == FromPin.StateId && ExistingTransition.ToStateId == ToPin.StateId)
		{
			SelectedTransitionId = ExistingTransition.TransitionId;
			SelectedStateId = ExistingTransition.FromStateId;
			return;
		}
	}

	FAnimStateTransitionData Transition;
	Transition.TransitionId = MakeUniqueTransitionId(StateMachine);
	Transition.FromStateId = FromPin.StateId;
	Transition.ToStateId = ToPin.StateId;
	Transition.BlendDuration = 0.2f;

	if (const UAnimInstanceAsset* Asset = GetAsset())
	{
		if (!Asset->GetParameters().empty())
		{
			FAnimTransitionCondition Condition;
			Condition.ParameterName = Asset->GetParameters().front().Name;
			Condition.Operator = Asset->GetParameters().front().ParameterType == EAnimGraphParameterType::Bool
				? EAnimTransitionConditionOperator::IsTrue
				: EAnimTransitionConditionOperator::Greater;
			Transition.Conditions.push_back(Condition);
		}
	}

	SelectedTransitionId = Transition.TransitionId;
	SelectedStateId = Transition.FromStateId;
	SelectedNodeId.clear();
	SelectedLinkIndex = -1;
	StateMachine.Transitions.push_back(Transition);
	MarkDirty();
}

bool FAnimInstanceEditorWidget::InputFString(const char* Label, FString& Value, size_t BufferSize)
{
	TArray<char> Buffer;
	Buffer.resize(BufferSize);
	std::snprintf(Buffer.data(), Buffer.size(), "%s", Value.c_str());
	if (ImGui::InputText(Label, Buffer.data(), Buffer.size()))
	{
		Value = Buffer.data();
		return true;
	}
	return false;
}
