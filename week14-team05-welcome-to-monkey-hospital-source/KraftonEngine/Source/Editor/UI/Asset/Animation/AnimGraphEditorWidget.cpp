#include "Editor/UI/Asset/Animation/AnimGraphEditorWidget.h"

#include "Animation/Graph/AnimGraphAsset.h"
#include "Animation/Graph/AnimGraphManager.h"
#include "Animation/Graph/AnimGraphTypes.h"
#include "Animation/AnimInstance.h"
#include "Asset/AssetRegistry.h"
#include "Core/Types/PropertyTypes.h"
#include "Object/Object.h"
#include "Object/Reflection/UClass.h"
#include "Serialization/MemoryArchive.h"

#include "imgui.h"
#include "imgui_node_editor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cfloat>
#include <cmath>

namespace ed = ax::NodeEditor;

namespace
{
	// 데이터 모델의 동일 namespace id 공간을 그대로 imgui-node-editor 의 NodeId/PinId/LinkId 로 캐스팅.
	// 0 == invalid 를 양쪽이 공유하므로 안전.
	inline ed::NodeId ToNodeId(uint32 Id) { return static_cast<ed::NodeId>(Id); }
	inline ed::PinId  ToPinId (uint32 Id) { return static_cast<ed::PinId >(Id); }
	inline ed::LinkId ToLinkId(uint32 Id) { return static_cast<ed::LinkId>(Id); }

	inline uint32 NodeIdToU32(ed::NodeId Id) { return static_cast<uint32>(Id.Get()); }
	inline uint32 PinIdToU32 (ed::PinId  Id) { return static_cast<uint32>(Id.Get()); }
	inline uint32 LinkIdToU32(ed::LinkId Id) { return static_cast<uint32>(Id.Get()); }

	// Variable Get 노드 생성/변수 타입 변경 경로에서 모두 사용된다.
	// RenderGetVariableAction 이 helper 정의보다 먼저 배치되어 있으므로 전방 선언이 필요하다.
	void SetVariableGetOutputType(FAnimGraphNode& Node, EAnimGraphPinType Type);

	const char* NodeTypeLabel(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
			case EAnimGraphNodeType::OutputPose:          return "Output Pose";
			case EAnimGraphNodeType::SequencePlayer:      return "Sequence Player";
			case EAnimGraphNodeType::StateMachine:        return "State Machine";
			case EAnimGraphNodeType::Slot:                return "Slot";
			case EAnimGraphNodeType::LayeredBlendPerBone: return "Layered Blend";
			case EAnimGraphNodeType::BlendListByEnum:     return "Blend List By Enum";
			case EAnimGraphNodeType::VariableGet:         return "Variable Get";
			case EAnimGraphNodeType::RefPose:             return "Ref Pose";
		}
		return "Node";
	}

	// 노드 헤더 텍스트 색상 — 노드 종류 한눈에 구분. UE Blueprint 의 카테고리 컬러 컨벤션 차용.
	ImVec4 NodeHeaderColor(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
			case EAnimGraphNodeType::OutputPose:          return ImVec4(0.95f, 0.45f, 0.45f, 1.0f); // 빨강 — 종착점
			case EAnimGraphNodeType::SequencePlayer:      return ImVec4(0.40f, 0.75f, 1.00f, 1.0f); // 파랑 — leaf pose
			case EAnimGraphNodeType::Slot:                return ImVec4(0.60f, 0.95f, 0.65f, 1.0f); // 녹색 — montage 진입점
			case EAnimGraphNodeType::LayeredBlendPerBone: return ImVec4(0.75f, 0.60f, 0.95f, 1.0f); // 연보라 — blender
			case EAnimGraphNodeType::BlendListByEnum:     return ImVec4(0.70f, 0.70f, 0.95f, 1.0f); // 연보라
			case EAnimGraphNodeType::StateMachine:        return ImVec4(0.90f, 0.55f, 0.95f, 1.0f); // 보라 — FSM
			case EAnimGraphNodeType::VariableGet:         return ImVec4(0.95f, 0.85f, 0.40f, 1.0f); // 노랑 — data
			case EAnimGraphNodeType::RefPose:             return ImVec4(0.70f, 0.70f, 0.70f, 1.0f); // 회색 — neutral leaf
		}
		return ImVec4(1, 1, 1, 1);
	}

	FString GetStemFromPath(const FString& Path)
	{
		if (Path.empty()) return FString();
		const std::filesystem::path P(Path);
		return std::filesystem::path(P).stem().string();
	}

	const char* TransitionOpLabel(ETransitionOp Op)
	{
		switch (Op)
		{
			case ETransitionOp::Greater:      return ">";
			case ETransitionOp::GreaterEqual: return ">=";
			case ETransitionOp::Less:         return "<";
			case ETransitionOp::LessEqual:    return "<=";
			case ETransitionOp::Equal:        return "==";
			case ETransitionOp::NotEqual:     return "!=";
		}
		return "?";
	}

	const char* TransitionRuleKindLabel(ETransitionRuleKind Kind)
	{
		switch (Kind)
		{
			case ETransitionRuleKind::FloatCompare:         return "Float Compare";
			case ETransitionRuleKind::BoolProperty:         return "Bool Property";
			case ETransitionRuleKind::TimeRemaining:        return "Time Remaining";
			case ETransitionRuleKind::TimeRemainingRatio:   return "Time Remaining Ratio";
			case ETransitionRuleKind::TimeElapsed:          return "Current State Time";
			case ETransitionRuleKind::AutomaticSequenceEnd: return "Automatic Sequence End";
			case ETransitionRuleKind::AlwaysTrue:           return "Always True";
			case ETransitionRuleKind::AlwaysFalse:          return "Always False";
		}
		return "Rule";
	}

	const char* TransitionRuleKindSearchText(ETransitionRuleKind Kind)
	{
		switch (Kind)
		{
			case ETransitionRuleKind::FloatCompare:         return "property access float compare speed greater less transition rule condition";
			case ETransitionRuleKind::BoolProperty:         return "bool boolean property access variable transition rule condition";
			case ETransitionRuleKind::TimeRemaining:        return "time remaining seconds asset player current state transition function";
			case ETransitionRuleKind::TimeRemainingRatio:   return "time remaining ratio fraction normalized current state transition function";
			case ETransitionRuleKind::TimeElapsed:          return "current state time elapsed seconds transition function";
			case ETransitionRuleKind::AutomaticSequenceEnd: return "automatic sequence end complete finished transition rule";
			case ETransitionRuleKind::AlwaysTrue:           return "always true unconditional transition rule";
			case ETransitionRuleKind::AlwaysFalse:          return "always false disabled transition rule";
		}
		return "transition rule";
	}

	FString FormatTransitionSummary(const FAnimGraphTransition& T);
	FString FormatTransitionRuleNodeTitle(const FAnimGraphTransition& T);
	bool StringContainsInsensitive(const FString& Haystack, const char* Needle);

	ImVec4 PinTypeColor(EAnimGraphPinType Type)
	{
		// UE AnimGraph 에서 pose 링크는 밝은 흰색/회색 실행선, 데이터 핀은 타입 컬러로 읽힌다.
		switch (Type)
		{
			case EAnimGraphPinType::Pose:  return ImVec4(0.92f, 0.94f, 0.96f, 1.0f);
			case EAnimGraphPinType::Float: return ImVec4(0.28f, 0.92f, 0.18f, 1.0f);
			case EAnimGraphPinType::Bool:  return ImVec4(0.88f, 0.20f, 0.16f, 1.0f);
			case EAnimGraphPinType::Int:   return ImVec4(0.32f, 0.72f, 1.00f, 1.0f);
			case EAnimGraphPinType::Name:  return ImVec4(0.80f, 0.52f, 1.00f, 1.0f);
		}
		return ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
	}

	ImVec4 NodeTitleColor(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
			case EAnimGraphNodeType::OutputPose:          return ImVec4(0.98f, 0.88f, 0.66f, 1.0f);
			case EAnimGraphNodeType::SequencePlayer:      return ImVec4(1.00f, 0.70f, 0.66f, 1.0f);
			case EAnimGraphNodeType::StateMachine:        return ImVec4(0.72f, 0.82f, 0.56f, 1.0f);
			case EAnimGraphNodeType::Slot:                return ImVec4(0.62f, 0.96f, 0.70f, 1.0f);
			case EAnimGraphNodeType::LayeredBlendPerBone: return ImVec4(0.82f, 0.68f, 1.00f, 1.0f);
			case EAnimGraphNodeType::BlendListByEnum:     return ImVec4(0.78f, 0.70f, 1.00f, 1.0f);
			case EAnimGraphNodeType::VariableGet:         return ImVec4(0.42f, 1.00f, 0.24f, 1.0f);
			case EAnimGraphNodeType::RefPose:             return ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
		}
		return ImVec4(1, 1, 1, 1);
	}

	ImVec4 NodeHeaderTopColor(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
			case EAnimGraphNodeType::OutputPose:          return ImVec4(0.36f, 0.30f, 0.23f, 1.0f);
			case EAnimGraphNodeType::SequencePlayer:      return ImVec4(0.38f, 0.17f, 0.16f, 1.0f);
			case EAnimGraphNodeType::StateMachine:        return ImVec4(0.18f, 0.25f, 0.12f, 1.0f);
			case EAnimGraphNodeType::Slot:                return ImVec4(0.12f, 0.28f, 0.16f, 1.0f);
			case EAnimGraphNodeType::LayeredBlendPerBone: return ImVec4(0.25f, 0.18f, 0.36f, 1.0f);
			case EAnimGraphNodeType::BlendListByEnum:     return ImVec4(0.23f, 0.19f, 0.38f, 1.0f);
			case EAnimGraphNodeType::VariableGet:         return ImVec4(0.08f, 0.25f, 0.08f, 1.0f);
			case EAnimGraphNodeType::RefPose:             return ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
		}
		return ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
	}

	ImVec4 NodeHeaderBottomColor(EAnimGraphNodeType Type)
	{
		const ImVec4 Top = NodeHeaderTopColor(Type);
		return ImVec4(Top.x * 0.36f, Top.y * 0.36f, Top.z * 0.36f, 1.0f);
	}

	const char* PinTypeLabel(EAnimGraphPinType Type)
	{
		switch (Type)
		{
			case EAnimGraphPinType::Pose:  return "Pose";
			case EAnimGraphPinType::Float: return "Float";
			case EAnimGraphPinType::Bool:  return "Bool";
			case EAnimGraphPinType::Int:   return "Int";
			case EAnimGraphPinType::Name:  return "Name";
		}
		return "Pin";
	}

	ImU32 ToColorU32(const ImVec4& C)
	{
		return ImGui::ColorConvertFloat4ToU32(C);
	}

	bool IsAnimGraphPinLinked(const UAnimGraphAsset& Asset, uint32 PinId)
	{
		for (const FAnimGraphLink& Link : Asset.GetLinks())
		{
			if (Link.FromPinId == PinId || Link.ToPinId == PinId) return true;
		}
		return false;
	}

	EAnimGraphPinType ResolveLinkType(const UAnimGraphAsset& Asset, const FAnimGraphLink& Link)
	{
		if (const FAnimGraphPin* Pin = Asset.FindPin(Link.FromPinId)) return Pin->Type;
		if (const FAnimGraphPin* Pin = Asset.FindPin(Link.ToPinId)) return Pin->Type;
		return EAnimGraphPinType::Pose;
	}

	void DrawAnimGraphPinIcon(EAnimGraphPinType Type, bool bConnected)
	{
		const ImVec4 Color = PinTypeColor(Type);
		const float Diameter = ImGui::GetTextLineHeight() * 0.92f;
		const ImVec2 Size(Diameter, Diameter);
		const ImVec2 P = ImGui::GetCursorScreenPos();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Center(P.x + Size.x * 0.5f, P.y + Size.y * 0.5f);
		const float Radius = Size.x * 0.42f;
		const ImU32 Col = ToColorU32(Color);
		if (DrawList)
		{
			if (Type == EAnimGraphPinType::Pose)
			{
				// UE 의 pose 핀은 데이터 핀 원형과 다르게 작은 포즈/실행 아이콘처럼 읽히도록 다이아몬드로 표현.
				const ImVec2 A(Center.x, Center.y - Radius);
				const ImVec2 B(Center.x + Radius, Center.y);
				const ImVec2 C(Center.x, Center.y + Radius);
				const ImVec2 D(Center.x - Radius, Center.y);
				if (bConnected)
				{
					DrawList->AddQuadFilled(A, B, C, D, Col);
				}
				else
				{
					DrawList->AddQuad(A, B, C, D, Col, 1.8f);
				}
			}
			else if (bConnected)
			{
				DrawList->AddCircleFilled(Center, Radius, Col, 16);
			}
			else
			{
				DrawList->AddCircle(Center, Radius, Col, 16, 1.8f);
				DrawList->AddCircleFilled(Center, Radius * 0.42f, ToColorU32(ImVec4(Color.x, Color.y, Color.z, 0.30f)), 12);
			}
		}
		ImGui::Dummy(Size);
	}

	void PopBackUtf8Codepoint(std::string& Text)
	{
		if (Text.empty()) return;
		size_t Start = Text.size() - 1;
		while (Start > 0 && ((static_cast<unsigned char>(Text[Start]) & 0xC0u) == 0x80u))
		{
			--Start;
		}
		Text.erase(Start);
	}

	std::string EllipsizeToWidth(const FString& InText, float Width)
	{
		std::string Text = InText;
		if (Text.empty() || Width <= 4.0f) return std::string();
		if (ImGui::CalcTextSize(Text.c_str()).x <= Width) return Text;

		static constexpr const char* Ellipsis = "...";
		const float EllipsisWidth = ImGui::CalcTextSize(Ellipsis).x;
		if (Width <= EllipsisWidth + 2.0f) return std::string(Ellipsis);

		while (!Text.empty() && ImGui::CalcTextSize((Text + Ellipsis).c_str()).x > Width)
		{
			PopBackUtf8Codepoint(Text);
		}
		Text += Ellipsis;
		return Text;
	}

	void DrawNodeHeaderBlock(const FAnimGraphNode& Node, float Width, float Height, const FString& Title, const char* Subtitle)
	{
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + Height);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		if (DrawList)
		{
			DrawList->AddRectFilled(Min, Max, ToColorU32(NodeHeaderBottomColor(Node.Type)), 6.0f);
			DrawList->AddRectFilled(Min, ImVec2(Max.x, Min.y + Height * 0.62f), ToColorU32(NodeHeaderTopColor(Node.Type)), 6.0f);
			DrawList->AddLine(ImVec2(Min.x, Max.y - 1.0f), ImVec2(Max.x, Max.y - 1.0f), ToColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f)), 1.0f);
			const float TextWidth = std::max(8.0f, Width - 22.0f);
			const std::string ClippedTitle = EllipsizeToWidth(Title, TextWidth);
			const std::string ClippedSubtitle = EllipsizeToWidth(FString(Subtitle ? Subtitle : ""), TextWidth);
			DrawList->AddText(ImVec2(Min.x + 11.0f, Min.y + 7.0f), ToColorU32(NodeTitleColor(Node.Type)), ClippedTitle.c_str());
			DrawList->AddText(ImVec2(Min.x + 11.0f, Min.y + 25.0f), ToColorU32(ImVec4(0.70f, 0.70f, 0.66f, 0.95f)), ClippedSubtitle.c_str());
		}
		ImGui::Dummy(ImVec2(Width, Height));
	}

	void DrawOutputPoseFigure(float Width, float Height)
	{
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + Height);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		if (DrawList)
		{
			DrawList->AddRectFilled(Min, Max, ToColorU32(ImVec4(0.03f, 0.04f, 0.035f, 0.82f)), 4.0f);
			DrawList->AddRect(Min, Max, ToColorU32(ImVec4(0.28f, 0.25f, 0.18f, 0.70f)), 4.0f, 0, 1.0f);
			const ImU32 Bone = ToColorU32(ImVec4(0.82f, 0.84f, 0.84f, 1.0f));
			const ImU32 Shadow = ToColorU32(ImVec4(0.00f, 0.00f, 0.00f, 0.35f));
			const ImVec2 C(Min.x + Width * 0.55f, Min.y + Height * 0.48f);
			auto L = [&](ImVec2 A, ImVec2 B, ImU32 Col, float T) { DrawList->AddLine(A, B, Col, T); };
			L(ImVec2(C.x - 12, C.y - 10), ImVec2(C.x + 10, C.y + 6), Shadow, 5.0f);
			L(ImVec2(C.x - 12, C.y - 10), ImVec2(C.x + 10, C.y + 6), Bone, 3.0f);
			DrawList->AddCircleFilled(ImVec2(C.x - 18, C.y - 18), 7.0f, Bone, 18);
			L(ImVec2(C.x - 2, C.y - 2), ImVec2(C.x - 25, C.y + 7), Bone, 3.0f);
			L(ImVec2(C.x + 3, C.y + 1), ImVec2(C.x + 27, C.y - 11), Bone, 3.0f);
			L(ImVec2(C.x + 10, C.y + 6), ImVec2(C.x - 6, C.y + 28), Bone, 3.0f);
			L(ImVec2(C.x + 10, C.y + 6), ImVec2(C.x + 31, C.y + 24), Bone, 3.0f);
		}
		ImGui::Dummy(ImVec2(Width, Height));
	}

	FString NodeTitleForDisplay(const FAnimGraphNode& Node)
	{
		switch (Node.Type)
		{
			case EAnimGraphNodeType::SequencePlayer:
				return Node.SequencePath.empty() ? FString("Sequence Player") : GetStemFromPath(Node.SequencePath);
			case EAnimGraphNodeType::VariableGet:
				return Node.VariableName == FName::None ? FString("Variable") : FString("Get ") + Node.VariableName.ToString();
			case EAnimGraphNodeType::StateMachine:
				return Node.DisplayName == FName::None ? FString("LocomotionSM") : Node.DisplayName.ToString();
			default:
				break;
		}
		if (Node.DisplayName != FName::None)
		{
			return Node.DisplayName.ToString();
		}
		return NodeTypeLabel(Node.Type);
	}

	FString NodePrimarySummary(const FAnimGraphNode& Node)
	{
		switch (Node.Type)
		{
			case EAnimGraphNodeType::SequencePlayer:
				return Node.SequencePath.empty() ? FString("No animation assigned") : GetStemFromPath(Node.SequencePath);
			case EAnimGraphNodeType::StateMachine:
				return FString("Entry: ") + (Node.InitialStateName == FName::None ? FString("not set") : Node.InitialStateName.ToString());
			case EAnimGraphNodeType::Slot:
				return FString("Slot: ") + (Node.SlotName == FName::None ? FString("DefaultSlot") : Node.SlotName.ToString());
			case EAnimGraphNodeType::LayeredBlendPerBone:
				return Node.RootBoneName.empty() ? FString("Full body blend") : FString("Root bone: ") + Node.RootBoneName;
			case EAnimGraphNodeType::BlendListByEnum:
				return "Selector chooses active pose";
			case EAnimGraphNodeType::VariableGet:
				return Node.VariableName == FName::None ? FString("Select a variable") : Node.VariableName.ToString();
			case EAnimGraphNodeType::RefPose:
				return "Reference pose";
			case EAnimGraphNodeType::OutputPose:
				return "Final animation pose";
		}
		return "";
	}

	FString NodeSecondarySummary(const FAnimGraphNode& Node)
	{
		char Buf[128];
		switch (Node.Type)
		{
			case EAnimGraphNodeType::SequencePlayer:
				std::snprintf(Buf, sizeof(Buf), "%s, %.2fx", Node.bLooping ? "Loop" : "Once", Node.PlayRate);
				return Buf;
			case EAnimGraphNodeType::StateMachine:
				std::snprintf(Buf, sizeof(Buf), "%zu states, %zu transitions", Node.States.size(), Node.Transitions.size());
				return Buf;
			case EAnimGraphNodeType::LayeredBlendPerBone:
				std::snprintf(Buf, sizeof(Buf), "Weight %.2f", Node.BlendWeight);
				return Buf;
			default:
				return PinTypeLabel(Node.Pins.empty() ? EAnimGraphPinType::Pose : Node.Pins.front().Type);
		}
	}


	const char* NodeRoleShort(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
			case EAnimGraphNodeType::OutputPose:          return "최종 출력";
			case EAnimGraphNodeType::SequencePlayer:      return "애니메이션 재생";
			case EAnimGraphNodeType::StateMachine:        return "상태 선택";
			case EAnimGraphNodeType::Slot:                return "몽타주 삽입";
			case EAnimGraphNodeType::LayeredBlendPerBone: return "본별 블렌드";
			case EAnimGraphNodeType::BlendListByEnum:     return "값으로 포즈 선택";
			case EAnimGraphNodeType::VariableGet:         return "변수 읽기";
			case EAnimGraphNodeType::RefPose:             return "기준 포즈";
		}
		return "노드";
	}

	const char* NodeRoleLong(EAnimGraphNodeType Type)
	{
		switch (Type)
		{
			case EAnimGraphNodeType::OutputPose:
				return "이 그래프의 최종 포즈입니다. Pose 입력으로 들어온 결과가 캐릭터에 적용됩니다.";
			case EAnimGraphNodeType::SequencePlayer:
				return "지정한 Animation Sequence 한 개를 재생해서 Pose를 출력합니다. State 내부에서 가장 자주 쓰는 leaf 노드입니다.";
			case EAnimGraphNodeType::StateMachine:
				return "여러 State 중 현재 조건에 맞는 State를 선택해서 Pose를 출력합니다. 더블클릭하거나 Open으로 내부 State Machine Graph를 엽니다.";
			case EAnimGraphNodeType::Slot:
				return "기본 포즈 위에 Montage Slot을 끼워 넣는 지점입니다. 공격/피격 같은 몽타주 재생을 받습니다.";
			case EAnimGraphNodeType::LayeredBlendPerBone:
				return "Base Pose와 Blend Pose를 섞습니다. Root Bone을 지정하면 해당 본 이하만 섞습니다.";
			case EAnimGraphNodeType::BlendListByEnum:
				return "Selector 값으로 여러 Pose 중 하나를 고릅니다. 현재 구현에서는 숫자 입력 기반입니다.";
			case EAnimGraphNodeType::VariableGet:
				return "AnimGraph 변수 또는 Owner AnimInstance 속성을 읽는 데이터 노드입니다. Lua BP/C++에서 갱신한 값을 여기서 읽습니다.";
			case EAnimGraphNodeType::RefPose:
				return "스켈레톤의 기준 포즈를 출력합니다. 임시 fallback 또는 초기 테스트용입니다.";
		}
		return "AnimGraph 노드입니다.";
	}

	void TextClippedWithTooltip(const FString& Text, float Width, const ImVec4* Color = nullptr)
	{
		if (Text.empty())
		{
			return;
		}
		const char* Raw = Text.c_str();
		const ImVec2 Size = ImGui::CalcTextSize(Raw);
		if (Color) ImGui::PushStyleColor(ImGuiCol_Text, *Color);
		if (Size.x <= Width)
		{
			ImGui::TextUnformatted(Raw);
		}
		else
		{
			const std::string Clipped = EllipsizeToWidth(Text, Width);
			ImGui::TextUnformatted(Clipped.c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Raw);
		}
		if (Color) ImGui::PopStyleColor();
	}

	void RenderInputPinRow(UAnimGraphAsset& Asset, const FAnimGraphPin& Pin, float Width)
	{
		ed::BeginPin(ToPinId(Pin.PinId), ed::PinKind::Input);
		ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
		DrawAnimGraphPinIcon(Pin.Type, IsAnimGraphPinLinked(Asset, Pin.PinId));
		ImGui::SameLine(0.0f, 7.0f);
		const FString Label = Pin.DisplayName.ToString();
		const float LabelWidth = std::max(24.0f, Width - ImGui::GetTextLineHeight() - 12.0f);
		const std::string Clipped = EllipsizeToWidth(Label, LabelWidth);
		ImGui::TextColored(PinTypeColor(Pin.Type), "%s", Clipped.c_str());
		if (ImGui::IsItemHovered() && Clipped != Label) ImGui::SetTooltip("%s", Label.c_str());
		ed::EndPin();
	}

	void RenderOutputPinRow(UAnimGraphAsset& Asset, const FAnimGraphPin& Pin, float Width)
	{
		const FString Label = Pin.DisplayName.ToString();
		const float IconWidth = ImGui::GetTextLineHeight() * 0.92f;
		const float MaxLabelWidth = std::max(24.0f, Width * 0.46f - IconWidth - 12.0f);
		const std::string Clipped = EllipsizeToWidth(Label, MaxLabelWidth);
		const float LabelWidth = ImGui::CalcTextSize(Clipped.c_str()).x;
		const float X = ImGui::GetCursorPosX();
		const float TargetX = X + std::max(0.0f, Width - LabelWidth - IconWidth - 12.0f);
		ImGui::SetCursorPosX(TargetX);
		ed::BeginPin(ToPinId(Pin.PinId), ed::PinKind::Output);
		ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
		ImGui::TextColored(PinTypeColor(Pin.Type), "%s", Clipped.c_str());
		if (ImGui::IsItemHovered() && Clipped != Label) ImGui::SetTooltip("%s", Label.c_str());
		ImGui::SameLine(0.0f, 7.0f);
		DrawAnimGraphPinIcon(Pin.Type, IsAnimGraphPinLinked(Asset, Pin.PinId));
		ed::EndPin();
	}

	void RenderAnimGraphPins(UAnimGraphAsset& Asset, const FAnimGraphNode& Node, float Width)
	{
		TArray<const FAnimGraphPin*> Inputs;
		TArray<const FAnimGraphPin*> Outputs;
		Inputs.reserve(Node.Pins.size());
		Outputs.reserve(Node.Pins.size());
		for (const FAnimGraphPin& Pin : Node.Pins)
		{
			if (Pin.Kind == EAnimGraphPinKind::Input) Inputs.push_back(&Pin);
			else Outputs.push_back(&Pin);
		}

		const int32 Rows = std::max(static_cast<int32>(Inputs.size()), static_cast<int32>(Outputs.size()));
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float RowStartY = ImGui::GetCursorPosY();
			if (Row < static_cast<int32>(Inputs.size()))
			{
				RenderInputPinRow(Asset, *Inputs[Row], Width * 0.48f);
			}
			else
			{
				ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight() * 0.92f));
			}

			if (Row < static_cast<int32>(Outputs.size()))
			{
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::SetCursorPosY(RowStartY);
				RenderOutputPinRow(Asset, *Outputs[Row], Width);
			}
		}
	}

	bool RenderAnimGraphNodeCard(UAnimGraphAsset& Asset, const FAnimGraphNode& Node)
	{
		bool bRequestOpenStateMachine = false;
		ImGui::PushID(static_cast<int>(Node.NodeId));

		const bool bCompactVariable = Node.Type == EAnimGraphNodeType::VariableGet;
		const bool bOutputPose = Node.Type == EAnimGraphNodeType::OutputPose;
		const float Width = bOutputPose ? 242.0f : (bCompactVariable ? 184.0f : 224.0f);
		const float HeaderHeight = bCompactVariable ? 30.0f : 44.0f;
		const FString Title = NodeTitleForDisplay(Node);
		DrawNodeHeaderBlock(Node, Width, HeaderHeight, Title, NodeTypeLabel(Node.Type));

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.86f, 0.82f, 1.0f));
		ImGui::Dummy(ImVec2(Width, 3.0f));

		if (bOutputPose)
		{
			ImGui::BeginGroup();
			ImGui::TextUnformatted("Final animation pose");
			ImGui::TextDisabled("AnimGraph Result");
			ImGui::Spacing();
			for (const FAnimGraphPin& Pin : Node.Pins)
			{
				if (Pin.Kind == EAnimGraphPinKind::Input) RenderInputPinRow(Asset, Pin, Width * 0.50f);
			}
			ImGui::EndGroup();
			ImGui::SameLine();
			DrawOutputPoseFigure(88.0f, 70.0f);
		}
		else if (bCompactVariable)
		{
			ImGui::TextDisabled("%s", NodeRoleShort(Node.Type));
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", NodeRoleLong(Node.Type));
			RenderAnimGraphPins(Asset, Node, Width);
		}
		else
		{
			const FString Primary = NodePrimarySummary(Node);
			const FString Secondary = NodeSecondarySummary(Node);
			ImGui::TextDisabled("%s", NodeRoleShort(Node.Type));
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", NodeRoleLong(Node.Type));
			TextClippedWithTooltip(Primary, Width - 12.0f);
			TextClippedWithTooltip(Secondary, Width - 12.0f, nullptr);

			if (Node.Type == EAnimGraphNodeType::StateMachine)
			{
				ImGui::Spacing();
				if (ImGui::SmallButton("Open"))
				{
					bRequestOpenStateMachine = true;
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("상태/전환 그래프를 엽니다.");
			}

			ImGui::Spacing();
			RenderAnimGraphPins(Asset, Node, Width);
		}

		ImGui::Dummy(ImVec2(Width, 3.0f));
		ImGui::PopStyleColor();
		ImGui::PopID();
		return bRequestOpenStateMachine;
	}


	int PushUnrealAnimGraphCanvasStyle()
	{
		// UE AnimGraph 의 핵심 인상: 어두운 그래프 배경, 낮은 대비의 격자, 검은 노드 바디,
		// 선택/호버 시 밝아지는 외곽선, pose 링크는 두껍고 밝게.
		ed::PushStyleColor(ed::StyleColor_Bg,             ImColor(31, 32, 39, 255));
		ed::PushStyleColor(ed::StyleColor_Grid,           ImColor(48, 50, 58, 150));
		ed::PushStyleColor(ed::StyleColor_NodeBg,         ImColor(10, 11, 11, 232));
		ed::PushStyleColor(ed::StyleColor_NodeBorder,     ImColor(82, 82, 82, 150));
		ed::PushStyleColor(ed::StyleColor_HovNodeBorder,  ImColor(190, 210, 230, 220));
		ed::PushStyleColor(ed::StyleColor_SelNodeBorder,  ImColor(255, 230, 105, 235));
		ed::PushStyleColor(ed::StyleColor_HovLinkBorder,  ImColor(255, 255, 255, 220));
		ed::PushStyleColor(ed::StyleColor_SelLinkBorder,  ImColor(255, 230, 105, 235));
		return 8;
	}

	bool RenderAddNodeAction(EAnimGraphNodeType Type, UAnimGraphAsset& Asset, const ImVec2& SpawnPosition, const char* SearchText, const char* Category)
	{
		const FString Label = NodeTypeLabel(Type);
		FString Haystack = Label + FString(" ") + Category;
		if (SearchText && SearchText[0] != '\0')
		{
			// 간단한 ASCII case-insensitive 필터. 노드 이름/카테고리에 포함되지 않으면 숨김.
			std::string H = Haystack;
			std::string N = SearchText;
			std::transform(H.begin(), H.end(), H.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
			std::transform(N.begin(), N.end(), N.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
			if (H.find(N) == std::string::npos) return false;
		}

		const bool bDisabled = (Type == EAnimGraphNodeType::OutputPose) && Asset.HasOutputPoseNode();
		if (bDisabled) ImGui::BeginDisabled();
		ImGui::PushStyleColor(ImGuiCol_Text, NodeTitleColor(Type));
		const bool bClicked = ImGui::Selectable(Label.c_str(), false);
		ImGui::PopStyleColor();
		if (bClicked)
		{
			FAnimGraphNode* NewNode = Asset.AddNodeOfType(Type, SpawnPosition.x, SpawnPosition.y);
			if (NewNode)
			{
				ed::SetNodePosition(ToNodeId(NewNode->NodeId), SpawnPosition);
			}
			ImGui::CloseCurrentPopup();
			if (bDisabled) ImGui::EndDisabled();
			return NewNode != nullptr;
		}
		if (bDisabled) ImGui::EndDisabled();
		return false;
	}

	bool RenderGetVariableAction(UAnimGraphAsset& Asset, const FAnimGraphVariable& Var, const ImVec2& SpawnPosition, const char* SearchText)
	{
		if (Var.VariableName == FName::None) return false;
		const FString Label = FString("Get ") + Var.VariableName.ToString();
		const FString Haystack = Label + FString(" Variable Property Access Float Bool Int");
		if (!StringContainsInsensitive(Haystack, SearchText)) return false;

		ImGui::PushStyleColor(ImGuiCol_Text, NodeTitleColor(EAnimGraphNodeType::VariableGet));
		const bool bClicked = ImGui::Selectable(Label.c_str(), false);
		ImGui::PopStyleColor();
		if (!bClicked) return false;

		FAnimGraphNode* NewNode = Asset.AddNodeOfType(EAnimGraphNodeType::VariableGet, SpawnPosition.x, SpawnPosition.y);
		if (NewNode)
		{
			NewNode->VariableName = Var.VariableName;
			SetVariableGetOutputType(*NewNode, Var.Type);
			ed::SetNodePosition(ToNodeId(NewNode->NodeId), SpawnPosition);
		}
		ImGui::CloseCurrentPopup();
		return NewNode != nullptr;
	}


	// State name dropdown — Node.States 의 이름을 list. "(any)" 항목은 FName::None 으로 (AnyState 전이).
	// 반환: 변경 발생 여부.
	bool StateNameCombo(const char* Label, const TArray<FAnimGraphState>& States, FName& InOutName, bool bAllowAny)
	{
		const FString CurStr = (InOutName == FName::None) ? FString(bAllowAny ? "(any)" : "(none)") : InOutName.ToString();
		bool bChanged = false;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo(Label, CurStr.c_str()))
		{
			if (bAllowAny)
			{
				const bool bSel = (InOutName == FName::None);
				if (ImGui::Selectable("(any)", bSel))
				{
					if (InOutName != FName::None) bChanged = true;
					InOutName = FName::None;
				}
			}
			for (const FAnimGraphState& S : States)
			{
				const FString SName = S.StateName.ToString();
				const bool bSel = (InOutName == S.StateName);
				if (ImGui::Selectable(SName.c_str(), bSel))
				{
					if (InOutName != S.StateName) bChanged = true;
					InOutName = S.StateName;
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	const char* VariableTypeLabel(EAnimGraphPinType Type)
	{
		switch (Type)
		{
			case EAnimGraphPinType::Float: return "Float";
			case EAnimGraphPinType::Bool:  return "Bool";
			case EAnimGraphPinType::Int:   return "Int";
			case EAnimGraphPinType::Name:  return "Name";
			case EAnimGraphPinType::Pose:  return "Pose";
		}
		return "Variable";
	}

	bool IsScalarPropertyType(EPropertyType T)
	{
		return T == EPropertyType::Float || T == EPropertyType::Int
			|| T == EPropertyType::Bool || T == EPropertyType::ByteBool;
	}

	bool OwnerClassHasScalarProperty(UClass* OwnerCls, FName Name)
	{
		if (!OwnerCls || Name == FName::None) return false;
		TArray<const FProperty*> Props;
		OwnerCls->GetPropertyRefs(Props);
		const FString Wanted = Name.ToString();
		for (const FProperty* Prop : Props)
		{
			if (Prop && Wanted == Prop->Name && IsScalarPropertyType(Prop->GetType())) return true;
		}
		return false;
	}

	bool IsVariableResolvable(const UAnimGraphAsset* Asset, UClass* OwnerCls, FName Name)
	{
		if (Name == FName::None) return false;
		if (Asset && Asset->FindVariable(Name)) return true;
		return OwnerClassHasScalarProperty(OwnerCls, Name);
	}

	// AnimGraph-owned 변수 + OwnerClass UPROPERTY 를 함께 보여주는 dropdown.
	// UE AnimBP 처럼 기본은 그래프 변수이고, C++ AnimInstance UPROPERTY 는 외부 주입용 fallback 으로 남긴다.
	void EnsureGraphVariable(UAnimGraphAsset* Asset, const FName& Name, EAnimGraphPinType Type)
	{
		if (!Asset || Name == FName::None) return;
		if (!Asset->FindVariable(Name))
		{
			Asset->AddVariable(Name, Type);
		}
	}

	bool VariableNameCombo(const char* Label, const UAnimGraphAsset* Asset, UClass* OwnerCls, FName& InOutName)
	{
		const FString Preview = (InOutName == FName::None) ? FString("(none)") : InOutName.ToString();
		bool bChanged = false;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo(Label, Preview.c_str()))
		{
			const bool bNone = InOutName == FName::None;
			if (ImGui::Selectable("(none)", bNone))
			{
				if (!bNone) bChanged = true;
				InOutName = FName::None;
			}

			if (Asset)
			{
				ImGui::SeparatorText("Anim Graph Variables");
				for (const FAnimGraphVariable& Var : Asset->GetVariables())
				{
					if (Var.VariableName == FName::None) continue;
					const FString Name = Var.VariableName.ToString();
					const bool bSel = (InOutName == Var.VariableName);
					FString LabelText = Name + "  (" + VariableTypeLabel(Var.Type) + ")";
					if (ImGui::Selectable(LabelText.c_str(), bSel))
					{
						InOutName = Var.VariableName;
						bChanged = true;
					}
				}
			}

			ImGui::SeparatorText("Owner AnimInstance Properties");
			if (!OwnerCls)
			{
				ImGui::TextDisabled("Owner class not found");
			}
			else
			{
				TArray<const FProperty*> Props;
				OwnerCls->GetPropertyRefs(Props);
				for (const FProperty* Prop : Props)
				{
					if (!Prop || !IsScalarPropertyType(Prop->GetType())) continue;
					const bool bSel = (InOutName.ToString() == Prop->Name);
					if (ImGui::Selectable(Prop->Name, bSel))
					{
						InOutName = FName(Prop->Name);
						bChanged = true;
					}
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	EAnimGraphPinType PropertyTypeToAnimGraphPinType(EPropertyType Type)
	{
		switch (Type)
		{
			case EPropertyType::Bool:
			case EPropertyType::ByteBool:
				return EAnimGraphPinType::Bool;
			case EPropertyType::Int:
				return EAnimGraphPinType::Int;
			case EPropertyType::Float:
			default:
				return EAnimGraphPinType::Float;
		}
	}

	bool TryResolveVariableType(const UAnimGraphAsset* Asset, UClass* OwnerCls, FName Name, EAnimGraphPinType& OutType)
	{
		if (Name == FName::None) return false;
		if (Asset)
		{
			if (const FAnimGraphVariable* Var = Asset->FindVariable(Name))
			{
				OutType = Var->Type;
				return true;
			}
		}
		if (OwnerCls)
		{
			TArray<const FProperty*> Props;
			OwnerCls->GetPropertyRefs(Props);
			const FString Wanted = Name.ToString();
			for (const FProperty* Prop : Props)
			{
				if (Prop && Wanted == Prop->Name && IsScalarPropertyType(Prop->GetType()))
				{
					OutType = PropertyTypeToAnimGraphPinType(Prop->GetType());
					return true;
				}
			}
		}
		return false;
	}

	bool VariableTypeMatchesRule(ETransitionRuleKind Kind, EAnimGraphPinType Type)
	{
		switch (Kind)
		{
			case ETransitionRuleKind::BoolProperty:
				return Type == EAnimGraphPinType::Bool;
			case ETransitionRuleKind::FloatCompare:
				return Type == EAnimGraphPinType::Float || Type == EAnimGraphPinType::Int;
			default:
				return true;
		}
	}

	void SetVariableGetOutputType(FAnimGraphNode& Node, EAnimGraphPinType Type)
	{
		if (Node.Type != EAnimGraphNodeType::VariableGet) return;
		for (FAnimGraphPin& Pin : Node.Pins)
		{
			if (Pin.Kind == EAnimGraphPinKind::Output)
			{
				Pin.Type = Type;
				Pin.DisplayName = FName(VariableTypeLabel(Type));
				return;
			}
		}
	}

	bool VariableNameComboFiltered(const char* Label, const UAnimGraphAsset* Asset, UClass* OwnerCls, FName& InOutName, EAnimGraphPinType DesiredType, bool bAllowNumericForFloat)
	{
		const FString Preview = (InOutName == FName::None) ? FString("(none)") : InOutName.ToString();
		bool bChanged = false;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo(Label, Preview.c_str()))
		{
			const bool bNone = InOutName == FName::None;
			if (ImGui::Selectable("(none)", bNone))
			{
				if (!bNone) bChanged = true;
				InOutName = FName::None;
			}

			auto IsAccepted = [&](EAnimGraphPinType T)
			{
				if (T == DesiredType) return true;
				return bAllowNumericForFloat && DesiredType == EAnimGraphPinType::Float && T == EAnimGraphPinType::Int;
			};

			if (Asset)
			{
				ImGui::SeparatorText("AnimGraph 변수");
				for (const FAnimGraphVariable& Var : Asset->GetVariables())
				{
					if (Var.VariableName == FName::None || !IsAccepted(Var.Type)) continue;
					const FString Name = Var.VariableName.ToString();
					const bool bSel = (InOutName == Var.VariableName);
					const FString LabelText = Name + "  (" + VariableTypeLabel(Var.Type) + ")";
					if (ImGui::Selectable(LabelText.c_str(), bSel))
					{
						InOutName = Var.VariableName;
						bChanged = true;
					}
				}
			}

			ImGui::SeparatorText("Owner AnimInstance 속성");
			if (!OwnerCls)
			{
				ImGui::TextDisabled("Owner class not found");
			}
			else
			{
				TArray<const FProperty*> Props;
				OwnerCls->GetPropertyRefs(Props);
				for (const FProperty* Prop : Props)
				{
					if (!Prop || !IsScalarPropertyType(Prop->GetType())) continue;
					const EAnimGraphPinType PropType = PropertyTypeToAnimGraphPinType(Prop->GetType());
					if (!IsAccepted(PropType)) continue;
					const bool bSel = (InOutName.ToString() == Prop->Name);
					const FString LabelText = FString(Prop->Name) + "  (" + VariableTypeLabel(PropType) + ")";
					if (ImGui::Selectable(LabelText.c_str(), bSel))
					{
						InOutName = FName(Prop->Name);
						bChanged = true;
					}
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	// State 한 개의 form. true 반환 — 변경 발생.
	// OwnerNode (이 state 를 보유한 StateMachine 노드) 의 NodeId 는 sub-graph dropdown 의
	// 자기 자신 제외용. AllNodes 는 sub-graph 후보 (그래프 안의 다른 StateMachine 노드) list.
	bool RenderStateRow(FAnimGraphState& State, int32 Index, uint32 OwnerNodeId, const TArray<FAnimGraphNode>& AllNodes)
	{
		ImGui::PushID(Index);
		bool bChanged = false;

		// Name InputText
		char NameBuf[64];
		const FString Cur = State.StateName.ToString();
		std::snprintf(NameBuf, sizeof(NameBuf), "%s", Cur.c_str());
		ImGui::TextUnformatted("Name");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##Name", NameBuf, sizeof(NameBuf)))
		{
			State.StateName = (NameBuf[0] == '\0') ? FName::None : FName(NameBuf);
			bChanged = true;
		}

		// Sub-Graph dropdown — 그래프 안의 다른 StateMachine 노드 (자기 자신 제외).
		// 선택되면 sequence 무시되고 sub-tree 컴파일됨.
		ImGui::TextUnformatted("Sub-Graph");
		FString SubPreview = "(none)";
		if (State.SubGraphNodeId != 0)
		{
			for (const FAnimGraphNode& N : AllNodes)
			{
				if (N.NodeId == State.SubGraphNodeId)
				{
					char Buf[64];
					std::snprintf(Buf, sizeof(Buf), "%s #%u", N.DisplayName.ToString().c_str(), N.NodeId);
					SubPreview = Buf;
					break;
				}
			}
		}
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##SubGraph", SubPreview.c_str()))
		{
			if (ImGui::Selectable("(none)", State.SubGraphNodeId == 0))
			{
				if (State.SubGraphNodeId != 0) bChanged = true;
				State.SubGraphNodeId = 0;
			}
			for (const FAnimGraphNode& N : AllNodes)
			{
				if (N.Type != EAnimGraphNodeType::StateMachine) continue;
				if (N.NodeId == OwnerNodeId) continue; // 자기 자신 제외 (직접 self-ref 금지)
				char Buf[64];
				std::snprintf(Buf, sizeof(Buf), "%s #%u", N.DisplayName.ToString().c_str(), N.NodeId);
				const bool bSel = (State.SubGraphNodeId == N.NodeId);
				if (ImGui::Selectable(Buf, bSel))
				{
					if (State.SubGraphNodeId != N.NodeId) bChanged = true;
					State.SubGraphNodeId = N.NodeId;
				}
			}
			ImGui::EndCombo();
		}

		// Sub-Graph 가 선택된 경우 sequence/playrate/looping 은 무시되지만 form 자체는 그대로 노출 —
		// 사용자가 (none) 으로 되돌렸을 때 직전 sequence 값 보존.
		const bool bSubActive = (State.SubGraphNodeId != 0);
		if (bSubActive) ImGui::BeginDisabled();

		ImGui::TextUnformatted("Sequence");
		const FString PreviewStem = State.SequencePath.empty() ? FString("None") : GetStemFromPath(State.SequencePath);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##Seq", PreviewStem.c_str()))
		{
			const bool bSelNone = State.SequencePath.empty();
			if (ImGui::Selectable("None", bSelNone))
			{
				if (!State.SequencePath.empty()) bChanged = true;
				State.SequencePath.clear();
			}
			const TArray<FAssetListItem>& Anims = FAssetRegistry::ListByTypeName("UAnimSequence");
			for (const FAssetListItem& Item : Anims)
			{
				const bool bSel = (State.SequencePath == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSel))
				{
					if (State.SequencePath != Item.FullPath) bChanged = true;
					State.SequencePath = Item.FullPath;
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##PlayRate", &State.PlayRate, 0.05f, -4.0f, 4.0f, "PlayRate %.2f"))
		{
			bChanged = true;
		}
		if (ImGui::Checkbox("Looping", &State.bLooping))
		{
			bChanged = true;
		}

		if (bSubActive) ImGui::EndDisabled();

		ImGui::PopID();
		return bChanged;
	}

	bool TransitionRuleKindCombo(const char* Label, ETransitionRuleKind& InOutKind)
	{
		bool bChanged = false;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo(Label, TransitionRuleKindLabel(InOutKind)))
		{
			for (int i = 0; i <= static_cast<int>(ETransitionRuleKind::AlwaysFalse); ++i)
			{
				const ETransitionRuleKind K = static_cast<ETransitionRuleKind>(i);
				const bool bSel = (InOutKind == K);
				if (ImGui::Selectable(TransitionRuleKindLabel(K), bSel))
				{
					if (InOutKind != K) bChanged = true;
					InOutKind = K;
					// Bool rule 의 기본 기대값은 true 로 둔다. FloatCompare 는 기존 Threshold 보존.
					if (K == ETransitionRuleKind::BoolProperty && bChanged && InOutKind == K)
					{
						// noop — 선택 직후 아래 bool UI 에서 즉시 수정 가능.
					}
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	void DrawSmallRuleNode(const char* Title, const char* Subtitle, const ImVec4& Color, float Width)
	{
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const float Height = 58.0f;
		const ImVec2 Max(Min.x + Width, Min.y + Height);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		if (DrawList)
		{
			DrawList->AddRectFilled(Min, Max, ToColorU32(ImVec4(0.025f, 0.028f, 0.030f, 0.96f)), 5.0f);
			DrawList->AddRectFilled(Min, ImVec2(Max.x, Min.y + 22.0f), ToColorU32(ImVec4(Color.x * 0.30f, Color.y * 0.30f, Color.z * 0.30f, 1.0f)), 5.0f);
			DrawList->AddRect(Min, Max, ToColorU32(Color), 5.0f, 0, 1.2f);
			DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 4.0f), ToColorU32(Color), Title);
			DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 30.0f), ToColorU32(ImVec4(0.82f, 0.82f, 0.78f, 1.0f)), Subtitle);
		}
		ImGui::Dummy(ImVec2(Width, Height));
	}

	bool RenderTransitionRuleGraph(FAnimGraphTransition& T, UAnimGraphAsset* Asset, UClass* OwnerCls, int32 Index)
	{
		ImGui::PushID(Index + 100000);
		bool bChanged = false;

		ImGui::TextColored(ImVec4(0.70f, 0.50f, 1.00f, 1.0f), "Transition Rule Graph");
		ImGui::SameLine();
		if (ImGui::SmallButton("Add Rule Node"))
		{
			ImGui::OpenPopup("TransitionRuleActionPalette");
		}
		ImGui::TextDisabled("The rule must output a boolean for Can Enter Transition.");

		if (ImGui::BeginPopup("TransitionRuleActionPalette"))
		{
			static char RuleSearchBuf[96] = {};
			ImGui::TextUnformatted("All Actions for this Transition Rule");
			ImGui::SetNextItemWidth(310.0f);
			ImGui::InputText("##TransitionRuleSearch", RuleSearchBuf, sizeof(RuleSearchBuf));
			ImGui::Separator();

			auto RuleAction = [&](ETransitionRuleKind Kind)
			{
				const FString Search = FString(TransitionRuleKindLabel(Kind)) + FString(" ") + TransitionRuleKindSearchText(Kind);
				if (!StringContainsInsensitive(Search, RuleSearchBuf)) return;
				const bool bSelected = T.RuleKind == Kind;
				if (ImGui::Selectable(TransitionRuleKindLabel(Kind), bSelected))
				{
					if (T.RuleKind != Kind) bChanged = true;
					T.RuleKind = Kind;
					if (Kind == ETransitionRuleKind::BoolProperty && T.Threshold < 0.5f)
					{
						T.Threshold = 1.0f;
					}
					ImGui::CloseCurrentPopup();
				}
			};

			if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen))
			{
				RuleAction(ETransitionRuleKind::BoolProperty);
				RuleAction(ETransitionRuleKind::FloatCompare);
			}
			if (ImGui::CollapsingHeader("Transition Functions", ImGuiTreeNodeFlags_DefaultOpen))
			{
				RuleAction(ETransitionRuleKind::TimeRemaining);
				RuleAction(ETransitionRuleKind::TimeRemainingRatio);
				RuleAction(ETransitionRuleKind::TimeElapsed);
				RuleAction(ETransitionRuleKind::AutomaticSequenceEnd);
			}
			if (ImGui::CollapsingHeader("Literals", ImGuiTreeNodeFlags_DefaultOpen))
			{
				RuleAction(ETransitionRuleKind::AlwaysTrue);
				RuleAction(ETransitionRuleKind::AlwaysFalse);
			}
			ImGui::EndPopup();
		}

		const float NodeWidth = std::max(150.0f, (ImGui::GetContentRegionAvail().x - 26.0f) * 0.50f);
		const FString RuleTitle = FormatTransitionRuleNodeTitle(T);
		const FString RuleSummary = FormatTransitionSummary(T);
		DrawSmallRuleNode(RuleTitle.c_str(), RuleSummary.c_str(), ImVec4(0.30f, 0.85f, 0.30f, 1.0f), NodeWidth);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.86f, 0.86f, 0.82f, 1.0f), "=>");
		ImGui::SameLine();
		DrawSmallRuleNode("Can Enter Transition", "Return Value", ImVec4(0.86f, 0.20f, 0.16f, 1.0f), NodeWidth);

		ImGui::Spacing();
		ImGui::TextUnformatted("Rule Type");
		bChanged |= TransitionRuleKindCombo("##TransitionRuleKind", T.RuleKind);

		if (ImGui::CollapsingHeader("Common UE-style Presets"))
		{
			ImGui::TextWrapped("Pick a preset, then adjust the variable and threshold. For an Idle <-> Run pair, use GroundSpeed > threshold on the forward transition and GroundSpeed <= threshold on the reverse transition.");
			if (ImGui::Button("Locomotion Enter: GroundSpeed > 10"))
			{
				EnsureGraphVariable(Asset, FName("GroundSpeed"), EAnimGraphPinType::Float);
				T.RuleKind = ETransitionRuleKind::FloatCompare;
				T.VariableName = FName("GroundSpeed");
				T.Op = ETransitionOp::Greater;
				T.Threshold = 10.0f;
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Locomotion Exit: GroundSpeed <= 10"))
			{
				EnsureGraphVariable(Asset, FName("GroundSpeed"), EAnimGraphPinType::Float);
				T.RuleKind = ETransitionRuleKind::FloatCompare;
				T.VariableName = FName("GroundSpeed");
				T.Op = ETransitionOp::LessEqual;
				T.Threshold = 10.0f;
				bChanged = true;
			}
			if (ImGui::Button("Jump/Fall: IsFalling == true"))
			{
				EnsureGraphVariable(Asset, FName("IsFalling"), EAnimGraphPinType::Bool);
				T.RuleKind = ETransitionRuleKind::BoolProperty;
				T.VariableName = FName("IsFalling");
				T.Threshold = 1.0f;
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Land: IsFalling == false"))
			{
				EnsureGraphVariable(Asset, FName("IsFalling"), EAnimGraphPinType::Bool);
				T.RuleKind = ETransitionRuleKind::BoolProperty;
				T.VariableName = FName("IsFalling");
				T.Threshold = 0.0f;
				bChanged = true;
			}
			if (ImGui::Button("One-shot: Current Animation Reached End"))
			{
				T.RuleKind = ETransitionRuleKind::AutomaticSequenceEnd;
				T.VariableName = FName::None;
				T.Threshold = 0.0f;
				bChanged = true;
			}
		}

		switch (T.RuleKind)
		{
			case ETransitionRuleKind::FloatCompare:
			{
				ImGui::TextUnformatted("Property");
				bChanged |= VariableNameCombo("##RuleFloatProperty", Asset, OwnerCls, T.VariableName);
				ImGui::TextUnformatted("Compare");
				ImGui::SetNextItemWidth(72.0f);
				if (ImGui::BeginCombo("##RuleOp", TransitionOpLabel(T.Op)))
				{
					for (int i = 0; i <= static_cast<int>(ETransitionOp::NotEqual); ++i)
					{
						const ETransitionOp O = static_cast<ETransitionOp>(i);
						const bool bSel = (T.Op == O);
						if (ImGui::Selectable(TransitionOpLabel(O), bSel))
						{
							if (T.Op != O) bChanged = true;
							T.Op = O;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##RuleThreshold", &T.Threshold, 0.1f, -1000.0f, 1000.0f, "%.2f")) bChanged = true;
				break;
			}
			case ETransitionRuleKind::BoolProperty:
			{
				ImGui::TextUnformatted("Property");
				bChanged |= VariableNameCombo("##RuleBoolProperty", Asset, OwnerCls, T.VariableName);
				bool bExpected = T.Threshold >= 0.5f;
				if (ImGui::Checkbox("Expected True", &bExpected))
				{
					T.Threshold = bExpected ? 1.0f : 0.0f;
					bChanged = true;
				}
				break;
			}
			case ETransitionRuleKind::TimeRemaining:
				ImGui::TextUnformatted("Remaining Seconds <=");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##RuleTimeRemaining", &T.Threshold, 0.01f, 0.0f, 60.0f, "%.2fs")) bChanged = true;
				break;
			case ETransitionRuleKind::TimeRemainingRatio:
				ImGui::TextUnformatted("Remaining Ratio <=");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##RuleTimeRemainingRatio", &T.Threshold, 0.01f, 0.0f, 1.0f, "%.2f")) bChanged = true;
				break;
			case ETransitionRuleKind::TimeElapsed:
				ImGui::TextUnformatted("Current State Time >=");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##RuleTimeElapsed", &T.Threshold, 0.01f, 0.0f, 60.0f, "%.2fs")) bChanged = true;
				break;
			case ETransitionRuleKind::AutomaticSequenceEnd:
				ImGui::TextWrapped("Uses the current non-looping state's sequence end. Looping states will not auto-transition.");
				break;
			case ETransitionRuleKind::AlwaysTrue:
				ImGui::TextWrapped("This transition is unconditional. Use it only for intentional forced flow.");
				break;
			case ETransitionRuleKind::AlwaysFalse:
				ImGui::TextWrapped("This transition is disabled until a real rule node is selected.");
				break;
		}

		ImGui::TextUnformatted("Blend Duration");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##RuleBlendTime", &T.BlendTime, 0.01f, 0.0f, 5.0f, "%.2fs")) bChanged = true;

		ImGui::PopID();
		return bChanged;
	}

	bool RenderTransitionRuleCompactEditor(FAnimGraphTransition& T, UAnimGraphAsset* Asset, UClass* OwnerCls)
	{
		bool bChanged = false;
		ImGui::TextUnformatted("Rule");
		bChanged |= TransitionRuleKindCombo("##TransitionRuleKind", T.RuleKind);

		switch (T.RuleKind)
		{
			case ETransitionRuleKind::FloatCompare:
			{
				ImGui::TextUnformatted("Float / Int Variable");
				bChanged |= VariableNameComboFiltered("##RuleFloatProperty", Asset, OwnerCls, T.VariableName, EAnimGraphPinType::Float, true);
				ImGui::SetNextItemWidth(72.0f);
				if (ImGui::BeginCombo("##RuleOp", TransitionOpLabel(T.Op)))
				{
					for (int i = 0; i <= static_cast<int>(ETransitionOp::NotEqual); ++i)
					{
						const ETransitionOp O = static_cast<ETransitionOp>(i);
						const bool bSel = (T.Op == O);
						if (ImGui::Selectable(TransitionOpLabel(O), bSel))
						{
							if (T.Op != O) bChanged = true;
							T.Op = O;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##RuleThreshold", &T.Threshold, 0.1f, -1000.0f, 1000.0f, "%.2f")) bChanged = true;
				break;
			}
			case ETransitionRuleKind::BoolProperty:
			{
				ImGui::TextUnformatted("Bool Variable");
				bChanged |= VariableNameComboFiltered("##RuleBoolProperty", Asset, OwnerCls, T.VariableName, EAnimGraphPinType::Bool, false);
				bool bExpected = T.Threshold >= 0.5f;
				if (ImGui::Checkbox("Expected True", &bExpected))
				{
					T.Threshold = bExpected ? 1.0f : 0.0f;
					bChanged = true;
				}
				break;
			}
			case ETransitionRuleKind::TimeRemaining:
				ImGui::TextUnformatted("Remaining Seconds <=");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##RuleTimeRemaining", &T.Threshold, 0.01f, 0.0f, 60.0f, "%.2fs")) bChanged = true;
				break;
			case ETransitionRuleKind::TimeRemainingRatio:
				ImGui::TextUnformatted("Remaining Ratio <=");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##RuleTimeRemainingRatio", &T.Threshold, 0.01f, 0.0f, 1.0f, "%.2f")) bChanged = true;
				break;
			case ETransitionRuleKind::TimeElapsed:
				ImGui::TextUnformatted("Current State Time >=");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##RuleTimeElapsed", &T.Threshold, 0.01f, 0.0f, 60.0f, "%.2fs")) bChanged = true;
				break;
			case ETransitionRuleKind::AutomaticSequenceEnd:
				ImGui::TextDisabled("현재 State의 non-looping sequence가 끝나면 true입니다.");
				break;
			case ETransitionRuleKind::AlwaysTrue:
				ImGui::TextDisabled("항상 전환됩니다. 의도한 경우에만 사용하세요.");
				break;
			case ETransitionRuleKind::AlwaysFalse:
				ImGui::TextDisabled("비활성 전환입니다. Rule을 선택해야 전환됩니다.");
				break;
		}

		ImGui::TextUnformatted("Blend");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##RuleBlendTime", &T.BlendTime, 0.01f, 0.0f, 5.0f, "%.2fs")) bChanged = true;

		if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_None))
		{
			if (ImGui::Button("GroundSpeed > 10"))
			{
				EnsureGraphVariable(Asset, FName("GroundSpeed"), EAnimGraphPinType::Float);
				T.RuleKind = ETransitionRuleKind::FloatCompare;
				T.VariableName = FName("GroundSpeed");
				T.Op = ETransitionOp::Greater;
				T.Threshold = 10.0f;
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("GroundSpeed <= 10"))
			{
				EnsureGraphVariable(Asset, FName("GroundSpeed"), EAnimGraphPinType::Float);
				T.RuleKind = ETransitionRuleKind::FloatCompare;
				T.VariableName = FName("GroundSpeed");
				T.Op = ETransitionOp::LessEqual;
				T.Threshold = 10.0f;
				bChanged = true;
			}
			if (ImGui::Button("IsFalling true"))
			{
				EnsureGraphVariable(Asset, FName("IsFalling"), EAnimGraphPinType::Bool);
				T.RuleKind = ETransitionRuleKind::BoolProperty;
				T.VariableName = FName("IsFalling");
				T.Threshold = 1.0f;
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("IsFalling false"))
			{
				EnsureGraphVariable(Asset, FName("IsFalling"), EAnimGraphPinType::Bool);
				T.RuleKind = ETransitionRuleKind::BoolProperty;
				T.VariableName = FName("IsFalling");
				T.Threshold = 0.0f;
				bChanged = true;
			}
			if (ImGui::Button("Animation End"))
			{
				T.RuleKind = ETransitionRuleKind::AutomaticSequenceEnd;
				T.VariableName = FName::None;
				T.Threshold = 0.0f;
				bChanged = true;
			}
		}
		return bChanged;
	}

	// Transition 한 개의 form. true 반환 — 변경 발생.
	bool RenderTransitionRow(FAnimGraphTransition& T, const TArray<FAnimGraphState>& States, UAnimGraphAsset* Asset, UClass* OwnerCls, int32 Index)
	{
		ImGui::PushID(Index);
		bool bChanged = false;

		ImGui::TextUnformatted("From");
		bChanged |= StateNameCombo("##From", States, T.FromStateName, /*bAllowAny*/true);

		ImGui::TextUnformatted("To");
		bChanged |= StateNameCombo("##To", States, T.ToStateName, /*bAllowAny*/false);

		ImGui::Separator();
		bChanged |= RenderTransitionRuleCompactEditor(T, Asset, OwnerCls);

		ImGui::PopID();
		return bChanged;
	}

	// 노드 타입별 properties. 변경 시 Asset.BumpVersion() — UAnimGraphInstance 가 다음 frame 에
	// 재컴파일하여 in-editor live preview 가 즉시 반영되도록.
	bool RenderNodeInspector(UAnimGraphAsset& Asset, FAnimGraphNode& Node)
	{
		ImGui::TextColored(NodeHeaderColor(Node.Type), "%s", NodeTypeLabel(Node.Type));
		ImGui::SameLine();
		ImGui::TextDisabled("%s", NodeRoleShort(Node.Type));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", NodeRoleLong(Node.Type));
		ImGui::Separator();

		bool bChanged = false;

		switch (Node.Type)
		{
			case EAnimGraphNodeType::SequencePlayer:
			{
				ImGui::TextUnformatted("Sequence");
				const FString PreviewStem = Node.SequencePath.empty() ? FString("None") : GetStemFromPath(Node.SequencePath);
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##NodeSequence", PreviewStem.c_str()))
				{
					const bool bSelectedNone = Node.SequencePath.empty();
					if (ImGui::Selectable("None", bSelectedNone))
					{
						if (!Node.SequencePath.empty()) bChanged = true;
						Node.SequencePath.clear();
					}
					if (bSelectedNone) ImGui::SetItemDefaultFocus();

					const TArray<FAssetListItem>& AnimFiles = FAssetRegistry::ListByTypeName("UAnimSequence");
					for (const FAssetListItem& Item : AnimFiles)
					{
						const bool bSelected = (Node.SequencePath == Item.FullPath);
						if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
						{
							if (Node.SequencePath != Item.FullPath) bChanged = true;
							Node.SequencePath = Item.FullPath;
						}
						if (bSelected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::DragFloat("##PlayRate", &Node.PlayRate, 0.05f, -4.0f, 4.0f, "PlayRate %.2f"))
				{
					bChanged = true;
				}
				if (ImGui::Checkbox("Looping", &Node.bLooping))
				{
					bChanged = true;
				}
				break;
			}

			case EAnimGraphNodeType::Slot:
			{
				// SlotName 편집 — 비어있으면 컴파일러가 DefaultMontageSlot 으로 fallback.
				char Buf[64];
				const FString Cur = (Node.SlotName == FName::None) ? FString() : Node.SlotName.ToString();
				std::snprintf(Buf, sizeof(Buf), "%s", Cur.c_str());
				ImGui::TextUnformatted("Slot Name");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##SlotName", Buf, sizeof(Buf)))
				{
					Node.SlotName = (Buf[0] == '\0') ? FName::None : FName(Buf);
					bChanged = true;
				}
				ImGui::TextDisabled("(empty → DefaultSlot)");
				break;
			}

			case EAnimGraphNodeType::LayeredBlendPerBone:
			{
				ImGui::TextUnformatted("Blend Weight");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::SliderFloat("##BlendWeight", &Node.BlendWeight, 0.0f, 1.0f, "%.2f"))
				{
					bChanged = true;
				}

				// Root bone — 비어있으면 full blend, 있으면 해당 본 + 자손만 BlendPose 적용.
				ImGui::TextUnformatted("Root Bone");
				char Buf[96];
				std::snprintf(Buf, sizeof(Buf), "%s", Node.RootBoneName.c_str());
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##RootBone", Buf, sizeof(Buf)))
				{
					Node.RootBoneName = Buf;
					bChanged = true;
				}
				ImGui::TextDisabled("(empty → 모든 본 blend)");
				break;
			}

			case EAnimGraphNodeType::VariableGet:
			{
				UClass* OwnerCls = UClass::FindByName(Asset.GetOwnerClassName().c_str());
				ImGui::TextUnformatted("Variable");
				if (VariableNameCombo("##VariableName", &Asset, OwnerCls, Node.VariableName))
				{
					EAnimGraphPinType VarType = EAnimGraphPinType::Float;
					if (TryResolveVariableType(&Asset, OwnerCls, Node.VariableName, VarType))
					{
						SetVariableGetOutputType(Node, VarType);
					}
					bChanged = true;
				}
				EAnimGraphPinType Resolved = EAnimGraphPinType::Float;
				if (TryResolveVariableType(&Asset, OwnerCls, Node.VariableName, Resolved))
				{
					ImGui::TextDisabled("Output: %s", VariableTypeLabel(Resolved));
				}
				else
				{
					ImGui::TextDisabled("Output type unknown until variable is resolved.");
				}
				break;
			}

			case EAnimGraphNodeType::StateMachine:
			{
				UClass* OwnerCls = UClass::FindByName(Asset.GetOwnerClassName().c_str());

				ImGui::TextUnformatted("Initial State");
				if (StateNameCombo("##Initial", Node.States, Node.InitialStateName, /*bAllowAny*/false))
				{
					bChanged = true;
				}

				ImGui::Spacing();

				// ── States ──
				char Header[64];
				std::snprintf(Header, sizeof(Header), "States (%zu)###States", Node.States.size());
				if (ImGui::CollapsingHeader(Header, ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (ImGui::Button("Add State"))
					{
						FAnimGraphState S;
						S.StateName = FName("NewState");
						Node.States.push_back(std::move(S));
						// 첫 state 면 InitialStateName 자동 박기 — SM 컴파일 시 SetInitialState 가
						// None 이면 SM 자체가 동작 안 함.
						if (Node.InitialStateName == FName::None)
						{
							Node.InitialStateName = Node.States.back().StateName;
						}
						bChanged = true;
					}

					int32 PendingDeleteIdx = -1;
					for (int32 i = 0; i < static_cast<int32>(Node.States.size()); ++i)
					{
						ImGui::PushID(i);
						ImGui::Separator();

						// rename cascade — RenderStateRow 가 state.StateName 직접 수정하므로
						// 호출 전 이전 이름 캡쳐, 호출 후 비교해 InitialStateName / Transitions 의
						// stale ref 자동 갱신.
						const FName PrevName = Node.States[i].StateName;
						if (RenderStateRow(Node.States[i], i, Node.NodeId, Asset.GetNodes()))
						{
							bChanged = true;
							const FName NewName = Node.States[i].StateName;
							if (PrevName != NewName)
							{
								if (Node.InitialStateName == PrevName)
								{
									Node.InitialStateName = NewName;
								}
								for (FAnimGraphTransition& T : Node.Transitions)
								{
									if (T.FromStateName == PrevName) T.FromStateName = NewName;
									if (T.ToStateName   == PrevName) T.ToStateName   = NewName;
								}
							}
						}
						if (ImGui::Button("Delete##State")) PendingDeleteIdx = i;
						ImGui::PopID();
					}
					if (PendingDeleteIdx >= 0)
					{
						const FName DeletedName = Node.States[PendingDeleteIdx].StateName;
						Node.States.erase(Node.States.begin() + PendingDeleteIdx);

						// InitialStateName 이 삭제된 state 면 남은 첫 state 로 교체 (없으면 None).
						if (Node.InitialStateName == DeletedName)
						{
							Node.InitialStateName = Node.States.empty()
								? FName::None
								: Node.States.front().StateName;
						}

						// 그 state 를 가리키는 transitions 제거 — stale ref 방지.
						Node.Transitions.erase(std::remove_if(Node.Transitions.begin(), Node.Transitions.end(),
							[&DeletedName](const FAnimGraphTransition& T)
							{
								return T.FromStateName == DeletedName || T.ToStateName == DeletedName;
							}), Node.Transitions.end());

						bChanged = true;
					}
				}

				ImGui::Spacing();

				// ── Transitions ──
				std::snprintf(Header, sizeof(Header), "Transitions (%zu)###Transitions", Node.Transitions.size());
				if (ImGui::CollapsingHeader(Header, ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (ImGui::Button("Add Transition"))
					{
						Node.Transitions.push_back(FAnimGraphTransition{});
						bChanged = true;
					}

					int32 PendingDeleteIdx = -1;
					for (int32 i = 0; i < static_cast<int32>(Node.Transitions.size()); ++i)
					{
						ImGui::PushID(i);
						ImGui::Separator();
						if (RenderTransitionRow(Node.Transitions[i], Node.States, &Asset, OwnerCls, i)) bChanged = true;
						if (ImGui::Button("Delete##Trans")) PendingDeleteIdx = i;
						ImGui::PopID();
					}
					if (PendingDeleteIdx >= 0)
					{
						Node.Transitions.erase(Node.Transitions.begin() + PendingDeleteIdx);
						bChanged = true;
					}
				}
				break;
			}

			default:
				ImGui::TextDisabled("(no editable properties yet)");
				break;
		}

		if (bChanged) Asset.BumpVersion();
		return bChanged;
	}


	bool RenderGraphVariablesPanel(UAnimGraphAsset& Asset)
	{
		bool bChanged = false;
		if (!ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_None))
		{
			return false;
		}

		ImGui::TextDisabled("Transition Rule과 Variable Get이 읽는 값입니다.");
		if (ImGui::Button("+ Add"))
		{
			Asset.AddVariable(FName("NewVariable"), EAnimGraphPinType::Float);
			bChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Locomotion Defaults"))
		{
			auto AddDefaultVar = [&](const char* Name, EAnimGraphPinType Type)
			{
				if (!Asset.FindVariable(FName(Name)))
				{
					FAnimGraphVariable* V = Asset.AddVariable(FName(Name), Type);
					if (V) V->Category = "Locomotion";
					bChanged = true;
				}
			};
			AddDefaultVar("Speed", EAnimGraphPinType::Float);
			AddDefaultVar("GroundSpeed", EAnimGraphPinType::Float);
			AddDefaultVar("Direction", EAnimGraphPinType::Float);
			AddDefaultVar("ShouldMove", EAnimGraphPinType::Bool);
			AddDefaultVar("IsFalling", EAnimGraphPinType::Bool);
			AddDefaultVar("bIsFalling", EAnimGraphPinType::Bool);
		}

		if (ImGui::CollapsingHeader("Lua BP 연동", ImGuiTreeNodeFlags_None))
		{
			ImGui::BulletText("Get Skeletal Mesh Component");
			ImGui::BulletText("Get Anim Instance");
			ImGui::BulletText("Set Anim Graph Float/Bool/Int");
			ImGui::TextDisabled("Success=false이면 AnimInstance가 AnimGraphInstance가 아니거나 변수명이 다릅니다.");
		}

		int32 PendingDeleteIndex = -1;
		for (int32 i = 0; i < static_cast<int32>(Asset.EditVariables().size()); ++i)
		{
			FAnimGraphVariable& Var = Asset.EditVariables()[i];
			ImGui::PushID(i);
			ImGui::Separator();
			const FString Header = Var.VariableName == FName::None ? FString("Variable") : Var.VariableName.ToString();
			const bool bOpen = ImGui::TreeNodeEx("##VariableRow", ImGuiTreeNodeFlags_None, "%s  (%s)", Header.c_str(), VariableTypeLabel(Var.Type));
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 48.0f);
			if (ImGui::SmallButton("Del"))
			{
				PendingDeleteIndex = i;
			}
			if (bOpen)
			{
				char NameBuf[96];
				std::snprintf(NameBuf, sizeof(NameBuf), "%s", Var.VariableName == FName::None ? "" : Var.VariableName.ToString().c_str());
				ImGui::TextUnformatted("Name");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##VariableName", NameBuf, sizeof(NameBuf)))
				{
					const FName OldName = Var.VariableName;
					const FName NewName = NameBuf[0] == '\0' ? FName::None : FName(NameBuf);
					if (NewName != FName::None && NewName != OldName)
					{
						if (Asset.RenameVariable(OldName, NewName))
						{
							bChanged = true;
						}
					}
				}

				ImGui::TextUnformatted("Type");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##VariableType", VariableTypeLabel(Var.Type)))
				{
					for (EAnimGraphPinType Type : { EAnimGraphPinType::Float, EAnimGraphPinType::Bool, EAnimGraphPinType::Int })
					{
						const bool bSel = Var.Type == Type;
						if (ImGui::Selectable(VariableTypeLabel(Type), bSel))
						{
							Var.Type = Type;
							if (Type == EAnimGraphPinType::Bool) Var.DefaultValue = Var.DefaultValue >= 0.5f ? 1.0f : 0.0f;
							for (FAnimGraphNode& Node : const_cast<TArray<FAnimGraphNode>&>(Asset.GetNodes()))
							{
								if (Node.Type == EAnimGraphNodeType::VariableGet && Node.VariableName == Var.VariableName)
								{
									SetVariableGetOutputType(Node, Type);
								}
							}
							bChanged = true;
						}
					}
					ImGui::EndCombo();
				}

				ImGui::TextUnformatted("Default");
				ImGui::SetNextItemWidth(-1.0f);
				if (Var.Type == EAnimGraphPinType::Bool)
				{
					bool bValue = Var.DefaultValue >= 0.5f;
					if (ImGui::Checkbox("##VariableDefaultBool", &bValue))
					{
						Var.DefaultValue = bValue ? 1.0f : 0.0f;
						bChanged = true;
					}
				}
				else if (Var.Type == EAnimGraphPinType::Int)
				{
					int32 Value = static_cast<int32>(std::floor(Var.DefaultValue + 0.5f));
					if (ImGui::DragInt("##VariableDefaultInt", &Value, 1.0f))
					{
						Var.DefaultValue = static_cast<float>(Value);
						bChanged = true;
					}
				}
				else
				{
					if (ImGui::DragFloat("##VariableDefaultFloat", &Var.DefaultValue, 0.1f))
					{
						bChanged = true;
					}
				}

				char CategoryBuf[96];
				std::snprintf(CategoryBuf, sizeof(CategoryBuf), "%s", Var.Category.c_str());
				ImGui::TextUnformatted("Category");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputText("##VariableCategory", CategoryBuf, sizeof(CategoryBuf)))
				{
					Var.Category = CategoryBuf;
					bChanged = true;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (PendingDeleteIndex >= 0 && PendingDeleteIndex < static_cast<int32>(Asset.EditVariables().size()))
		{
			const FName DeleteName = Asset.EditVariables()[PendingDeleteIndex].VariableName;
			if (Asset.RemoveVariable(DeleteName))
			{
				bChanged = true;
			}
		}

		if (bChanged) Asset.BumpVersion();
		return bChanged;
	}


	// ── StateMachine internal graph synthetic ids ──
	// 별도 자산 필드를 늘리지 않고 기존 States / Transitions 를 UE 스타일 캔버스로 투영한다.
	// id 상위 바이트를 예약해 root graph 의 실제 NodeId/PinId/LinkId 와 충돌을 피한다.
	constexpr uint32 kStateEntryNodeBase      = 0x71000000u;
	constexpr uint32 kStateNodeBase           = 0x72000000u;
	constexpr uint32 kStateEntryOutPinBase    = 0x73000000u;
	constexpr uint32 kAnyStateNodeBase        = 0x78000000u;
	constexpr uint32 kAnyStateOutPinBase      = 0x79000000u;
	constexpr uint32 kStateInPinBase          = 0x74000000u;
	constexpr uint32 kStateOutPinBase         = 0x75000000u;
	constexpr uint32 kStateEntryLinkBase      = 0x76000000u;
	constexpr uint32 kStateTransitionLinkBase = 0x77000000u;
	constexpr uint32 kStateIdMask             = 0xFF000000u;
	constexpr uint32 kStateMachineBitsMask    = 0x00FFF000u;
	constexpr uint32 kStateIndexMask          = 0x00000FFFu;

	uint32 MakeStateMachineScopedId(uint32 Base, uint32 StateMachineNodeId, uint32 Index)
	{
		return Base | ((StateMachineNodeId & 0xFFFu) << 12) | (Index & kStateIndexMask);
	}

	bool IsStateMachineScopedId(uint32 Id, uint32 Base, uint32 StateMachineNodeId)
	{
		return (Id & kStateIdMask) == Base &&
			((Id & kStateMachineBitsMask) == ((StateMachineNodeId & 0xFFFu) << 12));
	}

	uint32 MakeEntryNodeId(uint32 StateMachineNodeId) { return MakeStateMachineScopedId(kStateEntryNodeBase, StateMachineNodeId, 0); }
	uint32 MakeEntryOutPinId(uint32 StateMachineNodeId) { return MakeStateMachineScopedId(kStateEntryOutPinBase, StateMachineNodeId, 0); }
	uint32 MakeAnyStateNodeId(uint32 StateMachineNodeId) { return MakeStateMachineScopedId(kAnyStateNodeBase, StateMachineNodeId, 0); }
	uint32 MakeAnyStateOutPinId(uint32 StateMachineNodeId) { return MakeStateMachineScopedId(kAnyStateOutPinBase, StateMachineNodeId, 0); }
	uint32 MakeStateNodeId(uint32 StateMachineNodeId, int32 StateIndex) { return MakeStateMachineScopedId(kStateNodeBase, StateMachineNodeId, static_cast<uint32>(StateIndex)); }
	uint32 MakeStateInPinId(uint32 StateMachineNodeId, int32 StateIndex) { return MakeStateMachineScopedId(kStateInPinBase, StateMachineNodeId, static_cast<uint32>(StateIndex)); }
	uint32 MakeStateOutPinId(uint32 StateMachineNodeId, int32 StateIndex) { return MakeStateMachineScopedId(kStateOutPinBase, StateMachineNodeId, static_cast<uint32>(StateIndex)); }
	uint32 MakeEntryLinkId(uint32 StateMachineNodeId, int32 StateIndex) { return MakeStateMachineScopedId(kStateEntryLinkBase, StateMachineNodeId, static_cast<uint32>(StateIndex)); }
	uint32 MakeTransitionLinkId(uint32 StateMachineNodeId, int32 TransitionIndex) { return MakeStateMachineScopedId(kStateTransitionLinkBase, StateMachineNodeId, static_cast<uint32>(TransitionIndex)); }

	bool DecodeStateIndexFromNodeId(uint32 StateMachineNodeId, uint32 NodeId, int32& OutIndex)
	{
		if (!IsStateMachineScopedId(NodeId, kStateNodeBase, StateMachineNodeId)) return false;
		OutIndex = static_cast<int32>(NodeId & kStateIndexMask);
		return true;
	}

	bool DecodeStateIndexFromInPinId(uint32 StateMachineNodeId, uint32 PinId, int32& OutIndex)
	{
		if (!IsStateMachineScopedId(PinId, kStateInPinBase, StateMachineNodeId)) return false;
		OutIndex = static_cast<int32>(PinId & kStateIndexMask);
		return true;
	}

	bool DecodeStateIndexFromOutPinId(uint32 StateMachineNodeId, uint32 PinId, int32& OutIndex)
	{
		if (!IsStateMachineScopedId(PinId, kStateOutPinBase, StateMachineNodeId)) return false;
		OutIndex = static_cast<int32>(PinId & kStateIndexMask);
		return true;
	}

	bool DecodeTransitionIndexFromLinkId(uint32 StateMachineNodeId, uint32 LinkId, int32& OutIndex)
	{
		if (!IsStateMachineScopedId(LinkId, kStateTransitionLinkBase, StateMachineNodeId)) return false;
		OutIndex = static_cast<int32>(LinkId & kStateIndexMask);
		return true;
	}

	bool DecodeEntryIndexFromLinkId(uint32 StateMachineNodeId, uint32 LinkId, int32& OutIndex)
	{
		if (!IsStateMachineScopedId(LinkId, kStateEntryLinkBase, StateMachineNodeId)) return false;
		OutIndex = static_cast<int32>(LinkId & kStateIndexMask);
		return true;
	}

	int32 FindStateIndexByName(const TArray<FAnimGraphState>& States, FName Name)
	{
		for (int32 i = 0; i < static_cast<int32>(States.size()); ++i)
		{
			if (States[i].StateName == Name) return i;
		}
		return -1;
	}

	FString FormatStateClipSummary(const FAnimGraphState& State)
	{
		if (State.SubGraphNodeId != 0)
		{
			char Buf[64];
			std::snprintf(Buf, sizeof(Buf), "Sub State Machine #%u", State.SubGraphNodeId);
			return Buf;
		}
		if (State.SequencePath.empty() || State.SequencePath == "None") return "No Animation";
		return GetStemFromPath(State.SequencePath);
	}

	FString FormatStatePlaybackSummary(const FAnimGraphState& State)
	{
		char Buf[96];
		std::snprintf(Buf, sizeof(Buf), "%s  |  %.2fx", State.bLooping ? "Loop" : "Once", State.PlayRate);
		return Buf;
	}

	FString FormatTransitionSummary(const FAnimGraphTransition& T)
	{
		char Buf[192];
		switch (T.RuleKind)
		{
			case ETransitionRuleKind::FloatCompare:
			{
				const FString Var = (T.VariableName == FName::None) ? FString("<select variable>") : T.VariableName.ToString();
				std::snprintf(Buf, sizeof(Buf), "%s %s %.2f  |  Blend %.2fs",
					Var.c_str(), TransitionOpLabel(T.Op), T.Threshold, T.BlendTime);
				return Buf;
			}
			case ETransitionRuleKind::BoolProperty:
			{
				const FString Var = (T.VariableName == FName::None) ? FString("<select bool>") : T.VariableName.ToString();
				std::snprintf(Buf, sizeof(Buf), "%s is %s  |  Blend %.2fs",
					Var.c_str(), T.Threshold >= 0.5f ? "true" : "false", T.BlendTime);
				return Buf;
			}
			case ETransitionRuleKind::TimeRemaining:
				std::snprintf(Buf, sizeof(Buf), "Current state time remaining <= %.2fs  |  Blend %.2fs", T.Threshold, T.BlendTime);
				return Buf;
			case ETransitionRuleKind::TimeRemainingRatio:
				std::snprintf(Buf, sizeof(Buf), "Current state time remaining ratio <= %.2f  |  Blend %.2fs", T.Threshold, T.BlendTime);
				return Buf;
			case ETransitionRuleKind::TimeElapsed:
				std::snprintf(Buf, sizeof(Buf), "Current state time >= %.2fs  |  Blend %.2fs", T.Threshold, T.BlendTime);
				return Buf;
			case ETransitionRuleKind::AutomaticSequenceEnd:
				std::snprintf(Buf, sizeof(Buf), "When non-looping source sequence reaches the end  |  Blend %.2fs", T.BlendTime);
				return Buf;
			case ETransitionRuleKind::AlwaysTrue:
				std::snprintf(Buf, sizeof(Buf), "Always true  |  Blend %.2fs", T.BlendTime);
				return Buf;
			case ETransitionRuleKind::AlwaysFalse:
				std::snprintf(Buf, sizeof(Buf), "Always false  |  Blend %.2fs", T.BlendTime);
				return Buf;
		}
		return "Transition Rule";
	}

	FString FormatTransitionRuleNodeTitle(const FAnimGraphTransition& T)
	{
		switch (T.RuleKind)
		{
			case ETransitionRuleKind::FloatCompare:
				return "Float > Compare";
			case ETransitionRuleKind::BoolProperty:
				return "Bool Property";
			case ETransitionRuleKind::TimeRemaining:
				return "Time Remaining";
			case ETransitionRuleKind::TimeRemainingRatio:
				return "Time Remaining Ratio";
			case ETransitionRuleKind::TimeElapsed:
				return "Current State Time";
			case ETransitionRuleKind::AutomaticSequenceEnd:
				return "Automatic Rule";
			case ETransitionRuleKind::AlwaysTrue:
				return "Literal True";
			case ETransitionRuleKind::AlwaysFalse:
				return "Literal False";
		}
		return "Rule Node";
	}

	FName MakeUniqueStateName(const TArray<FAnimGraphState>& States, const char* Prefix)
	{
		for (int32 Candidate = 1; Candidate < 10000; ++Candidate)
		{
			char Buf[64];
			std::snprintf(Buf, sizeof(Buf), "%s%d", Prefix, Candidate);
			FName Name(Buf);
			if (FindStateIndexByName(States, Name) < 0) return Name;
		}
		return FName("State");
	}

	void RemoveStateAndCascade(FAnimGraphNode& StateMachineNode, int32 StateIndex)
	{
		if (StateIndex < 0 || StateIndex >= static_cast<int32>(StateMachineNode.States.size())) return;

		const FName DeletedName = StateMachineNode.States[StateIndex].StateName;
		StateMachineNode.States.erase(StateMachineNode.States.begin() + StateIndex);

		if (StateMachineNode.InitialStateName == DeletedName)
		{
			StateMachineNode.InitialStateName = StateMachineNode.States.empty()
				? FName::None
				: StateMachineNode.States.front().StateName;
		}

		StateMachineNode.Transitions.erase(std::remove_if(StateMachineNode.Transitions.begin(), StateMachineNode.Transitions.end(),
			[&DeletedName](const FAnimGraphTransition& T)
			{
				return T.FromStateName == DeletedName || T.ToStateName == DeletedName;
			}), StateMachineNode.Transitions.end());
	}

	void RenameStateAndCascade(FAnimGraphNode& StateMachineNode, FName OldName, FName NewName)
	{
		if (OldName == NewName) return;
		if (StateMachineNode.InitialStateName == OldName) StateMachineNode.InitialStateName = NewName;
		for (FAnimGraphTransition& T : StateMachineNode.Transitions)
		{
			if (T.FromStateName == OldName) T.FromStateName = NewName;
			if (T.ToStateName   == OldName) T.ToStateName   = NewName;
		}
	}

	ETransitionOp InvertTransitionOp(ETransitionOp Op)
	{
		switch (Op)
		{
			case ETransitionOp::Greater:      return ETransitionOp::LessEqual;
			case ETransitionOp::GreaterEqual: return ETransitionOp::Less;
			case ETransitionOp::Less:         return ETransitionOp::GreaterEqual;
			case ETransitionOp::LessEqual:    return ETransitionOp::Greater;
			case ETransitionOp::Equal:        return ETransitionOp::NotEqual;
			case ETransitionOp::NotEqual:     return ETransitionOp::Equal;
		}
		return ETransitionOp::NotEqual;
	}

	int32 FindTransitionIndex(const FAnimGraphNode& StateMachineNode, FName FromStateName, FName ToStateName)
	{
		for (int32 i = 0; i < static_cast<int32>(StateMachineNode.Transitions.size()); ++i)
		{
			const FAnimGraphTransition& T = StateMachineNode.Transitions[i];
			if (T.FromStateName == FromStateName && T.ToStateName == ToStateName) return i;
		}
		return -1;
	}

	bool HasTransition(const FAnimGraphNode& StateMachineNode, FName FromStateName, FName ToStateName)
	{
		return FindTransitionIndex(StateMachineNode, FromStateName, ToStateName) >= 0;
	}

	FAnimGraphTransition MakeInvertedReverseTransition(const FAnimGraphTransition& Source)
	{
		FAnimGraphTransition Reverse = Source;
		Reverse.FromStateName = Source.ToStateName;
		Reverse.ToStateName   = Source.FromStateName;

		// Safe auto-inversion is only possible for simple scalar/bool property rules.
		// Time-based rules are bound to the source state and cannot be meaningfully inverted.
		switch (Source.RuleKind)
		{
			case ETransitionRuleKind::FloatCompare:
				Reverse.Op = InvertTransitionOp(Source.Op);
				break;
			case ETransitionRuleKind::BoolProperty:
				Reverse.Threshold = Source.Threshold >= 0.5f ? 0.0f : 1.0f;
				break;
			case ETransitionRuleKind::AlwaysTrue:
			case ETransitionRuleKind::AlwaysFalse:
			case ETransitionRuleKind::TimeRemaining:
			case ETransitionRuleKind::TimeRemainingRatio:
			case ETransitionRuleKind::TimeElapsed:
			case ETransitionRuleKind::AutomaticSequenceEnd:
				Reverse.RuleKind = ETransitionRuleKind::AlwaysFalse;
				Reverse.VariableName = FName::None;
				Reverse.Threshold = 0.0f;
				break;
		}
		return Reverse;
	}

	bool AddTransitionIfMissing(FAnimGraphNode& StateMachineNode, const FAnimGraphTransition& Transition)
	{
		if (Transition.ToStateName == FName::None) return false;
		if (HasTransition(StateMachineNode, Transition.FromStateName, Transition.ToStateName)) return false;
		StateMachineNode.Transitions.push_back(Transition);
		return true;
	}

	bool AddReverseTransitionFrom(FAnimGraphNode& StateMachineNode, int32 TransitionIndex)
	{
		if (TransitionIndex < 0 || TransitionIndex >= static_cast<int32>(StateMachineNode.Transitions.size())) return false;
		const FAnimGraphTransition& Source = StateMachineNode.Transitions[TransitionIndex];
		if (Source.FromStateName == FName::None || Source.ToStateName == FName::None) return false;
		FAnimGraphTransition Reverse = MakeInvertedReverseTransition(Source);
		return AddTransitionIfMissing(StateMachineNode, Reverse);
	}

	bool AddReverseTransitionsForAllStatePairs(FAnimGraphNode& StateMachineNode)
	{
		bool bChanged = false;
		const int32 OriginalCount = static_cast<int32>(StateMachineNode.Transitions.size());
		for (int32 i = 0; i < OriginalCount; ++i)
		{
			bChanged |= AddReverseTransitionFrom(StateMachineNode, i);
		}
		return bChanged;
	}


	bool IsOutputPoseNode(const UAnimGraphAsset& Asset, uint32 NodeId)
	{
		const FAnimGraphNode* Node = Asset.FindNode(NodeId);
		return Node && Node->Type == EAnimGraphNodeType::OutputPose;
	}

	ImVec2 ComputeAnimGraphNodeFragmentMin(const TArray<FAnimGraphNode>& Nodes)
	{
		ImVec2 Min(FLT_MAX, FLT_MAX);
		for (const FAnimGraphNode& Node : Nodes)
		{
			Min.x = std::min(Min.x, Node.PosX);
			Min.y = std::min(Min.y, Node.PosY);
		}
		if (Min.x == FLT_MAX || Min.y == FLT_MAX) return ImVec2(0, 0);
		return Min;
	}

	bool StringContainsInsensitive(const FString& Haystack, const char* Needle)
	{
		if (!Needle || Needle[0] == '\0') return true;
		std::string H = Haystack;
		std::string N = Needle;
		std::transform(H.begin(), H.end(), H.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
		std::transform(N.begin(), N.end(), N.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
		return H.find(N) != std::string::npos;
	}

	bool NodeTypeCanConnectToPendingPin(EAnimGraphNodeType Type, const FAnimGraphPin& DragPin, const UAnimGraphAsset& Asset)
	{
		if (Type == EAnimGraphNodeType::OutputPose && Asset.HasOutputPoseNode()) return false;

		auto HasInput = [&](EAnimGraphPinType PinType) -> bool
		{
			switch (Type)
			{
				case EAnimGraphNodeType::OutputPose:          return PinType == EAnimGraphPinType::Pose;
				case EAnimGraphNodeType::Slot:                return PinType == EAnimGraphPinType::Pose;
				case EAnimGraphNodeType::LayeredBlendPerBone: return PinType == EAnimGraphPinType::Pose;
				case EAnimGraphNodeType::BlendListByEnum:     return PinType == EAnimGraphPinType::Pose || PinType == EAnimGraphPinType::Float;
				default:                                      return false;
			}
		};
		auto HasOutput = [&](EAnimGraphPinType PinType) -> bool
		{
			switch (Type)
			{
				case EAnimGraphNodeType::SequencePlayer:
				case EAnimGraphNodeType::StateMachine:
				case EAnimGraphNodeType::Slot:
				case EAnimGraphNodeType::LayeredBlendPerBone:
				case EAnimGraphNodeType::BlendListByEnum:
				case EAnimGraphNodeType::RefPose:
					return PinType == EAnimGraphPinType::Pose;
				case EAnimGraphNodeType::VariableGet:
					return PinType == EAnimGraphPinType::Float;
				default:
					return false;
			}
		};

		return DragPin.Kind == EAnimGraphPinKind::Output
			? HasInput(DragPin.Type)
			: HasOutput(DragPin.Type);
	}

	FAnimGraphPin* FindFirstCompatiblePinOnNode(UAnimGraphAsset& Asset, FAnimGraphNode& Node, const FAnimGraphPin& DragPin)
	{
		for (FAnimGraphPin& Pin : Node.Pins)
		{
			uint32 FromPinId = 0;
			uint32 ToPinId = 0;
			if (Asset.CanLinkPins(DragPin.PinId, Pin.PinId, &FromPinId, &ToPinId)) return &Pin;
		}
		return nullptr;
	}

	bool RemoveLinksForPin(UAnimGraphAsset& Asset, uint32 PinId)
	{
		TArray<uint32> LinksToRemove;
		for (const FAnimGraphLink& Link : Asset.GetLinks())
		{
			if (Link.FromPinId == PinId || Link.ToPinId == PinId)
			{
				LinksToRemove.push_back(Link.LinkId);
			}
		}
		bool bChanged = false;
		for (uint32 LinkId : LinksToRemove)
		{
			bChanged |= Asset.RemoveLink(LinkId);
		}
		return bChanged;
	}

	FString DescribePin(const UAnimGraphAsset& Asset, uint32 PinId)
	{
		const FAnimGraphPin* Pin = Asset.FindPin(PinId);
		if (!Pin) return "Missing Pin";
		const FAnimGraphNode* Node = Asset.FindNode(Pin->OwningNodeId);
		FString NodeName = Node ? NodeTitleForDisplay(*Node) : FString("Missing Node");
		return NodeName + "." + Pin->DisplayName.ToString();
	}

	void AddValidationMessage(TArray<FString>& Messages, bool& bOk, const FString& Text, bool bError)
	{
		Messages.push_back((bError ? FString("Error: ") : FString("Warning: ")) + Text);
		if (bError) bOk = false;
	}


	bool IsStateMachineNodeRefValid(const UAnimGraphAsset& Asset, uint32 NodeId)
	{
		const FAnimGraphNode* Node = Asset.FindNode(NodeId);
		return Node && Node->Type == EAnimGraphNodeType::StateMachine;
	}

	bool DoesStateMachineReachNode(const UAnimGraphAsset& Asset, uint32 CurrentNodeId, uint32 TargetNodeId, std::unordered_set<uint32>& Visiting)
	{
		if (CurrentNodeId == 0 || TargetNodeId == 0) return false;
		if (CurrentNodeId == TargetNodeId) return true;
		if (Visiting.count(CurrentNodeId)) return false;
		Visiting.insert(CurrentNodeId);

		const FAnimGraphNode* Node = Asset.FindNode(CurrentNodeId);
		if (!Node || Node->Type != EAnimGraphNodeType::StateMachine) return false;
		for (const FAnimGraphState& State : Node->States)
		{
			if (State.SubGraphNodeId == 0) continue;
			if (State.SubGraphNodeId == TargetNodeId) return true;
			if (DoesStateMachineReachNode(Asset, State.SubGraphNodeId, TargetNodeId, Visiting)) return true;
		}
		return false;
	}

	bool WouldCreateSubGraphCycle(const UAnimGraphAsset& Asset, uint32 OwnerStateMachineNodeId, uint32 CandidateSubGraphNodeId)
	{
		if (OwnerStateMachineNodeId == 0 || CandidateSubGraphNodeId == 0) return false;
		if (OwnerStateMachineNodeId == CandidateSubGraphNodeId) return true;
		std::unordered_set<uint32> Visiting;
		return DoesStateMachineReachNode(Asset, CandidateSubGraphNodeId, OwnerStateMachineNodeId, Visiting);
	}

	FString FormatStateMachineDisplayName(const FAnimGraphNode& Node)
	{
		const FString Title = NodeTitleForDisplay(Node);
		char Buf[256];
		std::snprintf(Buf, sizeof(Buf), "%s  (%zu states)", Title.c_str(), Node.States.size());
		return Buf;
	}

	FString FormatTransitionDisplayName(const FAnimGraphTransition& T)
	{
		const FString From = T.FromStateName == FName::None ? FString("Any State") : T.FromStateName.ToString();
		const FString To = T.ToStateName == FName::None ? FString("<missing>") : T.ToStateName.ToString();
		return From + " -> " + To;
	}

}

FAnimGraphEditorWidget::~FAnimGraphEditorWidget()
{
	DestroyContext();
}

bool FAnimGraphEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UAnimGraphAsset>();
}

bool FAnimGraphEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const UAnimGraphAsset* CurrentAsset = Cast<UAnimGraphAsset>(EditedObject);
	const UAnimGraphAsset* RequestedAsset = Cast<UAnimGraphAsset>(Object);
	if (!IsOpen() || !CurrentAsset || !RequestedAsset)
	{
		return false;
	}

	const FString& CurrentPath = CurrentAsset->GetSourcePath();
	return !CurrentPath.empty() && CurrentPath == RequestedAsset->GetSourcePath();
}

void FAnimGraphEditorWidget::Open(UObject* Object)
{
	if (!CanEdit(Object))
	{
		return;
	}

	FAssetEditorWidget::Open(Object);
	EnsureContext();
	bPositionsPushed = false;
	OpenStateMachineNodeId = 0;
	OpenStateIndex = -1;
	OpenTransitionIndex = -1;
	ViewMode = EViewMode::RootAnimGraph;
	bStateMachinePositionsPushed = false;
	SanitizeGraphReferences(Cast<UAnimGraphAsset>(EditedObject));
	CaptureInitialUndoSnapshot();
	ValidateGraph(Cast<UAnimGraphAsset>(EditedObject));
}

void FAnimGraphEditorWidget::Close()
{
	OpenStateMachineNodeId = 0;
	OpenStateIndex = -1;
	OpenTransitionIndex = -1;
	ViewMode = EViewMode::RootAnimGraph;
	bStateMachinePositionsPushed = false;
	DestroyContext();
	FAssetEditorWidget::Close();
}


void FAnimGraphEditorWidget::SanitizeGraphReferences(UAnimGraphAsset* Asset)
{
	if (!Asset) return;

	std::unordered_set<uint32> PinIds;
	for (const FAnimGraphNode& Node : Asset->GetNodes())
	{
		for (const FAnimGraphPin& Pin : Node.Pins)
		{
			PinIds.insert(Pin.PinId);
		}
	}

	TArray<uint32> LinksToRemove;
	std::unordered_map<uint32, uint32> LastLinkForInputPin;
	for (const FAnimGraphLink& Link : Asset->GetLinks())
	{
		const FAnimGraphPin* From = Asset->FindPin(Link.FromPinId);
		const FAnimGraphPin* To   = Asset->FindPin(Link.ToPinId);
		bool bRemove = false;
		if (!From || !To || !PinIds.count(Link.FromPinId) || !PinIds.count(Link.ToPinId))
		{
			bRemove = true;
		}
		else if (From->Kind != EAnimGraphPinKind::Output || To->Kind != EAnimGraphPinKind::Input || From->Type != To->Type)
		{
			bRemove = true;
		}

		if (bRemove)
		{
			LinksToRemove.push_back(Link.LinkId);
			continue;
		}

		auto It = LastLinkForInputPin.find(To->PinId);
		if (It != LastLinkForInputPin.end())
		{
			// Old assets may contain multiple links feeding one input pin. Keep the most recently
			// encountered link and remove the older one so runtime compilation never sees fan-in.
			LinksToRemove.push_back(It->second);
		}
		LastLinkForInputPin[To->PinId] = Link.LinkId;
	}
	for (uint32 LinkId : LinksToRemove)
	{
		Asset->RemoveLink(LinkId);
	}

	TArray<uint32> StateMachineIds;
	for (const FAnimGraphNode& Node : Asset->GetNodes())
	{
		if (Node.Type == EAnimGraphNodeType::StateMachine) StateMachineIds.push_back(Node.NodeId);
	}

	for (uint32 NodeId : StateMachineIds)
	{
		FAnimGraphNode* Node = Asset->FindNode(NodeId);
		if (!Node || Node->Type != EAnimGraphNodeType::StateMachine) continue;

		if (!Node->States.empty() && FindStateIndexByName(Node->States, Node->InitialStateName) < 0)
		{
			Node->InitialStateName = Node->States.front().StateName;
			Asset->BumpVersion();
		}
		else if (Node->States.empty() && Node->InitialStateName != FName::None)
		{
			Node->InitialStateName = FName::None;
			Asset->BumpVersion();
		}

		for (FAnimGraphState& State : Node->States)
		{
			if (State.SubGraphNodeId == 0) continue;
			if (!IsStateMachineNodeRefValid(*Asset, State.SubGraphNodeId) || WouldCreateSubGraphCycle(*Asset, Node->NodeId, State.SubGraphNodeId))
			{
				State.SubGraphNodeId = 0;
				Asset->BumpVersion();
			}
		}

		std::unordered_set<std::string> SeenTransitions;
		Node->Transitions.erase(std::remove_if(Node->Transitions.begin(), Node->Transitions.end(),
			[&](const FAnimGraphTransition& T)
			{
				const bool bMissingTo = T.ToStateName == FName::None || FindStateIndexByName(Node->States, T.ToStateName) < 0;
				const bool bMissingFrom = T.FromStateName != FName::None && FindStateIndexByName(Node->States, T.FromStateName) < 0;
				if (bMissingTo || bMissingFrom)
				{
					Asset->BumpVersion();
					return true;
				}

				const FString Key = (T.FromStateName == FName::None ? FString("<Any>") : T.FromStateName.ToString()) + "->" + T.ToStateName.ToString();
				if (SeenTransitions.count(Key))
				{
					Asset->BumpVersion();
					return true;
				}
				SeenTransitions.insert(Key);
				return false;
			}), Node->Transitions.end());
	}
}

bool FAnimGraphEditorWidget::RenderAnimBlueprintNavigator(UAnimGraphAsset& Asset, uint32 CurrentStateMachineNodeId, int32 CurrentStateIndex, int32 CurrentTransitionIndex, float Height)
{
	bool bChanged = false;
	ImGui::BeginChild("##AnimBlueprintNavigator", ImVec2(245.0f, Height), ImGuiChildFlags_Borders);
	ImGui::TextUnformatted("My Blueprint");
	ImGui::Separator();

	const bool bRootSelected = ViewMode == EViewMode::RootAnimGraph;
	if (ImGui::Selectable("AnimGraph", bRootSelected))
	{
		ViewMode = EViewMode::RootAnimGraph;
		OpenStateMachineNodeId = 0;
		OpenStateIndex = -1;
		OpenTransitionIndex = -1;
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("최종 포즈를 Output Pose에 연결하는 최상위 그래프입니다.");

	if (ImGui::TreeNodeEx("State Machines", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const FAnimGraphNode& Node : Asset.GetNodes())
		{
			if (Node.Type != EAnimGraphNodeType::StateMachine) continue;
			ImGui::PushID(static_cast<int>(Node.NodeId));
			const bool bSmSelected = CurrentStateMachineNodeId == Node.NodeId && ViewMode == EViewMode::StateMachine;
			const FString SmLabel = FormatStateMachineDisplayName(Node);
			if (ImGui::Selectable(SmLabel.c_str(), bSmSelected))
			{
				ViewMode = EViewMode::StateMachine;
				OpenStateMachineNodeId = Node.NodeId;
				OpenStateIndex = -1;
				OpenTransitionIndex = -1;
				bStateMachinePositionsPushed = false;
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("상태 박스와 전환선을 배치합니다.");

			if (ImGui::TreeNodeEx("States", ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (int32 StateIdx = 0; StateIdx < static_cast<int32>(Node.States.size()); ++StateIdx)
				{
					const FAnimGraphState& State = Node.States[StateIdx];
					ImGui::PushID(StateIdx);
					const bool bSelected = CurrentStateMachineNodeId == Node.NodeId && CurrentStateIndex == StateIdx && ViewMode == EViewMode::StatePose;
					FString Label = State.StateName == FName::None ? FString("<unnamed>") : State.StateName.ToString();
					if (Node.InitialStateName == State.StateName) Label += "  [Entry]";
					if (ImGui::Selectable(Label.c_str(), bSelected))
					{
						ViewMode = EViewMode::StatePose;
						OpenStateMachineNodeId = Node.NodeId;
						OpenStateIndex = StateIdx;
						OpenTransitionIndex = -1;
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("이 상태에서 출력할 애니메이션 포즈를 편집합니다.");
					ImGui::PopID();
				}
				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Transition Rules", ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (int32 TransitionIdx = 0; TransitionIdx < static_cast<int32>(Node.Transitions.size()); ++TransitionIdx)
				{
					const FAnimGraphTransition& T = Node.Transitions[TransitionIdx];
					ImGui::PushID(10000 + TransitionIdx);
					const bool bSelected = CurrentStateMachineNodeId == Node.NodeId && CurrentTransitionIndex == TransitionIdx && ViewMode == EViewMode::TransitionRule;
					const FString Label = FormatTransitionDisplayName(T);
					if (ImGui::Selectable(Label.c_str(), bSelected))
					{
						ViewMode = EViewMode::TransitionRule;
						OpenStateMachineNodeId = Node.NodeId;
						OpenStateIndex = -1;
						OpenTransitionIndex = TransitionIdx;
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", FormatTransitionSummary(T).c_str());
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Variables", ImGuiTreeNodeFlags_None))
	{
		for (const FAnimGraphVariable& Var : Asset.GetVariables())
		{
			const FString Label = Var.VariableName.ToString() + "  (" + VariableTypeLabel(Var.Type) + ")";
			ImGui::BulletText("%s", Label.c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lua BP/C++에서 같은 이름으로 값을 넣고, Transition Rule/Variable Get에서 읽습니다.");
		}
		if (Asset.GetVariables().empty())
		{
			ImGui::TextDisabled("변수 없음");
		}
		ImGui::TreePop();
	}

	ImGui::Separator();
	if (ImGui::CollapsingHeader("빠른 사용법", ImGuiTreeNodeFlags_None))
	{
		ImGui::BulletText("State: 재생할 포즈");
		ImGui::BulletText("Transition: 이동 경로");
		ImGui::BulletText("Rule: 이동 조건");
		ImGui::BulletText("Variable: BP/C++ 입력값");
	}
	ImGui::EndChild();
	return bChanged;
}

void FAnimGraphEditorWidget::EnsureContext()
{
	if (NodeEditorContext) return;

	ed::Config Cfg;
	Cfg.SettingsFile = nullptr; // 자산 자체에 직렬화 — node-editor 의 디스크 캐시 비활성.
	NodeEditorContext = ed::CreateEditor(&Cfg);
}

void FAnimGraphEditorWidget::DestroyContext()
{
	if (NodeEditorContext)
	{
		ed::DestroyEditor(NodeEditorContext);
		NodeEditorContext = nullptr;
	}
}

void FAnimGraphEditorWidget::CaptureInitialUndoSnapshot()
{
	UndoStack.clear();
	RedoStack.clear();
	UAnimGraphAsset* Asset = Cast<UAnimGraphAsset>(EditedObject);
	if (!Asset) return;
	const TArray<uint8> Snapshot = MakeGraphSnapshot(Asset);
	if (!Snapshot.empty()) UndoStack.push_back(Snapshot);
}

TArray<uint8> FAnimGraphEditorWidget::MakeGraphSnapshot(UAnimGraphAsset* Asset) const
{
	TArray<uint8> Empty;
	if (!Asset) return Empty;
	FMemoryArchive Saver(true);
	Asset->Serialize(Saver);
	return Saver.GetBuffer();
}

bool FAnimGraphEditorWidget::RestoreGraphSnapshot(UAnimGraphAsset* Asset, const TArray<uint8>& Snapshot)
{
	if (!Asset || Snapshot.empty()) return false;
	bRestoringSnapshot = true;
	FMemoryArchive Loader(Snapshot, false);
	Asset->Serialize(Loader);
	Asset->BumpVersion();
	bRestoringSnapshot = false;
	bPositionsPushed = false;
	bStateMachinePositionsPushed = false;
	MarkDirty();
	ValidateGraph(Asset);
	return true;
}

void FAnimGraphEditorWidget::CommitGraphEdit(UAnimGraphAsset* Asset)
{
	if (!Asset || bRestoringSnapshot) return;
	SanitizeGraphReferences(Asset);
	const TArray<uint8> Snapshot = MakeGraphSnapshot(Asset);
	if (!Snapshot.empty() && (UndoStack.empty() || UndoStack.back() != Snapshot))
	{
		UndoStack.push_back(Snapshot);
		if (UndoStack.size() > 128) UndoStack.erase(UndoStack.begin());
	}
	RedoStack.clear();
	MarkDirty();
	ValidateGraph(Asset);
}

void FAnimGraphEditorWidget::UndoGraphEdit(UAnimGraphAsset* Asset)
{
	if (!Asset || UndoStack.size() <= 1) return;
	RedoStack.push_back(UndoStack.back());
	UndoStack.pop_back();
	RestoreGraphSnapshot(Asset, UndoStack.back());
}

void FAnimGraphEditorWidget::RedoGraphEdit(UAnimGraphAsset* Asset)
{
	if (!Asset || RedoStack.empty()) return;
	const TArray<uint8> Snapshot = RedoStack.back();
	RedoStack.pop_back();
	UndoStack.push_back(Snapshot);
	RestoreGraphSnapshot(Asset, Snapshot);
}

bool FAnimGraphEditorWidget::GatherSelectedNodes(UAnimGraphAsset* Asset, TArray<FAnimGraphNode>& OutNodes, TArray<FAnimGraphLink>& OutLinks) const
{
	OutNodes.clear();
	OutLinks.clear();
	if (!Asset || !NodeEditorContext) return false;

	ed::NodeId Selected[512];
	const int Count = ed::GetSelectedNodes(Selected, 512);
	if (Count <= 0) return false;

	std::unordered_set<uint32> SelectedIds;
	for (int i = 0; i < Count; ++i) SelectedIds.insert(NodeIdToU32(Selected[i]));

	for (const FAnimGraphNode& Node : Asset->GetNodes())
	{
		if (!SelectedIds.count(Node.NodeId)) continue;
		if (Node.Type == EAnimGraphNodeType::OutputPose) continue;
		FAnimGraphNode Copy = Node;
		const ImVec2 Pos = ed::GetNodePosition(ToNodeId(Node.NodeId));
		Copy.PosX = Pos.x;
		Copy.PosY = Pos.y;
		OutNodes.push_back(std::move(Copy));
	}

	for (const FAnimGraphLink& Link : Asset->GetLinks())
	{
		const FAnimGraphPin* From = Asset->FindPin(Link.FromPinId);
		const FAnimGraphPin* To   = Asset->FindPin(Link.ToPinId);
		if (From && To && SelectedIds.count(From->OwningNodeId) && SelectedIds.count(To->OwningNodeId))
		{
			OutLinks.push_back(Link);
		}
	}
	return !OutNodes.empty();
}

bool FAnimGraphEditorWidget::CloneNodeFragment(UAnimGraphAsset* Asset, const TArray<FAnimGraphNode>& SourceNodes, const TArray<FAnimGraphLink>& SourceLinks, const ImVec2& TargetAnchor, TArray<uint32>* OutNewNodeIds)
{
	if (!Asset || SourceNodes.empty()) return false;
	const ImVec2 SourceAnchor = ComputeAnimGraphNodeFragmentMin(SourceNodes);
	std::unordered_map<uint32, uint32> PinIdMap;
	std::unordered_map<uint32, uint32> NodeIdMap;
	TArray<uint32> NewNodeIds;

	for (const FAnimGraphNode& SrcNode : SourceNodes)
	{
		if (SrcNode.Type == EAnimGraphNodeType::OutputPose) continue;
		FAnimGraphNode* NewNode = Asset->AddNodeOfType(SrcNode.Type,
			TargetAnchor.x + (SrcNode.PosX - SourceAnchor.x),
			TargetAnchor.y + (SrcNode.PosY - SourceAnchor.y));
		if (!NewNode) continue;

		const TArray<FAnimGraphPin> NewPins = NewNode->Pins;
		NewNode->DisplayName = SrcNode.DisplayName;
		NewNode->SequencePath = SrcNode.SequencePath;
		NewNode->SequenceRef = nullptr;
		NewNode->PlayRate = SrcNode.PlayRate;
		NewNode->bLooping = SrcNode.bLooping;
		NewNode->SlotName = SrcNode.SlotName;
		NewNode->BlendWeight = SrcNode.BlendWeight;
		NewNode->RootBoneName = SrcNode.RootBoneName;
		NewNode->VariableName = SrcNode.VariableName;
		NewNode->States = SrcNode.States;
		NewNode->Transitions = SrcNode.Transitions;
		NewNode->InitialStateName = SrcNode.InitialStateName;

		const size_t PinCount = std::min(SrcNode.Pins.size(), NewPins.size());
		for (size_t i = 0; i < PinCount; ++i)
		{
			PinIdMap[SrcNode.Pins[i].PinId] = NewPins[i].PinId;
		}

		NodeIdMap[SrcNode.NodeId] = NewNode->NodeId;
		NewNodeIds.push_back(NewNode->NodeId);
		if (NodeEditorContext) ed::SetNodePosition(ToNodeId(NewNode->NodeId), ImVec2(NewNode->PosX, NewNode->PosY));
	}

	// StateMachine state subgraph references are node ids. Copying a fragment must not leave
	// states pointing back to the original fragment. Remap intra-fragment references and clear
	// external ones so the duplicate is self-contained and cannot dangle when the source is deleted.
	for (uint32 NewNodeId : NewNodeIds)
	{
		FAnimGraphNode* NewNode = Asset->FindNode(NewNodeId);
		if (!NewNode || NewNode->Type != EAnimGraphNodeType::StateMachine) continue;
		for (FAnimGraphState& State : NewNode->States)
		{
			if (State.SubGraphNodeId == 0) continue;
			auto It = NodeIdMap.find(State.SubGraphNodeId);
			State.SubGraphNodeId = (It != NodeIdMap.end()) ? It->second : 0;
		}
	}

	for (const FAnimGraphLink& SrcLink : SourceLinks)
	{
		auto FromIt = PinIdMap.find(SrcLink.FromPinId);
		auto ToIt = PinIdMap.find(SrcLink.ToPinId);
		if (FromIt == PinIdMap.end() || ToIt == PinIdMap.end()) continue;
		uint32 FromPinId = 0;
		uint32 ToPinId = 0;
		if (Asset->CanLinkPins(FromIt->second, ToIt->second, &FromPinId, &ToPinId))
		{
			Asset->AddLink(FromPinId, ToPinId);
		}
	}

	if (OutNewNodeIds) *OutNewNodeIds = NewNodeIds;
	return !NewNodeIds.empty();
}

void FAnimGraphEditorWidget::SelectOnlyNodes(const TArray<uint32>& NodeIds)
{
	if (!NodeEditorContext) return;
	ed::ClearSelection();
	bool bAppend = false;
	for (uint32 NodeId : NodeIds)
	{
		ed::SelectNode(ToNodeId(NodeId), bAppend);
		bAppend = true;
	}
}

void FAnimGraphEditorWidget::CopySelectedNodes(UAnimGraphAsset* Asset)
{
	GatherSelectedNodes(Asset, ClipboardNodes, ClipboardLinks);
}

void FAnimGraphEditorWidget::PasteCopiedNodes(UAnimGraphAsset* Asset, const ImVec2* OverrideAnchor)
{
	if (!Asset || ClipboardNodes.empty()) return;
	const ImVec2 DefaultAnchor(ComputeAnimGraphNodeFragmentMin(ClipboardNodes).x + 32.0f, ComputeAnimGraphNodeFragmentMin(ClipboardNodes).y + 32.0f);
	const ImVec2 Anchor = OverrideAnchor ? *OverrideAnchor : DefaultAnchor;
	TArray<uint32> NewIds;
	if (CloneNodeFragment(Asset, ClipboardNodes, ClipboardLinks, Anchor, &NewIds))
	{
		SelectOnlyNodes(NewIds);
		CommitGraphEdit(Asset);
	}
}

void FAnimGraphEditorWidget::DuplicateSelectedNodes(UAnimGraphAsset* Asset)
{
	CopySelectedNodes(Asset);
	PasteCopiedNodes(Asset);
}

void FAnimGraphEditorWidget::DeleteSelectedNodes(UAnimGraphAsset* Asset)
{
	if (!Asset || !NodeEditorContext) return;
	ed::NodeId Selected[512];
	const int Count = ed::GetSelectedNodes(Selected, 512);
	if (Count <= 0) return;

	bool bChanged = false;
	for (int i = 0; i < Count; ++i)
	{
		const uint32 NodeId = NodeIdToU32(Selected[i]);
		if (IsOutputPoseNode(*Asset, NodeId)) continue;
		bChanged |= Asset->RemoveNode(NodeId);
	}
	if (bChanged) CommitGraphEdit(Asset);
}

void FAnimGraphEditorWidget::QueueRootGraphShortcuts(UAnimGraphAsset* Asset)
{
	(void)Asset;
	ImGuiIO& IO = ImGui::GetIO();
	if (IO.WantTextInput) return;

	if (IO.KeyCtrl)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Z))
		{
			if (IO.KeyShift) RedoGraphEdit(Asset);
			else UndoGraphEdit(Asset);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Y)) RedoGraphEdit(Asset);
		if (ImGui::IsKeyPressed(ImGuiKey_C)) bQueuedCopySelected = true;
		if (ImGui::IsKeyPressed(ImGuiKey_V)) bQueuedPasteNodes = true;
		if (ImGui::IsKeyPressed(ImGuiKey_D)) bQueuedDuplicateSelected = true;
		if (ImGui::IsKeyPressed(ImGuiKey_S) && Asset)
		{
			FAnimGraphManager::Get().Save(Asset);
			ClearDirty();
		}
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete)) bQueuedDeleteSelected = true;
	if (ImGui::IsKeyPressed(ImGuiKey_F))
	{
		ed::NavigateToContent(0.25f);
	}
}

void FAnimGraphEditorWidget::ProcessQueuedRootGraphCommands(UAnimGraphAsset* Asset)
{
	if (bQueuedCopySelected)
	{
		CopySelectedNodes(Asset);
		bQueuedCopySelected = false;
	}
	if (bQueuedPasteNodes)
	{
		PasteCopiedNodes(Asset);
		bQueuedPasteNodes = false;
	}
	if (bQueuedDuplicateSelected)
	{
		DuplicateSelectedNodes(Asset);
		bQueuedDuplicateSelected = false;
	}
	if (bQueuedDeleteSelected)
	{
		DeleteSelectedNodes(Asset);
		bQueuedDeleteSelected = false;
	}
}

void FAnimGraphEditorWidget::ValidateGraph(UAnimGraphAsset* Asset)
{
	LastValidationMessages.clear();
	bLastValidationOk = true;
	if (!Asset)
	{
		bLastValidationOk = false;
		LastValidationMessages.push_back("Error: no AnimGraph asset");
		return;
	}

	UClass* OwnerCls = UClass::FindByName(Asset->GetOwnerClassName().c_str());

	int32 OutputCount = 0;
	const FAnimGraphNode* OutputNode = nullptr;
	std::unordered_set<uint32> PinIds;
	std::unordered_map<uint32, int32> InputFanIn;

	for (const FAnimGraphNode& Node : Asset->GetNodes())
	{
		if (Node.Type == EAnimGraphNodeType::OutputPose)
		{
			++OutputCount;
			OutputNode = &Node;
		}
		for (const FAnimGraphPin& Pin : Node.Pins)
		{
			PinIds.insert(Pin.PinId);
		}
	}

	if (OutputCount != 1)
	{
		char Buf[128];
		std::snprintf(Buf, sizeof(Buf), "Output Pose node count is %d. AnimGraph requires exactly one Output Pose.", OutputCount);
		AddValidationMessage(LastValidationMessages, bLastValidationOk, Buf, true);
	}

	for (const FAnimGraphLink& Link : Asset->GetLinks())
	{
		if (!PinIds.count(Link.FromPinId) || !PinIds.count(Link.ToPinId))
		{
			AddValidationMessage(LastValidationMessages, bLastValidationOk, "Graph has a dangling link to a deleted pin.", true);
			continue;
		}
		const FAnimGraphPin* From = Asset->FindPin(Link.FromPinId);
		const FAnimGraphPin* To = Asset->FindPin(Link.ToPinId);
		if (!From || !To) continue;
		if (From->Kind != EAnimGraphPinKind::Output || To->Kind != EAnimGraphPinKind::Input)
		{
			AddValidationMessage(LastValidationMessages, bLastValidationOk, "A link is not normalized as Output -> Input.", true);
		}
		if (From->Type != To->Type)
		{
			AddValidationMessage(LastValidationMessages, bLastValidationOk, "A link connects pins with different types.", true);
		}
		InputFanIn[To->PinId]++;
	}

	for (const auto& Pair : InputFanIn)
	{
		if (Pair.second > 1)
		{
			AddValidationMessage(LastValidationMessages, bLastValidationOk,
				FString("Input pin has multiple upstream links: ") + DescribePin(*Asset, Pair.first), true);
		}
	}

	if (OutputNode)
	{
		bool bOutputConnected = false;
		for (const FAnimGraphPin& Pin : OutputNode->Pins)
		{
			if (Pin.Kind != EAnimGraphPinKind::Input) continue;
			for (const FAnimGraphLink& Link : Asset->GetLinks())
			{
				if (Link.ToPinId == Pin.PinId) bOutputConnected = true;
			}
		}
		if (!bOutputConnected)
		{
			AddValidationMessage(LastValidationMessages, bLastValidationOk,
				"Output Pose Result is not connected. Runtime will not produce the intended pose.", true);
		}
	}

	{
		std::unordered_set<std::string> VariableNames;
		for (const FAnimGraphVariable& Var : Asset->GetVariables())
		{
			const FString VarName = Var.VariableName.ToString();
			if (Var.VariableName == FName::None || VarName.empty())
			{
				AddValidationMessage(LastValidationMessages, bLastValidationOk,
					"AnimGraph has a variable without a name.", true);
			}
			else if (VariableNames.count(VarName))
			{
				AddValidationMessage(LastValidationMessages, bLastValidationOk,
					"AnimGraph has duplicate variable name: " + VarName, true);
			}
			VariableNames.insert(VarName);
		}
	}

	// Reachability from Output Pose through all upstream data/pose links.
	std::unordered_set<uint32> ReachableNodeIds;
	TArray<uint32> Stack;
	if (OutputNode) Stack.push_back(OutputNode->NodeId);
	while (!Stack.empty())
	{
		const uint32 NodeId = Stack.back();
		Stack.pop_back();
		if (ReachableNodeIds.count(NodeId)) continue;
		ReachableNodeIds.insert(NodeId);
		const FAnimGraphNode* Node = Asset->FindNode(NodeId);
		if (!Node) continue;
		for (const FAnimGraphPin& Pin : Node->Pins)
		{
			if (Pin.Kind != EAnimGraphPinKind::Input) continue;
			for (const FAnimGraphLink& Link : Asset->GetLinks())
			{
				if (Link.ToPinId != Pin.PinId) continue;
				const FAnimGraphPin* SrcPin = Asset->FindPin(Link.FromPinId);
				if (SrcPin) Stack.push_back(SrcPin->OwningNodeId);
			}
		}
	}

	for (const FAnimGraphNode& Node : Asset->GetNodes())
	{
		if (OutputNode && !ReachableNodeIds.count(Node.NodeId) && Node.Type != EAnimGraphNodeType::OutputPose)
		{
			AddValidationMessage(LastValidationMessages, bLastValidationOk,
				NodeTitleForDisplay(Node) + " is not connected to the Output Pose.", false);
		}

		switch (Node.Type)
		{
			case EAnimGraphNodeType::SequencePlayer:
				if (Node.SequencePath.empty() || Node.SequencePath == "None")
				{
					AddValidationMessage(LastValidationMessages, bLastValidationOk,
						NodeTitleForDisplay(Node) + " has no animation sequence assigned.", false);
				}
				break;
			case EAnimGraphNodeType::VariableGet:
				if (Node.VariableName == FName::None)
				{
					AddValidationMessage(LastValidationMessages, bLastValidationOk,
						NodeTitleForDisplay(Node) + " has no variable binding.", false);
				}
				else if (!IsVariableResolvable(Asset, OwnerCls, Node.VariableName))
				{
					AddValidationMessage(LastValidationMessages, bLastValidationOk,
						NodeTitleForDisplay(Node) + " references an unknown variable: " + Node.VariableName.ToString(), true);
				}
				break;
			case EAnimGraphNodeType::StateMachine:
			{
				std::unordered_set<std::string> StateNames;
				for (const FAnimGraphState& S : Node.States)
				{
					const FString SName = S.StateName.ToString();
					if (S.StateName == FName::None)
					{
						AddValidationMessage(LastValidationMessages, bLastValidationOk,
							NodeTitleForDisplay(Node) + " has a state without a name.", true);
					}
					else if (StateNames.count(SName))
					{
						AddValidationMessage(LastValidationMessages, bLastValidationOk,
							NodeTitleForDisplay(Node) + " has duplicate state name: " + SName, true);
					}
					StateNames.insert(SName);
					if (S.SubGraphNodeId != 0)
					{
						if (!IsStateMachineNodeRefValid(*Asset, S.SubGraphNodeId))
						{
							AddValidationMessage(LastValidationMessages, bLastValidationOk,
								NodeTitleForDisplay(Node) + " state " + SName + " references a missing nested State Machine.", true);
						}
						else if (WouldCreateSubGraphCycle(*Asset, Node.NodeId, S.SubGraphNodeId))
						{
							AddValidationMessage(LastValidationMessages, bLastValidationOk,
								NodeTitleForDisplay(Node) + " state " + SName + " creates a nested State Machine cycle.", true);
						}
					}
				}
				if (!Node.States.empty() && FindStateIndexByName(Node.States, Node.InitialStateName) < 0)
				{
					AddValidationMessage(LastValidationMessages, bLastValidationOk,
						NodeTitleForDisplay(Node) + " has no valid Entry state.", true);
				}
				std::unordered_set<std::string> TransitionPairs;
				for (const FAnimGraphTransition& T : Node.Transitions)
				{
					const FString PairKey = (T.FromStateName == FName::None ? FString("<Any>") : T.FromStateName.ToString()) + "->" + T.ToStateName.ToString();
					if (TransitionPairs.count(PairKey))
					{
						AddValidationMessage(LastValidationMessages, bLastValidationOk,
							NodeTitleForDisplay(Node) + " has duplicate transition: " + PairKey, true);
					}
					TransitionPairs.insert(PairKey);
					if (T.ToStateName == FName::None || FindStateIndexByName(Node.States, T.ToStateName) < 0)
					{
						AddValidationMessage(LastValidationMessages, bLastValidationOk,
							NodeTitleForDisplay(Node) + " has a transition with a missing target state.", true);
					}
					if (T.FromStateName != FName::None && FindStateIndexByName(Node.States, T.FromStateName) < 0)
					{
						AddValidationMessage(LastValidationMessages, bLastValidationOk,
							NodeTitleForDisplay(Node) + " has a transition with a missing source state.", true);
					}
					if ((T.RuleKind == ETransitionRuleKind::FloatCompare || T.RuleKind == ETransitionRuleKind::BoolProperty) && T.VariableName == FName::None)
					{
						AddValidationMessage(LastValidationMessages, bLastValidationOk,
							NodeTitleForDisplay(Node) + " has a property-based transition rule without a selected variable.", true);
					}
					else if ((T.RuleKind == ETransitionRuleKind::FloatCompare || T.RuleKind == ETransitionRuleKind::BoolProperty)
						&& !IsVariableResolvable(Asset, OwnerCls, T.VariableName))
					{
						AddValidationMessage(LastValidationMessages, bLastValidationOk,
							NodeTitleForDisplay(Node) + " has a transition rule referencing an unknown variable: " + T.VariableName.ToString(), true);
					}
					else if (T.RuleKind == ETransitionRuleKind::FloatCompare || T.RuleKind == ETransitionRuleKind::BoolProperty)
					{
						EAnimGraphPinType VarType = EAnimGraphPinType::Float;
						if (TryResolveVariableType(Asset, OwnerCls, T.VariableName, VarType) && !VariableTypeMatchesRule(T.RuleKind, VarType))
						{
							AddValidationMessage(LastValidationMessages, bLastValidationOk,
								NodeTitleForDisplay(Node) + " has a transition rule with a mismatched variable type: " + T.VariableName.ToString(), true);
						}
					}
					if (T.RuleKind == ETransitionRuleKind::AlwaysTrue)
					{
						AddValidationMessage(LastValidationMessages, bLastValidationOk,
							NodeTitleForDisplay(Node) + " has an explicit Always True transition rule.", false);
					}
				}
				break;
			}
			default:
				break;
		}
	}

	if (LastValidationMessages.empty())
	{
		LastValidationMessages.push_back("OK: graph is structurally valid.");
	}
}

void FAnimGraphEditorWidget::RenderPinSpawnMenu(UAnimGraphAsset* Asset)
{
	if (!Asset) return;
	const FAnimGraphPin* DragPin = Asset->FindPin(PendingPinSpawnPinId);
	if (!DragPin)
	{
		ImGui::TextDisabled("Drag source pin is gone.");
		return;
	}

	ImGui::TextUnformatted("All Actions for this Pin");
	ImGui::TextDisabled("%s", DescribePin(*Asset, PendingPinSpawnPinId).c_str());
	ImGui::SetNextItemWidth(340.0f);
	ImGui::InputText("##AnimGraphPinSpawnSearch", PinSpawnSearchBuf, sizeof(PinSpawnSearchBuf));
	ImGui::Separator();

	auto AddContextItem = [&](EAnimGraphNodeType Type, const char* Category) -> bool
	{
		const FString Label = NodeTypeLabel(Type);
		const FString SearchText = Label + FString(" ") + Category;
		if (!StringContainsInsensitive(SearchText, PinSpawnSearchBuf)) return false;
		if (!NodeTypeCanConnectToPendingPin(Type, *DragPin, *Asset)) return false;
		ImGui::PushStyleColor(ImGuiCol_Text, NodeTitleColor(Type));
		const bool bClicked = ImGui::Selectable(Label.c_str(), false);
		ImGui::PopStyleColor();
		if (!bClicked) return false;

		FAnimGraphNode* NewNode = Asset->AddNodeOfType(Type, 0.0f, 0.0f);
		if (!NewNode) return false;

		const ImVec2 CanvasPos = ed::ScreenToCanvas(PendingNewNodeScreenPosition);
		NewNode->PosX = CanvasPos.x;
		NewNode->PosY = CanvasPos.y;
		ed::SetNodePosition(ToNodeId(NewNode->NodeId), CanvasPos);

		FAnimGraphPin* Compatible = FindFirstCompatiblePinOnNode(*Asset, *NewNode, *DragPin);
		if (Compatible)
		{
			uint32 FromPinId = 0;
			uint32 ToPinId = 0;
			if (Asset->CanLinkPins(DragPin->PinId, Compatible->PinId, &FromPinId, &ToPinId))
			{
				Asset->AddLink(FromPinId, ToPinId);
			}
		}
		SelectOnlyNodes(TArray<uint32>{ NewNode->NodeId });
		CommitGraphEdit(Asset);
		ImGui::CloseCurrentPopup();
		return true;
	};

	auto AddVariableContextItem = [&](const FAnimGraphVariable& Var) -> bool
	{
		if (Var.VariableName == FName::None) return false;
		// VariableGet 은 현재 float-valued data output 이므로, 빈 공간 드롭은 input data pin 에서만 의미가 있다.
		if (DragPin->Kind != EAnimGraphPinKind::Input || DragPin->Type != EAnimGraphPinType::Float) return false;
		const FString Label = FString("Get ") + Var.VariableName.ToString();
		const FString SearchText = Label + FString(" Variable Property Access Float Bool Int");
		if (!StringContainsInsensitive(SearchText, PinSpawnSearchBuf)) return false;

		ImGui::PushStyleColor(ImGuiCol_Text, NodeTitleColor(EAnimGraphNodeType::VariableGet));
		const bool bClicked = ImGui::Selectable(Label.c_str(), false);
		ImGui::PopStyleColor();
		if (!bClicked) return false;

		FAnimGraphNode* NewNode = Asset->AddNodeOfType(EAnimGraphNodeType::VariableGet, 0.0f, 0.0f);
		if (!NewNode) return false;
		NewNode->VariableName = Var.VariableName;
		SetVariableGetOutputType(*NewNode, Var.Type);

		const ImVec2 CanvasPos = ed::ScreenToCanvas(PendingNewNodeScreenPosition);
		NewNode->PosX = CanvasPos.x;
		NewNode->PosY = CanvasPos.y;
		ed::SetNodePosition(ToNodeId(NewNode->NodeId), CanvasPos);

		FAnimGraphPin* Compatible = FindFirstCompatiblePinOnNode(*Asset, *NewNode, *DragPin);
		if (Compatible)
		{
			uint32 FromPinId = 0;
			uint32 ToPinId = 0;
			if (Asset->CanLinkPins(DragPin->PinId, Compatible->PinId, &FromPinId, &ToPinId))
			{
				Asset->AddLink(FromPinId, ToPinId);
			}
		}
		SelectOnlyNodes(TArray<uint32>{ NewNode->NodeId });
		CommitGraphEdit(Asset);
		ImGui::CloseCurrentPopup();
		return true;
	};

	ImGui::BeginChild("##AnimGraphPinActionList", ImVec2(360.0f, 290.0f), ImGuiChildFlags_Borders);
	if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		AddContextItem(EAnimGraphNodeType::SequencePlayer, "Animation Sequence Player Pose");
		AddContextItem(EAnimGraphNodeType::RefPose, "Animation Reference Pose");
		AddContextItem(EAnimGraphNodeType::Slot, "Animation Montage Slot Pose");
	}
	if (ImGui::CollapsingHeader("Blends", ImGuiTreeNodeFlags_DefaultOpen))
	{
		AddContextItem(EAnimGraphNodeType::LayeredBlendPerBone, "Blend Layered Per Bone Pose");
		AddContextItem(EAnimGraphNodeType::BlendListByEnum, "Blend List Enum Selector Pose");
	}
	if (ImGui::CollapsingHeader("State Machines", ImGuiTreeNodeFlags_DefaultOpen))
	{
		AddContextItem(EAnimGraphNodeType::StateMachine, "State Machine Locomotion Pose");
	}
	if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const FAnimGraphVariable& Var : Asset->GetVariables())
		{
			AddVariableContextItem(Var);
		}
		AddContextItem(EAnimGraphNodeType::VariableGet, "Variable Property Access Data Float");
	}
	if (ImGui::CollapsingHeader("Output", ImGuiTreeNodeFlags_DefaultOpen))
	{
		AddContextItem(EAnimGraphNodeType::OutputPose, "Output Pose Result Root");
	}
	ImGui::EndChild();
}



void FAnimGraphEditorWidget::RenderStatePoseEditor(UAnimGraphAsset& Asset, FAnimGraphNode& StateMachineNode, int32 StateIndex, float AvailableHeight)
{
	if (!NodeEditorContext) return;
	if (StateIndex < 0 || StateIndex >= static_cast<int32>(StateMachineNode.States.size()))
	{
		ViewMode = EViewMode::StateMachine;
		OpenStateIndex = -1;
		return;
	}

	FAnimGraphState& State = StateMachineNode.States[StateIndex];
	AvailableHeight = std::max(AvailableHeight, 240.0f);

	if (ImGui::Button("AnimGraph"))
	{
		ViewMode = EViewMode::RootAnimGraph;
		OpenStateMachineNodeId = 0;
		OpenStateIndex = -1;
		OpenTransitionIndex = -1;
		return;
	}
	ImGui::SameLine();
	ImGui::TextDisabled(">");
	ImGui::SameLine();
	if (ImGui::Button(StateMachineNode.DisplayName.ToString().c_str()))
	{
		ViewMode = EViewMode::StateMachine;
		OpenStateIndex = -1;
		OpenTransitionIndex = -1;
		return;
	}
	ImGui::SameLine();
	ImGui::TextDisabled(">");
	ImGui::SameLine();
	ImGui::TextColored(NodeHeaderColor(EAnimGraphNodeType::SequencePlayer), "%s (State)", State.StateName.ToString().c_str());
	ImGui::Separator();

	const float TotalWidth = ImGui::GetContentRegionAvail().x;
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;
	constexpr float NavigatorWidth = 270.0f;
	constexpr float InspectorWidth = 390.0f;
	const float CanvasWidth = std::max(260.0f, TotalWidth - NavigatorWidth - InspectorWidth - Spacing * 2.0f);

	RenderAnimBlueprintNavigator(Asset, StateMachineNode.NodeId, StateIndex, -1, AvailableHeight);
	ImGui::SameLine();

	ImGui::BeginChild("##StatePoseCanvasChild", ImVec2(CanvasWidth, AvailableHeight), ImGuiChildFlags_None);
	ed::SetCurrentEditor(NodeEditorContext);
	const int StyleColors = PushUnrealAnimGraphCanvasStyle();
	ed::Begin("StatePoseCanvas");

	const uint32 Scope = (StateMachineNode.NodeId & 0xFFFu) << 12;
	const uint32 Index = static_cast<uint32>(StateIndex) & 0xFFFu;
	const uint32 SourceNodeId = 0x51000000u | Scope | Index;
	const uint32 OutputNodeId = 0x52000000u | Scope | Index;
	const uint32 SourceOutPinId = 0x53000000u | Scope | Index;
	const uint32 OutputInPinId = 0x54000000u | Scope | Index;
	const uint32 PoseLinkId = 0x55000000u | Scope | Index;

	ed::SetNodePosition(ToNodeId(SourceNodeId), ImVec2(-220.0f, 0.0f));
	ed::SetNodePosition(ToNodeId(OutputNodeId), ImVec2(130.0f, 0.0f));

	const bool bNested = State.SubGraphNodeId != 0;
	FAnimGraphNode SourceProxy;
	SourceProxy.Type = bNested ? EAnimGraphNodeType::StateMachine : EAnimGraphNodeType::SequencePlayer;
	ed::BeginNode(ToNodeId(SourceNodeId));
	{
		const FString Title = bNested ? FString("Nested State Machine") : (State.SequencePath.empty() ? FString("Sequence Player") : GetStemFromPath(State.SequencePath));
		DrawNodeHeaderBlock(SourceProxy, 250.0f, 44.0f, Title, bNested ? "State Machine" : "Sequence Player");
		ImGui::Dummy(ImVec2(250.0f, 4.0f));
		ImGui::TextWrapped("%s", bNested ? "Uses another state machine as this state's pose." : FormatStateClipSummary(State).c_str());
		ImGui::TextDisabled("%s", FormatStatePlaybackSummary(State).c_str());
		ed::BeginPin(ToPinId(SourceOutPinId), ed::PinKind::Output);
			ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 170.0f);
			ImGui::TextColored(PinTypeColor(EAnimGraphPinType::Pose), "Pose");
			ImGui::SameLine(0.0f, 6.0f);
			DrawAnimGraphPinIcon(EAnimGraphPinType::Pose, true);
		ed::EndPin();
		ImGui::Dummy(ImVec2(250.0f, 2.0f));
	}
	ed::EndNode();

	FAnimGraphNode OutputProxy;
	OutputProxy.Type = EAnimGraphNodeType::OutputPose;
	ed::BeginNode(ToNodeId(OutputNodeId));
	{
		DrawNodeHeaderBlock(OutputProxy, 230.0f, 44.0f, "Output Animation Pose", "State Result");
		ImGui::Dummy(ImVec2(230.0f, 4.0f));
		ed::BeginPin(ToPinId(OutputInPinId), ed::PinKind::Input);
			ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
			DrawAnimGraphPinIcon(EAnimGraphPinType::Pose, true);
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::TextColored(PinTypeColor(EAnimGraphPinType::Pose), "Result");
		ed::EndPin();
		ImGui::TextWrapped("This is the pose returned while the state is active.");
		ImGui::Dummy(ImVec2(230.0f, 2.0f));
	}
	ed::EndNode();

	if (bNested || (!State.SequencePath.empty() && State.SequencePath != "None"))
	{
		ed::Link(ToLinkId(PoseLinkId), ToPinId(SourceOutPinId), ToPinId(OutputInPinId), PinTypeColor(EAnimGraphPinType::Pose), 4.0f);
	}

	ed::End();
	ed::PopStyleColor(StyleColors);
	ed::SetCurrentEditor(nullptr);
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("##StatePoseInspector", ImVec2(0, AvailableHeight), ImGuiChildFlags_Borders);
	bool bChanged = false;
	ImGui::TextColored(NodeHeaderColor(EAnimGraphNodeType::SequencePlayer), "State Details");
	ImGui::Separator();
	const FName OldName = State.StateName;
	if (RenderStateRow(State, StateIndex, StateMachineNode.NodeId, Asset.GetNodes()))
	{
		RenameStateAndCascade(StateMachineNode, OldName, State.StateName);
		bChanged = true;
	}
	ImGui::Spacing();
	if (ImGui::Button("Set as Entry State", ImVec2(-1.0f, 0.0f)))
	{
		StateMachineNode.InitialStateName = State.StateName;
		bChanged = true;
	}
	if (State.SubGraphNodeId != 0)
	{
		FAnimGraphNode* Sub = Asset.FindNode(State.SubGraphNodeId);
		if (Sub && Sub->Type == EAnimGraphNodeType::StateMachine && ImGui::Button("Open Nested State Machine", ImVec2(-1.0f, 0.0f)))
		{
			ViewMode = EViewMode::StateMachine;
			OpenStateMachineNodeId = Sub->NodeId;
			OpenStateIndex = -1;
			OpenTransitionIndex = -1;
			bStateMachinePositionsPushed = false;
		}
	}
	ImGui::Separator();
	if (ImGui::CollapsingHeader("역할", ImGuiTreeNodeFlags_None))
	{
		ImGui::TextWrapped("이 화면은 선택한 State가 출력할 포즈만 편집합니다. 전환 조건은 Transition Rule에서 편집합니다.");
	}
	if (bChanged)
	{
		Asset.BumpVersion();
		CommitGraphEdit(&Asset);
	}
	ImGui::EndChild();
}

void FAnimGraphEditorWidget::RenderTransitionRuleEditor(UAnimGraphAsset& Asset, FAnimGraphNode& StateMachineNode, int32 TransitionIndex, float AvailableHeight)
{
	if (!NodeEditorContext) return;
	if (TransitionIndex < 0 || TransitionIndex >= static_cast<int32>(StateMachineNode.Transitions.size()))
	{
		ViewMode = EViewMode::StateMachine;
		OpenTransitionIndex = -1;
		return;
	}

	FAnimGraphTransition& T = StateMachineNode.Transitions[TransitionIndex];
	AvailableHeight = std::max(AvailableHeight, 240.0f);

	if (ImGui::Button("AnimGraph"))
	{
		ViewMode = EViewMode::RootAnimGraph;
		OpenStateMachineNodeId = 0;
		OpenStateIndex = -1;
		OpenTransitionIndex = -1;
		return;
	}
	ImGui::SameLine();
	ImGui::TextDisabled(">");
	ImGui::SameLine();
	if (ImGui::Button(StateMachineNode.DisplayName.ToString().c_str()))
	{
		ViewMode = EViewMode::StateMachine;
		OpenStateIndex = -1;
		OpenTransitionIndex = -1;
		return;
	}
	ImGui::SameLine();
	ImGui::TextDisabled(">");
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.70f, 0.50f, 1.00f, 1.0f), "%s (Transition Rule)", FormatTransitionDisplayName(T).c_str());
	ImGui::Separator();

	const float TotalWidth = ImGui::GetContentRegionAvail().x;
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;
	constexpr float NavigatorWidth = 270.0f;
	constexpr float InspectorWidth = 420.0f;
	const float CanvasWidth = std::max(260.0f, TotalWidth - NavigatorWidth - InspectorWidth - Spacing * 2.0f);

	RenderAnimBlueprintNavigator(Asset, StateMachineNode.NodeId, -1, TransitionIndex, AvailableHeight);
	ImGui::SameLine();

	ImGui::BeginChild("##TransitionRuleCanvasChild", ImVec2(CanvasWidth, AvailableHeight), ImGuiChildFlags_None);
	ed::SetCurrentEditor(NodeEditorContext);
	const int StyleColors = PushUnrealAnimGraphCanvasStyle();
	ed::Begin("TransitionRuleCanvas");

	const uint32 Scope = (StateMachineNode.NodeId & 0xFFFu) << 12;
	const uint32 Index = static_cast<uint32>(TransitionIndex) & 0xFFFu;
	const uint32 RuleNodeId = 0x56000000u | Scope | Index;
	const uint32 ResultNodeId = 0x57000000u | Scope | Index;
	const uint32 RuleOutPinId = 0x58000000u | Scope | Index;
	const uint32 ResultInPinId = 0x59000000u | Scope | Index;
	const uint32 RuleLinkId = 0x5A000000u | Scope | Index;

	ed::SetNodePosition(ToNodeId(RuleNodeId), ImVec2(-240.0f, 0.0f));
	ed::SetNodePosition(ToNodeId(ResultNodeId), ImVec2(130.0f, 0.0f));

	FAnimGraphNode RuleProxy;
	RuleProxy.Type = EAnimGraphNodeType::VariableGet;
	ed::BeginNode(ToNodeId(RuleNodeId));
	{
		DrawNodeHeaderBlock(RuleProxy, 270.0f, 44.0f, FormatTransitionRuleNodeTitle(T), TransitionRuleKindLabel(T.RuleKind));
		ImGui::Dummy(ImVec2(270.0f, 4.0f));
		TextClippedWithTooltip(FormatTransitionSummary(T), 258.0f);
		ed::BeginPin(ToPinId(RuleOutPinId), ed::PinKind::Output);
			ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 190.0f);
			ImGui::TextColored(PinTypeColor(EAnimGraphPinType::Bool), "Can Enter");
			ImGui::SameLine(0.0f, 6.0f);
			DrawAnimGraphPinIcon(EAnimGraphPinType::Bool, true);
		ed::EndPin();
		ImGui::Dummy(ImVec2(270.0f, 2.0f));
	}
	ed::EndNode();

	FAnimGraphNode ResultProxy;
	ResultProxy.Type = EAnimGraphNodeType::OutputPose;
	ed::BeginNode(ToNodeId(ResultNodeId));
	{
		DrawNodeHeaderBlock(ResultProxy, 230.0f, 44.0f, "Result", "Transition Rule");
		ImGui::Dummy(ImVec2(230.0f, 4.0f));
		ed::BeginPin(ToPinId(ResultInPinId), ed::PinKind::Input);
			ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
			DrawAnimGraphPinIcon(EAnimGraphPinType::Bool, true);
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::TextColored(PinTypeColor(EAnimGraphPinType::Bool), "Result");
		ed::EndPin();
		ImGui::TextWrapped("true이면 전환이 실행됩니다.");
		ImGui::Dummy(ImVec2(230.0f, 2.0f));
	}
	ed::EndNode();
	ed::Link(ToLinkId(RuleLinkId), ToPinId(RuleOutPinId), ToPinId(ResultInPinId), PinTypeColor(EAnimGraphPinType::Bool), 3.0f);

	ed::End();
	ed::PopStyleColor(StyleColors);
	ed::SetCurrentEditor(nullptr);
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("##TransitionRuleInspector", ImVec2(0, AvailableHeight), ImGuiChildFlags_Borders);
	bool bChanged = false;
	ImGui::TextColored(ImVec4(0.70f, 0.50f, 1.00f, 1.0f), "Transition Details");
	ImGui::Separator();
	bChanged |= RenderTransitionRow(T, StateMachineNode.States, &Asset, UClass::FindByName(Asset.GetOwnerClassName().c_str()), TransitionIndex);
	ImGui::Separator();
	const bool bCanCreateReverse = T.FromStateName != FName::None && !HasTransition(StateMachineNode, T.ToStateName, T.FromStateName);
	if (ImGui::Button("Create Reverse Transition", ImVec2(-1.0f, 0.0f)))
	{
		if (bCanCreateReverse && AddReverseTransitionFrom(StateMachineNode, TransitionIndex))
		{
			bChanged = true;
		}
	}
	if (!bCanCreateReverse)
	{
		ImGui::TextDisabled("Reverse transition already exists or this is an Any State transition.");
	}
	if (ImGui::Button("Back To State Machine", ImVec2(-1.0f, 0.0f)))
	{
		ViewMode = EViewMode::StateMachine;
		OpenTransitionIndex = -1;
	}
	ImGui::Separator();
	if (ImGui::CollapsingHeader("역할", ImGuiTreeNodeFlags_None))
	{
		ImGui::TextWrapped("전환선은 경로이고, Rule은 조건입니다. Result가 true가 되는 프레임에 전환됩니다.");
	}
	if (bChanged)
	{
		Asset.BumpVersion();
		CommitGraphEdit(&Asset);
	}
	ImGui::EndChild();
}

void FAnimGraphEditorWidget::RenderStateMachineEditor(UAnimGraphAsset& Asset, FAnimGraphNode& StateMachineNode, float AvailableHeight)
{
	if (!NodeEditorContext) return;
	AvailableHeight = std::max(AvailableHeight, 240.0f);

	// 상단 breadcrumb / 액션 바.
	if (ImGui::Button("<- Root Anim Graph"))
	{
		OpenStateMachineNodeId = 0;
		OpenStateIndex = -1;
		OpenTransitionIndex = -1;
		ViewMode = EViewMode::RootAnimGraph;
		bPositionsPushed = false;
		return;
	}
	ImGui::SameLine();
	ImGui::TextColored(NodeHeaderColor(EAnimGraphNodeType::StateMachine), "State Machine: %s #%u",
		StateMachineNode.DisplayName.ToString().c_str(), StateMachineNode.NodeId);
	ImGui::SameLine();
	ImGui::TextDisabled("Entry: %s | States: %zu | Transitions: %zu",
		(StateMachineNode.InitialStateName == FName::None ? "(none)" : StateMachineNode.InitialStateName.ToString().c_str()),
		StateMachineNode.States.size(), StateMachineNode.Transitions.size());

	if (ImGui::Button("Add State"))
	{
		FAnimGraphState S;
		S.StateName = MakeUniqueStateName(StateMachineNode.States, "State");
		StateMachineNode.States.push_back(std::move(S));
		if (StateMachineNode.InitialStateName == FName::None)
		{
			StateMachineNode.InitialStateName = StateMachineNode.States.back().StateName;
		}
		Asset.BumpVersion();
		CommitGraphEdit(&Asset);
		bStateMachinePositionsPushed = false;
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Chain"))
	{
		StateMachineNode.Transitions.clear();
		for (int32 i = 0; i + 1 < static_cast<int32>(StateMachineNode.States.size()); ++i)
		{
			FAnimGraphTransition T;
			T.FromStateName = StateMachineNode.States[i].StateName;
			T.ToStateName   = StateMachineNode.States[i + 1].StateName;
			T.RuleKind      = ETransitionRuleKind::AlwaysFalse;
			T.BlendTime     = 0.2f;
			StateMachineNode.Transitions.push_back(T);
		}
		Asset.BumpVersion();
		CommitGraphEdit(&Asset);
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Bidirectional"))
	{
		if (AddReverseTransitionsForAllStatePairs(StateMachineNode))
		{
			Asset.BumpVersion();
			CommitGraphEdit(&Asset);
			return;
		}
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Transitions are one-way like Unreal. This adds missing reverse links and safely inverts simple float/bool rules.");
	}
	ImGui::SameLine();
	ImGui::Checkbox("Reverse", &bCreateReverseTransitionOnDrag);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("When enabled, dragging State A to State B also creates B to A. Hold Shift while dragging for the same one-shot behavior.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear"))
	{
		StateMachineNode.Transitions.clear();
		Asset.BumpVersion();
		CommitGraphEdit(&Asset);
		return;
	}
	ImGui::TextDisabled("전환은 단방향입니다. 왕복은 Reverse 전환을 추가하세요.");
	ImGui::Separator();

	constexpr float NavigatorWidth = 270.0f;
	constexpr float InspectorWidth = 390.0f;
	const float Spacing            = ImGui::GetStyle().ItemSpacing.x;
	const float TotalWidth         = ImGui::GetContentRegionAvail().x;
	const float CanvasWidth        = (TotalWidth > NavigatorWidth + InspectorWidth + Spacing * 2.0f + 160.0f)
		? TotalWidth - NavigatorWidth - InspectorWidth - Spacing * 2.0f
		: std::max(260.0f, TotalWidth - NavigatorWidth - Spacing);

	int32 SelectedStateIndex      = -1;
	int32 SelectedTransitionIndex = -1;
	bool  bSelectedEntry          = false;

	RenderAnimBlueprintNavigator(Asset, StateMachineNode.NodeId, -1, -1, AvailableHeight);
	ImGui::SameLine();
	ImGui::BeginChild("##StateMachineCanvasChild", ImVec2(CanvasWidth, AvailableHeight), ImGuiChildFlags_None);

	ed::SetCurrentEditor(NodeEditorContext);
	const int StateMachineStyleColors = PushUnrealAnimGraphCanvasStyle();
	ed::Begin("StateMachineCanvas");

	if (!bStateMachinePositionsPushed)
	{
		ed::SetNodePosition(ToNodeId(MakeEntryNodeId(StateMachineNode.NodeId)), ImVec2(-360.0f, -55.0f));
		ed::SetNodePosition(ToNodeId(MakeAnyStateNodeId(StateMachineNode.NodeId)), ImVec2(-360.0f, 115.0f));
		for (int32 i = 0; i < static_cast<int32>(StateMachineNode.States.size()); ++i)
		{
			const int32 Col = i % 3;
			const int32 Row = i / 3;
			ed::SetNodePosition(ToNodeId(MakeStateNodeId(StateMachineNode.NodeId, i)),
				ImVec2(0.0f + Col * 300.0f, Row * 170.0f));
		}
		bStateMachinePositionsPushed = true;
	}

	// Entry pseudo node.
	{
		const ImVec4 EntryColor(0.95f, 0.80f, 0.25f, 1.0f);
		ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(EntryColor.x, EntryColor.y, EntryColor.z, 0.95f));
		ed::BeginNode(ToNodeId(MakeEntryNodeId(StateMachineNode.NodeId)));
			FAnimGraphNode HeaderProxy;
			HeaderProxy.Type = EAnimGraphNodeType::StateMachine;
			DrawNodeHeaderBlock(HeaderProxy, 190.0f, 40.0f, "Entry", "Initial Transition");
			ImGui::Dummy(ImVec2(190.0f, 3.0f));
			TextClippedWithTooltip(FString("Start: ") + (StateMachineNode.InitialStateName == FName::None ? FString("not set") : StateMachineNode.InitialStateName.ToString()), 178.0f);
			ed::BeginPin(ToPinId(MakeEntryOutPinId(StateMachineNode.NodeId)), ed::PinKind::Output);
				ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
				const float LabelWidth = ImGui::CalcTextSize("Start").x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, 190.0f - LabelWidth - 24.0f));
				ImGui::TextColored(EntryColor, "Start");
				ImGui::SameLine(0.0f, 6.0f);
				DrawAnimGraphPinIcon(EAnimGraphPinType::Pose, StateMachineNode.InitialStateName != FName::None);
			ed::EndPin();
			ImGui::Dummy(ImVec2(190.0f, 2.0f));
		ed::EndNode();
		ed::PopStyleColor(1);
	}

	// Any State pseudo node — UE State Machine 의 전역 전이 시작점.
	{
		const ImVec4 AnyColor(0.60f, 0.46f, 0.92f, 1.0f);
		ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(AnyColor.x, AnyColor.y, AnyColor.z, 0.88f));
		ed::BeginNode(ToNodeId(MakeAnyStateNodeId(StateMachineNode.NodeId)));
			FAnimGraphNode HeaderProxy;
			HeaderProxy.Type = EAnimGraphNodeType::StateMachine;
			DrawNodeHeaderBlock(HeaderProxy, 190.0f, 40.0f, "Any State", "Global Transition");
			ImGui::Dummy(ImVec2(190.0f, 3.0f));
			TextClippedWithTooltip("전역 전환 시작점", 178.0f);
			ed::BeginPin(ToPinId(MakeAnyStateOutPinId(StateMachineNode.NodeId)), ed::PinKind::Output);
				ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
				const float LabelWidth = ImGui::CalcTextSize("Any").x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, 190.0f - LabelWidth - 24.0f));
				ImGui::TextColored(AnyColor, "Any");
				ImGui::SameLine(0.0f, 6.0f);
				DrawAnimGraphPinIcon(EAnimGraphPinType::Pose, true);
			ed::EndPin();
			ImGui::Dummy(ImVec2(190.0f, 2.0f));
		ed::EndNode();
		ed::PopStyleColor(1);
	}

	// State nodes.
	for (int32 i = 0; i < static_cast<int32>(StateMachineNode.States.size()); ++i)
	{
		const FAnimGraphState& State = StateMachineNode.States[i];
		const bool bEntry = (StateMachineNode.InitialStateName == State.StateName);
		const ImVec4 StateColor = bEntry ? ImVec4(0.95f, 0.80f, 0.25f, 1.0f) : NodeTitleColor(EAnimGraphNodeType::StateMachine);
		ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(StateColor.x, StateColor.y, StateColor.z, bEntry ? 0.95f : 0.70f));
		ed::BeginNode(ToNodeId(MakeStateNodeId(StateMachineNode.NodeId, i)));
			const float StateNodeWidth = 238.0f;
			FAnimGraphNode HeaderProxy;
			HeaderProxy.Type = EAnimGraphNodeType::StateMachine;
			const FString Title = State.StateName == FName::None ? FString("State") : State.StateName.ToString();
			DrawNodeHeaderBlock(HeaderProxy, StateNodeWidth, 42.0f, Title, bEntry ? "State  |  Entry State" : "State");
			ImGui::Dummy(ImVec2(StateNodeWidth, 3.0f));

			ed::BeginPin(ToPinId(MakeStateInPinId(StateMachineNode.NodeId, i)), ed::PinKind::Input);
				ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
				DrawAnimGraphPinIcon(EAnimGraphPinType::Pose, true);
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::TextColored(PinTypeColor(EAnimGraphPinType::Pose), "In");
			ed::EndPin();

			TextClippedWithTooltip(FormatStateClipSummary(State), StateNodeWidth - 12.0f);
			TextClippedWithTooltip(FormatStatePlaybackSummary(State), StateNodeWidth - 12.0f);
			if (State.SubGraphNodeId != 0)
			{
				ImGui::TextDisabled("Nested SM");
			}
			if (bEntry)
			{
				ImGui::TextColored(StateColor, "Entry");
			}

			ed::BeginPin(ToPinId(MakeStateOutPinId(StateMachineNode.NodeId, i)), ed::PinKind::Output);
				ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
				const float LabelWidth = ImGui::CalcTextSize("Out").x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, StateNodeWidth - LabelWidth - 24.0f));
				ImGui::TextColored(PinTypeColor(EAnimGraphPinType::Pose), "Out");
				ImGui::SameLine(0.0f, 6.0f);
				DrawAnimGraphPinIcon(EAnimGraphPinType::Pose, true);
			ed::EndPin();
			ImGui::Dummy(ImVec2(StateNodeWidth, 2.0f));
		ed::EndNode();
		ed::PopStyleColor(1);
	}

	// Entry link.
	const int32 EntryStateIndex = FindStateIndexByName(StateMachineNode.States, StateMachineNode.InitialStateName);
	if (EntryStateIndex >= 0)
	{
		ed::Link(ToLinkId(MakeEntryLinkId(StateMachineNode.NodeId, EntryStateIndex)),
			ToPinId(MakeEntryOutPinId(StateMachineNode.NodeId)),
			ToPinId(MakeStateInPinId(StateMachineNode.NodeId, EntryStateIndex)),
			ImVec4(0.95f, 0.80f, 0.25f, 1.0f), 4.0f);
	}

	// Transition links.
	for (int32 i = 0; i < static_cast<int32>(StateMachineNode.Transitions.size()); ++i)
	{
		const FAnimGraphTransition& T = StateMachineNode.Transitions[i];
		const int32 FromIdx = FindStateIndexByName(StateMachineNode.States, T.FromStateName);
		const int32 ToIdx   = FindStateIndexByName(StateMachineNode.States, T.ToStateName);
		if (ToIdx < 0) continue;
		const uint32 FromPinId = (T.FromStateName == FName::None)
			? MakeAnyStateOutPinId(StateMachineNode.NodeId)
			: (FromIdx >= 0 ? MakeStateOutPinId(StateMachineNode.NodeId, FromIdx) : 0);
		if (FromPinId == 0) continue;
		ed::Link(ToLinkId(MakeTransitionLinkId(StateMachineNode.NodeId, i)),
			ToPinId(FromPinId),
			ToPinId(MakeStateInPinId(StateMachineNode.NodeId, ToIdx)),
			T.FromStateName == FName::None ? ImVec4(0.60f, 0.46f, 0.92f, 1.0f) : ImVec4(0.70f, 0.50f, 1.00f, 1.0f),
			3.0f);
	}

	// Drag links: Entry -> State sets InitialState, State -> State creates Transition.
	// Dragging Entry/State/AnyState into empty canvas also creates a new state, matching the UE state machine workflow.
	if (ed::BeginCreate())
	{
		ed::PinId StartId, EndId;
		if (ed::QueryNewLink(&StartId, &EndId))
		{
			if (StartId && EndId)
			{
				uint32 A = PinIdToU32(StartId);
				uint32 B = PinIdToU32(EndId);
				bool bAccept = false;
				bool bEntryToState = false;
				bool bAnyToState = false;
				int32 FromStateIdx = -1;
				int32 ToStateIdx   = -1;

				// node-editor 는 드래그 방향이 자유롭기 때문에 양방향을 정규화한다.
				if (A == MakeEntryOutPinId(StateMachineNode.NodeId) && DecodeStateIndexFromInPinId(StateMachineNode.NodeId, B, ToStateIdx))
				{
					bAccept = true;
					bEntryToState = true;
				}
				else if (B == MakeEntryOutPinId(StateMachineNode.NodeId) && DecodeStateIndexFromInPinId(StateMachineNode.NodeId, A, ToStateIdx))
				{
					bAccept = true;
					bEntryToState = true;
				}
				else if (A == MakeAnyStateOutPinId(StateMachineNode.NodeId) && DecodeStateIndexFromInPinId(StateMachineNode.NodeId, B, ToStateIdx))
				{
					bAccept = true;
					bAnyToState = true;
				}
				else if (B == MakeAnyStateOutPinId(StateMachineNode.NodeId) && DecodeStateIndexFromInPinId(StateMachineNode.NodeId, A, ToStateIdx))
				{
					bAccept = true;
					bAnyToState = true;
				}
				else if (DecodeStateIndexFromOutPinId(StateMachineNode.NodeId, A, FromStateIdx) && DecodeStateIndexFromInPinId(StateMachineNode.NodeId, B, ToStateIdx))
				{
					bAccept = true;
				}
				else if (DecodeStateIndexFromOutPinId(StateMachineNode.NodeId, B, FromStateIdx) && DecodeStateIndexFromInPinId(StateMachineNode.NodeId, A, ToStateIdx))
				{
					bAccept = true;
				}

				if (bAccept)
				{
					if (ed::AcceptNewItem())
					{
						if (bEntryToState)
						{
							if (ToStateIdx >= 0 && ToStateIdx < static_cast<int32>(StateMachineNode.States.size()))
							{
								StateMachineNode.InitialStateName = StateMachineNode.States[ToStateIdx].StateName;
								Asset.BumpVersion();
								CommitGraphEdit(&Asset);
							}
						}
						else if (bAnyToState && ToStateIdx >= 0 && ToStateIdx < static_cast<int32>(StateMachineNode.States.size()))
						{
							FAnimGraphTransition NewT;
							NewT.FromStateName = FName::None;
							NewT.ToStateName   = StateMachineNode.States[ToStateIdx].StateName;
							NewT.RuleKind      = ETransitionRuleKind::AlwaysFalse;
							NewT.BlendTime     = 0.2f;
							if (AddTransitionIfMissing(StateMachineNode, NewT))
							{
								Asset.BumpVersion();
								CommitGraphEdit(&Asset);
							}
						}
						else if (FromStateIdx >= 0 && ToStateIdx >= 0 &&
							FromStateIdx < static_cast<int32>(StateMachineNode.States.size()) &&
							ToStateIdx   < static_cast<int32>(StateMachineNode.States.size()))
						{
							FAnimGraphTransition NewT;
							NewT.FromStateName = StateMachineNode.States[FromStateIdx].StateName;
							NewT.ToStateName   = StateMachineNode.States[ToStateIdx].StateName;
							NewT.RuleKind      = ETransitionRuleKind::AlwaysFalse;
							NewT.BlendTime     = 0.2f;
							bool bAdded = AddTransitionIfMissing(StateMachineNode, NewT);
							if (bAdded && (bCreateReverseTransitionOnDrag || ImGui::GetIO().KeyShift))
							{
								AddReverseTransitionFrom(StateMachineNode, static_cast<int32>(StateMachineNode.Transitions.size()) - 1);
							}
							if (bAdded)
							{
								Asset.BumpVersion();
								CommitGraphEdit(&Asset);
							}
						}
					}
				}
				else
				{
					ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
				}
			}
		}

		ed::PinId NewNodePinId = 0;
		if (ed::QueryNewNode(&NewNodePinId))
		{
			const uint32 PinU = PinIdToU32(NewNodePinId);
			int32 FromStateIdx = -1;
			const bool bFromEntry = (PinU == MakeEntryOutPinId(StateMachineNode.NodeId));
			const bool bFromAny   = (PinU == MakeAnyStateOutPinId(StateMachineNode.NodeId));
			const bool bFromState = DecodeStateIndexFromOutPinId(StateMachineNode.NodeId, PinU, FromStateIdx);
			if ((bFromEntry || bFromAny || bFromState) && ed::AcceptNewItem(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), 1.5f))
			{
				FAnimGraphState S;
				S.StateName = MakeUniqueStateName(StateMachineNode.States, "State");
				StateMachineNode.States.push_back(std::move(S));
				const int32 NewStateIdx = static_cast<int32>(StateMachineNode.States.size()) - 1;
				const FName NewStateName = StateMachineNode.States[NewStateIdx].StateName;

				if (bFromEntry)
				{
					StateMachineNode.InitialStateName = NewStateName;
				}
				else
				{
					FAnimGraphTransition T;
					T.FromStateName = bFromAny ? FName::None : StateMachineNode.States[FromStateIdx].StateName;
					T.ToStateName   = NewStateName;
					T.RuleKind      = ETransitionRuleKind::AlwaysFalse;
					T.BlendTime     = 0.2f;
					const bool bAdded = AddTransitionIfMissing(StateMachineNode, T);
					if (bAdded && bFromState && (bCreateReverseTransitionOnDrag || ImGui::GetIO().KeyShift))
					{
						AddReverseTransitionFrom(StateMachineNode, static_cast<int32>(StateMachineNode.Transitions.size()) - 1);
					}
				}

				const ImVec2 NewPos = ed::ScreenToCanvas(ImGui::GetMousePos());
				ed::SetNodePosition(ToNodeId(MakeStateNodeId(StateMachineNode.NodeId, NewStateIdx)), NewPos);
				Asset.BumpVersion();
				CommitGraphEdit(&Asset);
			}
		}
	}
	ed::EndCreate();

	if (ed::BeginDelete())
	{
		ed::LinkId DeletedLink;
		while (ed::QueryDeletedLink(&DeletedLink))
		{
			if (ed::AcceptDeletedItem())
			{
				int32 TransitionIdx = -1;
				int32 EntryIdx = -1;
				const uint32 LinkU = LinkIdToU32(DeletedLink);
				if (DecodeTransitionIndexFromLinkId(StateMachineNode.NodeId, LinkU, TransitionIdx) &&
					TransitionIdx >= 0 && TransitionIdx < static_cast<int32>(StateMachineNode.Transitions.size()))
				{
					StateMachineNode.Transitions.erase(StateMachineNode.Transitions.begin() + TransitionIdx);
					Asset.BumpVersion();
		CommitGraphEdit(&Asset);
				}
				else if (DecodeEntryIndexFromLinkId(StateMachineNode.NodeId, LinkU, EntryIdx))
				{
					StateMachineNode.InitialStateName = FName::None;
					Asset.BumpVersion();
		CommitGraphEdit(&Asset);
				}
			}
		}

		ed::NodeId DeletedNode;
		while (ed::QueryDeletedNode(&DeletedNode))
		{
			int32 StateIdx = -1;
			if (DecodeStateIndexFromNodeId(StateMachineNode.NodeId, NodeIdToU32(DeletedNode), StateIdx))
			{
				if (ed::AcceptDeletedItem())
				{
					RemoveStateAndCascade(StateMachineNode, StateIdx);
					Asset.BumpVersion();
		CommitGraphEdit(&Asset);
					bStateMachinePositionsPushed = false;
				}
			}
			else
			{
				// Entry 는 pseudo node 이므로 삭제하지 않지만, delete queue 는 consume 해야 한다.
				ed::AcceptDeletedItem();
			}
		}
	}
	ed::EndDelete();

	// Context menus.
	ed::NodeId ContextNodeId = 0;
	ed::LinkId ContextLinkId = 0;
	ed::Suspend();
	if (ed::ShowNodeContextMenu(&ContextNodeId))
	{
		ImGui::OpenPopup("StateMachineNodeMenu");
	}
	else if (ed::ShowLinkContextMenu(&ContextLinkId))
	{
		ImGui::OpenPopup("StateMachineLinkMenu");
	}
	else if (ed::ShowBackgroundContextMenu())
	{
		ImGui::OpenPopup("StateMachineBackgroundMenu");
	}

	if (ImGui::BeginPopup("StateMachineNodeMenu"))
	{
		int32 StateIdx = -1;
		const bool bIsState = DecodeStateIndexFromNodeId(StateMachineNode.NodeId, NodeIdToU32(ContextNodeId), StateIdx);
		if (bIsState && StateIdx >= 0 && StateIdx < static_cast<int32>(StateMachineNode.States.size()))
		{
			if (ImGui::MenuItem("Open State Graph"))
			{
				ViewMode = EViewMode::StatePose;
				OpenStateMachineNodeId = StateMachineNode.NodeId;
				OpenStateIndex = StateIdx;
				OpenTransitionIndex = -1;
			}
			if (ImGui::MenuItem("Set as Entry State"))
			{
				StateMachineNode.InitialStateName = StateMachineNode.States[StateIdx].StateName;
				Asset.BumpVersion();
				CommitGraphEdit(&Asset);
			}
			if (ImGui::MenuItem("Delete State"))
			{
				RemoveStateAndCascade(StateMachineNode, StateIdx);
				Asset.BumpVersion();
				CommitGraphEdit(&Asset);
				bStateMachinePositionsPushed = false;
			}
		}
		else
		{
			ImGui::TextDisabled("Entry node");
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("StateMachineLinkMenu"))
	{
		int32 TransitionIdx = -1;
		int32 EntryIdx = -1;
		const uint32 LinkU = LinkIdToU32(ContextLinkId);
		if (DecodeTransitionIndexFromLinkId(StateMachineNode.NodeId, LinkU, TransitionIdx) &&
			TransitionIdx >= 0 && TransitionIdx < static_cast<int32>(StateMachineNode.Transitions.size()))
		{
			if (ImGui::MenuItem("Open Transition Rule Graph"))
			{
				ViewMode = EViewMode::TransitionRule;
				OpenStateMachineNodeId = StateMachineNode.NodeId;
				OpenStateIndex = -1;
				OpenTransitionIndex = TransitionIdx;
			}
			const FAnimGraphTransition& T = StateMachineNode.Transitions[TransitionIdx];
			const bool bCanCreateReverse = T.FromStateName != FName::None &&
				!HasTransition(StateMachineNode, T.ToStateName, T.FromStateName);
			if (ImGui::MenuItem("Create Reverse Transition", nullptr, false, bCanCreateReverse))
			{
				if (AddReverseTransitionFrom(StateMachineNode, TransitionIdx))
				{
					Asset.BumpVersion();
					CommitGraphEdit(&Asset);
				}
			}
			if (ImGui::MenuItem("Delete Transition"))
			{
				StateMachineNode.Transitions.erase(StateMachineNode.Transitions.begin() + TransitionIdx);
				Asset.BumpVersion();
				CommitGraphEdit(&Asset);
			}
		}
		else if (DecodeEntryIndexFromLinkId(StateMachineNode.NodeId, LinkU, EntryIdx))
		{
			if (ImGui::MenuItem("Clear Entry"))
			{
				StateMachineNode.InitialStateName = FName::None;
				Asset.BumpVersion();
				CommitGraphEdit(&Asset);
			}
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("StateMachineBackgroundMenu"))
	{
		if (ImGui::MenuItem("Add State"))
		{
			FAnimGraphState S;
			S.StateName = MakeUniqueStateName(StateMachineNode.States, "State");
			StateMachineNode.States.push_back(std::move(S));
			if (StateMachineNode.InitialStateName == FName::None)
			{
				StateMachineNode.InitialStateName = StateMachineNode.States.back().StateName;
			}
			Asset.BumpVersion();
			CommitGraphEdit(&Asset);
			bStateMachinePositionsPushed = false;
		}
		if (ImGui::MenuItem("Make Existing Transitions Bidirectional"))
		{
			if (AddReverseTransitionsForAllStatePairs(StateMachineNode))
			{
				Asset.BumpVersion();
				CommitGraphEdit(&Asset);
			}
		}
		ImGui::EndPopup();
	}
	ed::Resume();

	// Selection → inspector 대상 계산.
	{
		ed::NodeId SelNodes[8];
		const int NodeSelCount = ed::GetSelectedNodes(SelNodes, 8);
		if (NodeSelCount > 0)
		{
			if (NodeIdToU32(SelNodes[0]) == MakeEntryNodeId(StateMachineNode.NodeId))
			{
				bSelectedEntry = true;
			}
			else
			{
				DecodeStateIndexFromNodeId(StateMachineNode.NodeId, NodeIdToU32(SelNodes[0]), SelectedStateIndex);
			}
		}

		ed::LinkId SelLinks[8];
		const int LinkSelCount = ed::GetSelectedLinks(SelLinks, 8);
		if (LinkSelCount > 0)
		{
			DecodeTransitionIndexFromLinkId(StateMachineNode.NodeId, LinkIdToU32(SelLinks[0]), SelectedTransitionIndex);
			SelectedStateIndex = -1;
			bSelectedEntry = false;
		}
	}

	ed::End();
	ed::PopStyleColor(StateMachineStyleColors);
	ed::SetCurrentEditor(nullptr);
	ImGui::EndChild();

	if (CanvasWidth < TotalWidth)
	{
		ImGui::SameLine();
		ImGui::BeginChild("##StateMachineInspector", ImVec2(0, AvailableHeight), ImGuiChildFlags_Borders);

		bool bChanged = false;
		if (SelectedStateIndex >= 0 && SelectedStateIndex < static_cast<int32>(StateMachineNode.States.size()))
		{
			ImGui::TextColored(NodeHeaderColor(EAnimGraphNodeType::StateMachine), "Selected State #%d", SelectedStateIndex + 1);
			ImGui::Separator();

			if (ImGui::Button("Open State Graph", ImVec2(-1.0f, 0.0f)))
			{
				ViewMode = EViewMode::StatePose;
				OpenStateMachineNodeId = StateMachineNode.NodeId;
				OpenStateIndex = SelectedStateIndex;
				OpenTransitionIndex = -1;
			}
			ImGui::Separator();
			const FName OldName = StateMachineNode.States[SelectedStateIndex].StateName;
			if (RenderStateRow(StateMachineNode.States[SelectedStateIndex], SelectedStateIndex, StateMachineNode.NodeId, Asset.GetNodes()))
			{
				RenameStateAndCascade(StateMachineNode, OldName, StateMachineNode.States[SelectedStateIndex].StateName);
				bChanged = true;
			}
			ImGui::Spacing();
			if (ImGui::Button("Set as Entry State"))
			{
				StateMachineNode.InitialStateName = StateMachineNode.States[SelectedStateIndex].StateName;
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete State"))
			{
				RemoveStateAndCascade(StateMachineNode, SelectedStateIndex);
				bChanged = true;
				bStateMachinePositionsPushed = false;
			}
			if (SelectedStateIndex >= 0 && SelectedStateIndex < static_cast<int32>(StateMachineNode.States.size()))
			{
				if (ImGui::Button("Convert To Nested State Machine", ImVec2(-1.0f, 0.0f)))
				{
					// AddNodeOfType can reallocate Asset.GetNodes(). Capture stable ids/names first,
					// then re-resolve the parent StateMachine before writing SubGraphNodeId.
					const uint32 ParentNodeId = StateMachineNode.NodeId;
					const float ParentPosX = StateMachineNode.PosX;
					const float ParentPosY = StateMachineNode.PosY;
					const FName TargetStateName = StateMachineNode.States[SelectedStateIndex].StateName;
					const FString NestedName = TargetStateName.ToString() + "_SM";
					FAnimGraphNode* Nested = Asset.AddNodeOfType(EAnimGraphNodeType::StateMachine, ParentPosX + 320.0f, ParentPosY + 220.0f);
					if (Nested)
					{
						Nested->DisplayName = FName(NestedName.c_str());
						FAnimGraphState Inner;
						Inner.StateName = FName("EntryState");
						Nested->States.push_back(Inner);
						Nested->InitialStateName = Inner.StateName;
						const uint32 NestedNodeId = Nested->NodeId;

						FAnimGraphNode* Parent = Asset.FindNode(ParentNodeId);
						const int32 PatchStateIndex = Parent ? FindStateIndexByName(Parent->States, TargetStateName) : -1;
						if (Parent && PatchStateIndex >= 0)
						{
							Parent->States[PatchStateIndex].SubGraphNodeId = NestedNodeId;
						}
						OpenStateMachineNodeId = NestedNodeId;
						OpenStateIndex = -1;
						OpenTransitionIndex = -1;
						ViewMode = EViewMode::StateMachine;
						bStateMachinePositionsPushed = false;
						bChanged = true;
					}
				}
			}
		}
		else if (SelectedTransitionIndex >= 0 && SelectedTransitionIndex < static_cast<int32>(StateMachineNode.Transitions.size()))
		{
			ImGui::TextColored(ImVec4(0.70f, 0.50f, 1.00f, 1.0f), "Selected Transition #%d", SelectedTransitionIndex + 1);
			TextClippedWithTooltip(FormatTransitionSummary(StateMachineNode.Transitions[SelectedTransitionIndex]), ImGui::GetContentRegionAvail().x - 8.0f);
			ImGui::Separator();
			if (ImGui::Button("Open Transition Rule Graph", ImVec2(-1.0f, 0.0f)))
			{
				ViewMode = EViewMode::TransitionRule;
				OpenStateMachineNodeId = StateMachineNode.NodeId;
				OpenStateIndex = -1;
				OpenTransitionIndex = SelectedTransitionIndex;
			}
			ImGui::Separator();
			if (RenderTransitionRow(StateMachineNode.Transitions[SelectedTransitionIndex], StateMachineNode.States, &Asset,
				UClass::FindByName(Asset.GetOwnerClassName().c_str()), SelectedTransitionIndex))
			{
				bChanged = true;
			}
			ImGui::Spacing();
			FAnimGraphTransition& SelectedT = StateMachineNode.Transitions[SelectedTransitionIndex];
			const bool bCanCreateReverse = SelectedT.FromStateName != FName::None &&
				!HasTransition(StateMachineNode, SelectedT.ToStateName, SelectedT.FromStateName);
			if (!bCanCreateReverse && SelectedT.FromStateName != FName::None)
			{
				ImGui::TextDisabled("Reverse transition already exists.");
			}
			if (ImGui::Button("Create Reverse Transition", ImVec2(-1.0f, 0.0f)))
			{
				if (bCanCreateReverse && AddReverseTransitionFrom(StateMachineNode, SelectedTransitionIndex))
				{
					bChanged = true;
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Adds the missing opposite arrow. Float/bool rules are inverted automatically; time-based rules start disabled so you can author them explicitly.");
			}
			if (ImGui::Button("Delete Transition"))
			{
				StateMachineNode.Transitions.erase(StateMachineNode.Transitions.begin() + SelectedTransitionIndex);
				bChanged = true;
			}
		}
		else if (bSelectedEntry)
		{
			ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.25f, 1.0f), "Entry");
			ImGui::Separator();
			ImGui::TextWrapped("Entry decides which state starts when this state machine becomes relevant.");
			ImGui::TextUnformatted("Initial State");
			if (StateNameCombo("##EntryInitial", StateMachineNode.States, StateMachineNode.InitialStateName, /*bAllowAny*/false))
			{
				bChanged = true;
			}
		}
		else
		{
			ImGui::TextColored(NodeHeaderColor(EAnimGraphNodeType::StateMachine), "State Machine Overview");
			ImGui::Separator();
			if (ImGui::CollapsingHeader("역할", ImGuiTreeNodeFlags_None))
			{
				ImGui::BulletText("Entry: 시작 State 지정");
				ImGui::BulletText("State: 실제 포즈 출력");
				ImGui::BulletText("Transition: 단방향 이동");
				ImGui::BulletText("Rule: 이동 조건");
			}
			if (ImGui::Button("Add Missing Reverse Links", ImVec2(-1.0f, 0.0f)))
			{
				if (AddReverseTransitionsForAllStatePairs(StateMachineNode))
				{
					bChanged = true;
				}
			}
			ImGui::Spacing();
			ImGui::Text("Entry State: %s", StateMachineNode.InitialStateName == FName::None
				? "(none)" : StateMachineNode.InitialStateName.ToString().c_str());
			ImGui::Text("States: %zu", StateMachineNode.States.size());
			ImGui::Text("Transitions: %zu", StateMachineNode.Transitions.size());

			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Transitions", ImGuiTreeNodeFlags_None))
			{
				for (int32 i = 0; i < static_cast<int32>(StateMachineNode.Transitions.size()); ++i)
				{
					const FAnimGraphTransition& T = StateMachineNode.Transitions[i];
					ImGui::PushID(i);
					const FString FromLabel = T.FromStateName == FName::None ? FString("Any State") : T.FromStateName.ToString();
					TextClippedWithTooltip(FromLabel + " -> " + T.ToStateName.ToString(), ImGui::GetContentRegionAvail().x - 8.0f);
					TextClippedWithTooltip(FormatTransitionSummary(T), ImGui::GetContentRegionAvail().x - 8.0f);
					if (T.FromStateName != FName::None && !HasTransition(StateMachineNode, T.ToStateName, T.FromStateName))
					{
						ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "No reverse transition");
					}
					ImGui::Separator();
					ImGui::PopID();
				}
			}
		}

		if (bChanged)
		{
			Asset.BumpVersion();
			CommitGraphEdit(&Asset);
		}
		ImGui::EndChild();
	}
}

void FAnimGraphEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	if (!IsOpen() || !EditedObject || !NodeEditorContext)
	{
		return;
	}

	UAnimGraphAsset* Asset = static_cast<UAnimGraphAsset*>(EditedObject);

	// 자산별 윈도우 고유 ID — 동시 다중 인스턴스 대비.
	char WindowTitle[128];
	std::snprintf(WindowTitle, sizeof(WindowTitle),
		"AnimGraph Editor##%p", static_cast<const void*>(Asset));

	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	bool bOpenFlag = true;
	if (!bRenderingDocument && !ImGui::Begin(WindowTitle, &bOpenFlag))
	{
		ImGui::End();
		if (!bOpenFlag) Close();
		return;
	}

	// Toolbar — transient 자산(SourcePath empty)은 Save 비활성. ContentBrowser 에서 만든 자산만 저장.
	{
		const bool bHasPath = !Asset->GetSourcePath().empty();
		if (!bHasPath) ImGui::BeginDisabled();
		if (ImGui::Button("Save"))
		{
			FAnimGraphManager::Get().Save(Asset);
			ClearDirty();
		}
		if (!bHasPath) ImGui::EndDisabled();
		ImGui::SameLine();
		if (bHasPath)
		{
			ImGui::TextDisabled("%s", Asset->GetSourcePath().c_str());
		}
		else
		{
			ImGui::TextDisabled("(transient — Save 불가. ContentBrowser 에서 생성하세요)");
		}

		// OwnerClass dropdown — VariableGet 노드 inspector 의 변수 dropdown 이 이 클래스의
		// UPROPERTY 만 보여줌. UAnimInstance 자손만 list.
		ImGui::SameLine();
		ImGui::TextUnformatted("Owner:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::BeginCombo("##OwnerClass", Asset->GetOwnerClassName().c_str()))
		{
			UClass* AnimInstanceCls = UClass::FindByName("UAnimInstance");
			for (UClass* C : UClass::GetAllClasses())
			{
				if (!C || !AnimInstanceCls || !C->IsA(AnimInstanceCls)) continue;
				const bool bSelected = (Asset->GetOwnerClassName() == C->GetName());
				if (ImGui::Selectable(C->GetName(), bSelected))
				{
					Asset->SetOwnerClassName(C->GetName());
					Asset->BumpVersion();
					CommitGraphEdit(Asset);
				}
				if (bSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Compile / Validate"))
		{
			ValidateGraph(Asset);
		}
		ImGui::SameLine();
		if (UndoStack.size() <= 1) ImGui::BeginDisabled();
		if (ImGui::Button("Undo")) UndoGraphEdit(Asset);
		if (UndoStack.size() <= 1) ImGui::EndDisabled();
		ImGui::SameLine();
		if (RedoStack.empty()) ImGui::BeginDisabled();
		if (ImGui::Button("Redo")) RedoGraphEdit(Asset);
		if (RedoStack.empty()) ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextColored(bLastValidationOk ? ImVec4(0.45f, 0.90f, 0.45f, 1.0f) : ImVec4(1.0f, 0.42f, 0.35f, 1.0f),
			"%s", bLastValidationOk ? "Compiled" : "Needs Fix");

		ImGui::Separator();
	}

	// UE-style drill-in views: Root AnimGraph -> State Machine -> State / Transition Rule.
	if (ViewMode != EViewMode::RootAnimGraph && OpenStateMachineNodeId != 0)
	{
		FAnimGraphNode* OpenNode = Asset->FindNode(OpenStateMachineNodeId);
		if (!OpenNode || OpenNode->Type != EAnimGraphNodeType::StateMachine)
		{
			OpenStateMachineNodeId = 0;
			OpenStateIndex = -1;
			OpenTransitionIndex = -1;
			ViewMode = EViewMode::RootAnimGraph;
			bStateMachinePositionsPushed = false;
		}
		else
		{
			const float ViewHeight = ImGui::GetContentRegionAvail().y;
			if (ViewMode == EViewMode::StatePose)
			{
				RenderStatePoseEditor(*Asset, *OpenNode, OpenStateIndex, ViewHeight);
			}
			else if (ViewMode == EViewMode::TransitionRule)
			{
				RenderTransitionRuleEditor(*Asset, *OpenNode, OpenTransitionIndex, ViewHeight);
			}
			else
			{
				RenderStateMachineEditor(*Asset, *OpenNode, ViewHeight);
			}
			if (!bRenderingDocument)
			{
				ImGui::End();
			}
			if (!bOpenFlag) Close();
			return;
		}
	}

	// ── 좌(My Blueprint) / 중앙(canvas) / 우(inspector) split ──
	constexpr float NavigatorWidth = 270.0f;
	constexpr float InspectorWidth = 380.0f;
	const float Spacing            = ImGui::GetStyle().ItemSpacing.x;
	const float TotalWidth         = ImGui::GetContentRegionAvail().x;
	const float AvailableHeight    = ImGui::GetContentRegionAvail().y;
	const float CanvasWidth        = (TotalWidth > NavigatorWidth + InspectorWidth + Spacing * 2.0f + 180.0f)
		? TotalWidth - NavigatorWidth - InspectorWidth - Spacing * 2.0f
		: std::max(260.0f, TotalWidth - NavigatorWidth - Spacing);

	uint32 SelectedNodeId = 0;

	RenderAnimBlueprintNavigator(*Asset, 0, -1, -1, AvailableHeight);
	ImGui::SameLine();
	ImGui::BeginChild("##AnimGraphCanvasChild", ImVec2(CanvasWidth, 0), ImGuiChildFlags_None);

	ed::SetCurrentEditor(NodeEditorContext);
	const int AnimGraphStyleColors = PushUnrealAnimGraphCanvasStyle();
	ed::Begin("AnimGraphCanvas");
	QueueRootGraphShortcuts(Asset);

	// 첫 프레임에 데이터 모델 좌표를 ed 컨텍스트로 push (1회). 이후 매 프레임 GetNodePosition
	// 으로 pull 해 모델에 반영 — 단방향 (model → ed) 1회 + (ed → model) 매 프레임.
	if (!bPositionsPushed)
	{
		for (const FAnimGraphNode& Node : Asset->GetNodes())
		{
			ed::SetNodePosition(ToNodeId(Node.NodeId), ImVec2(Node.PosX, Node.PosY));
		}
		bPositionsPushed = true;
	}

	uint32 PendingOpenStateMachineFromNode = 0;
	for (const FAnimGraphNode& Node : Asset->GetNodes())
	{
		const ImVec4 Header = NodeHeaderColor(Node.Type);
		ed::PushStyleColor(ed::StyleColor_NodeBorder,
			ImColor(Header.x, Header.y, Header.z, Node.Type == EAnimGraphNodeType::StateMachine ? 0.92f : 0.68f));
		ed::BeginNode(ToNodeId(Node.NodeId));
		if (RenderAnimGraphNodeCard(*Asset, Node))
		{
			PendingOpenStateMachineFromNode = Node.NodeId;
		}
		ed::EndNode();
		ed::PopStyleColor(1);
	}

	for (const FAnimGraphLink& Link : Asset->GetLinks())
	{
		const EAnimGraphPinType LinkType = ResolveLinkType(*Asset, Link);
		const ImVec4 LinkColor = PinTypeColor(LinkType);
		const float LinkThickness = (LinkType == EAnimGraphPinType::Pose) ? 4.0f : 2.0f;
		ed::Link(ToLinkId(Link.LinkId), ToPinId(Link.FromPinId), ToPinId(Link.ToPinId), LinkColor, LinkThickness);
	}

	// ── 핀 드래그로 링크 생성 ──
	if (ed::BeginCreate())
	{
		ed::PinId StartId, EndId;
		if (ed::QueryNewLink(&StartId, &EndId))
		{
			if (StartId && EndId)
			{
				uint32 FromU = 0, ToU = 0;
				const bool bOk = Asset->CanLinkPins(PinIdToU32(StartId), PinIdToU32(EndId), &FromU, &ToU);
				if (bOk)
				{
					if (ed::AcceptNewItem())
					{
						Asset->AddLink(FromU, ToU);
						CommitGraphEdit(Asset);
					}
				}
				else
				{
					ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
				}
			}
		}

		ed::PinId NewNodePinId = 0;
		if (ed::QueryNewNode(&NewNodePinId))
		{
			if (NewNodePinId && ed::AcceptNewItem(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), 1.5f))
			{
				PendingPinSpawnPinId = PinIdToU32(NewNodePinId);
				PendingNewNodeScreenPosition = ImGui::GetMousePos();
				PinSpawnSearchBuf[0] = 0;
				bShowPinSpawnMenu = true;
			}
		}
	}
	ed::EndCreate();

	// ── Delete 키 / 메뉴 → BeginDelete 큐 ──
	if (ed::BeginDelete())
	{
		bool bDeletedSomething = false;
		ed::LinkId DeletedLink;
		while (ed::QueryDeletedLink(&DeletedLink))
		{
			if (ed::AcceptDeletedItem())
			{
				bDeletedSomething |= Asset->RemoveLink(LinkIdToU32(DeletedLink));
			}
		}

		ed::NodeId DeletedNode;
		while (ed::QueryDeletedNode(&DeletedNode))
		{
			const uint32 NodeId = NodeIdToU32(DeletedNode);
			if (IsOutputPoseNode(*Asset, NodeId))
			{
				continue;
			}
			if (ed::AcceptDeletedItem())
			{
				bDeletedSomething |= Asset->RemoveNode(NodeId);
			}
		}
		if (bDeletedSomething) CommitGraphEdit(Asset);
	}
	ed::EndDelete();

	// ── 위치 동기화 (ed → model) ──
	for (FAnimGraphNode& Node : const_cast<TArray<FAnimGraphNode>&>(Asset->GetNodes()))
	{
		const ImVec2 P = ed::GetNodePosition(ToNodeId(Node.NodeId));
		if (std::fabs(Node.PosX - P.x) > 0.01f || std::fabs(Node.PosY - P.y) > 0.01f)
		{
			Node.PosX = P.x;
			Node.PosY = P.y;
			MarkDirty();
		}
	}

	// ── 컨텍스트 메뉴 ──
	ed::NodeId   ContextNodeId   = 0;
	ed::PinId    ContextPinId    = 0;
	ed::LinkId   ContextLinkId   = 0;

	ed::Suspend();
	if (ed::ShowNodeContextMenu(&ContextNodeId))
	{
		ImGui::OpenPopup("AnimGraphNodeMenu");
	}
	else if (ed::ShowPinContextMenu(&ContextPinId))
	{
		ImGui::OpenPopup("AnimGraphPinMenu");
	}
	else if (ed::ShowLinkContextMenu(&ContextLinkId))
	{
		ImGui::OpenPopup("AnimGraphLinkMenu");
	}
	else if (ed::ShowBackgroundContextMenu())
	{
		PendingNewNodeScreenPosition = ImGui::GetMousePos();
		PendingNewNodePosition = ed::ScreenToCanvas(PendingNewNodeScreenPosition);
		AddNodeSearchBuf[0] = 0;
		ImGui::OpenPopup("AnimGraphBackgroundMenu");
	}

	if (ImGui::BeginPopup("AnimGraphNodeMenu"))
	{
		FAnimGraphNode* ContextNode = Asset->FindNode(NodeIdToU32(ContextNodeId));
		if (ContextNode && ContextNode->Type == EAnimGraphNodeType::StateMachine)
		{
			if (ImGui::MenuItem("Open State Machine"))
			{
				OpenStateMachineNodeId = ContextNode->NodeId;
				OpenStateIndex = -1;
				OpenTransitionIndex = -1;
				ViewMode = EViewMode::StateMachine;
				bStateMachinePositionsPushed = false;
			}
			ImGui::Separator();
		}
		if (ImGui::MenuItem("Copy")) bQueuedCopySelected = true;
		if (ImGui::MenuItem("Duplicate")) bQueuedDuplicateSelected = true;
		ImGui::Separator();
		const bool bContextIsOutput = ContextNode && ContextNode->Type == EAnimGraphNodeType::OutputPose;
		if (bContextIsOutput)
		{
			ImGui::BeginDisabled();
			ImGui::MenuItem("Delete");
			ImGui::EndDisabled();
			ImGui::TextDisabled("Output Pose is the graph result and cannot be deleted.");
		}
		else if (ImGui::MenuItem("Delete"))
		{
			if (Asset->RemoveNode(NodeIdToU32(ContextNodeId))) CommitGraphEdit(Asset);
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("AnimGraphLinkMenu"))
	{
		if (ImGui::MenuItem("Break Link"))
		{
			if (Asset->RemoveLink(LinkIdToU32(ContextLinkId))) CommitGraphEdit(Asset);
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("AnimGraphPinMenu"))
	{
		ImGui::TextDisabled("%s", DescribePin(*Asset, PinIdToU32(ContextPinId)).c_str());
		if (ImGui::MenuItem("Break Link(s)"))
		{
			if (RemoveLinksForPin(*Asset, PinIdToU32(ContextPinId))) CommitGraphEdit(Asset);
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("AnimGraphBackgroundMenu"))
	{
		// UE Blueprint 의 우클릭 팔레트처럼 검색 + 카테고리 기반으로 노드를 고르게 만든다.
		bool bAddedNodeFromMenu = false;
		ImGui::TextUnformatted("All Actions for this Anim Graph");
		ImGui::SetNextItemWidth(340.0f);
		ImGui::InputText("##AnimGraphActionSearch", AddNodeSearchBuf, sizeof(AddNodeSearchBuf));
		ImGui::Separator();

		ImGui::BeginChild("##AnimGraphActionList", ImVec2(350.0f, 310.0f), ImGuiChildFlags_Borders);
		if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bAddedNodeFromMenu |= RenderAddNodeAction(EAnimGraphNodeType::SequencePlayer, *Asset, PendingNewNodePosition, AddNodeSearchBuf, "Animation Sequence Player Pose");
			bAddedNodeFromMenu |= RenderAddNodeAction(EAnimGraphNodeType::RefPose,        *Asset, PendingNewNodePosition, AddNodeSearchBuf, "Animation Reference Pose");
			bAddedNodeFromMenu |= RenderAddNodeAction(EAnimGraphNodeType::Slot,           *Asset, PendingNewNodePosition, AddNodeSearchBuf, "Animation Montage Slot Pose");
		}
		if (ImGui::CollapsingHeader("Blends", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bAddedNodeFromMenu |= RenderAddNodeAction(EAnimGraphNodeType::LayeredBlendPerBone, *Asset, PendingNewNodePosition, AddNodeSearchBuf, "Blend Layered Per Bone Pose");
			bAddedNodeFromMenu |= RenderAddNodeAction(EAnimGraphNodeType::BlendListByEnum,     *Asset, PendingNewNodePosition, AddNodeSearchBuf, "Blend List Enum Selector Pose");
		}
		if (ImGui::CollapsingHeader("State Machines", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bAddedNodeFromMenu |= RenderAddNodeAction(EAnimGraphNodeType::StateMachine, *Asset, PendingNewNodePosition, AddNodeSearchBuf, "State Machine Locomotion Pose");
			const FString TransitionRuleSearch = "Transition Rule Condition Bool Property Float Compare Time Remaining Time Remaining Ratio Current State Time Automatic Sequence End";
			if (StringContainsInsensitive(TransitionRuleSearch, AddNodeSearchBuf))
			{
				ImGui::Spacing();
				ImGui::TextDisabled("Transition Rule Graph");
				ImGui::TextWrapped("Open a State Machine, select a transition link, then use Edit Transition Rule / Add Rule Node.");
			}
		}
		if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const FAnimGraphVariable& Var : Asset->GetVariables())
			{
				bAddedNodeFromMenu |= RenderGetVariableAction(*Asset, Var, PendingNewNodePosition, AddNodeSearchBuf);
			}
			bAddedNodeFromMenu |= RenderAddNodeAction(EAnimGraphNodeType::VariableGet, *Asset, PendingNewNodePosition, AddNodeSearchBuf, "Variable Property Access Data Float");
		}
		if (ImGui::CollapsingHeader("Output", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bAddedNodeFromMenu |= RenderAddNodeAction(EAnimGraphNodeType::OutputPose, *Asset, PendingNewNodePosition, AddNodeSearchBuf, "Output Pose Result Root");
		}
		ImGui::EndChild();

		if (bAddedNodeFromMenu) CommitGraphEdit(Asset);
		ImGui::TextDisabled("Drag pose pins to build the final Output Pose.");
		ImGui::EndPopup();
	}
	if (bShowPinSpawnMenu)
	{
		ImGui::OpenPopup("AnimGraphPinSpawnPopup");
		bShowPinSpawnMenu = false;
	}
	if (ImGui::BeginPopup("AnimGraphPinSpawnPopup"))
	{
		RenderPinSpawnMenu(Asset);
		ImGui::EndPopup();
	}

	ed::Resume();

	ProcessQueuedRootGraphCommands(Asset);

	// ed::End 직전에 선택된 노드 캡쳐 (inspector pane 이 ed 컨텍스트 외부에서 참조).
	{
		ed::NodeId SelBuf[4];
		const int SelCount = ed::GetSelectedNodes(SelBuf, 4);
		if (SelCount > 0) SelectedNodeId = NodeIdToU32(SelBuf[0]);
	}

	if (PendingOpenStateMachineFromNode != 0)
	{
		OpenStateMachineNodeId = PendingOpenStateMachineFromNode;
		OpenStateIndex = -1;
		OpenTransitionIndex = -1;
		ViewMode = EViewMode::StateMachine;
		bStateMachinePositionsPushed = false;
	}

	ed::End();
	ed::PopStyleColor(AnimGraphStyleColors);
	ed::SetCurrentEditor(nullptr);

	ImGui::EndChild();

	// ── 우측 inspector pane ──
	if (CanvasWidth < TotalWidth)
	{
		ImGui::SameLine();
		ImGui::BeginChild("##AnimGraphInspector", ImVec2(0, 0), ImGuiChildFlags_Borders);

		if (SelectedNodeId != 0)
		{
			if (FAnimGraphNode* SelNode = Asset->FindNode(SelectedNodeId))
			{
				if (SelNode->Type == EAnimGraphNodeType::StateMachine)
				{
					if (ImGui::Button("Open State Machine Graph", ImVec2(-1.0f, 0.0f)))
					{
						OpenStateMachineNodeId = SelNode->NodeId;
						OpenStateIndex = -1;
						OpenTransitionIndex = -1;
						ViewMode = EViewMode::StateMachine;
						bStateMachinePositionsPushed = false;
					}
					ImGui::Separator();
				}
				RenderNodeInspector(*Asset, *SelNode);
			}
			else
			{
				ImGui::TextDisabled("(stale selection)");
			}
		}
		else
		{
			ImGui::TextDisabled("Select a node to edit properties.");
		}

		ImGui::Separator();
		if (RenderGraphVariablesPanel(*Asset))
		{
			CommitGraphEdit(Asset);
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Compiler Results", ImGuiTreeNodeFlags_None))
		{
			for (const FString& Message : LastValidationMessages)
			{
				const bool bError = Message.find("Error:") == 0;
				ImGui::TextColored(bError ? ImVec4(1.0f, 0.42f, 0.35f, 1.0f) : ImVec4(0.92f, 0.78f, 0.35f, 1.0f),
					"%s", Message.c_str());
			}
		}
		ImGui::EndChild();
	}

	if (!bRenderingDocument)
	{
		ImGui::End();
	}

	if (!bOpenFlag) Close();
}

void FAnimGraphEditorWidget::RenderDocument(float DeltaTime)
{
	bRenderingDocument = true;
	Render(DeltaTime);
	bRenderingDocument = false;
}

FString FAnimGraphEditorWidget::GetDocumentTitle() const
{
	const UAnimGraphAsset* Asset = Cast<UAnimGraphAsset>(EditedObject);
	const FString SourcePath = Asset ? Asset->GetSourcePath() : FString();
	return SourcePath.empty() ? FString("Anim Graph") : FString("Anim Graph - " + SourcePath);
}

FString FAnimGraphEditorWidget::GetDocumentPayloadId() const
{
	const UAnimGraphAsset* Asset = Cast<UAnimGraphAsset>(EditedObject);
	const FString SourcePath = Asset ? Asset->GetSourcePath() : FString();
	return SourcePath.empty() ? FAssetEditorWidget::GetDocumentPayloadId() : SourcePath;
}
