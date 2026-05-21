#include "EditorViewerWindowWidget.h"
#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorChromeConstants.h"
#include "Editor/Viewer/AnimStateMachineGraphViewer.h"
#include "Editor/Viewer/AnimationViewer.h"
#include "Editor/Viewer/EditorViewer.h"
#include "Editor/Viewer/FSkeletalMeshViewer.h"
#include "Animation/AnimStateMachineAsset.h"
#include "Animation/AnimationSequenceBase.h"
#include "Asset/AnimationSequence.h"
#include "Engine/Core/EditorResourcePaths.h"
#include "Viewport/ViewportLayout.h"
#include "GameFramework/PrimitiveActors.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/ResourceManager.h"
#include "Core/StringUtils.h"
#include "Component/GizmoComponent.h"
#include "Component/TransformProxy.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "WICTextureLoader.h"
#include "imgui-node-editor/imgui_node_editor.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <functional>

namespace
{
namespace ed = ax::NodeEditor;

void SetOpaqueBlendStateCallback(const ImDrawList*, const ImDrawCmd* Cmd)
{
    ID3D11DeviceContext* DeviceContext = static_cast<ID3D11DeviceContext*>(Cmd->UserCallbackData);
    if (!DeviceContext)
        return;

    const float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
    DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xffffffff);
}

bool UsesAbsoluteImGuiCoordinates()
{
    return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
}

POINT ImGuiScreenToClientPoint(FWindowsWindow* Window, const ImVec2& Point)
{
    POINT Result =
    {
        static_cast<LONG>(std::lround(Point.x)),
        static_cast<LONG>(std::lround(Point.y))
    };
    if (Window && Window->GetHWND() && UsesAbsoluteImGuiCoordinates())
    {
        ::ScreenToClient(Window->GetHWND(), &Result);
    }
    return Result;
}

FString GetBaseFileNameWithoutExtension(const FString& Path)
{
    if (Path.empty())
    {
        return "Viewer";
    }

    const size_t SlashPos = Path.find_last_of("/\\");
    const size_t NameBegin = SlashPos == FString::npos ? 0 : SlashPos + 1;
    FString Name = Path.substr(NameBegin);

    const size_t DotPos = Name.find_last_of('.');
    if (DotPos != FString::npos && DotPos > 0)
    {
        Name = Name.substr(0, DotPos);
    }

    return Name.empty() ? "Viewer" : Name;
}

FString GetViewerAssetLabel(FEditorViewer* Viewer)
{
    if (!Viewer)
    {
        return "Viewer";
    }

    const FString BaseLabel = GetBaseFileNameWithoutExtension(Viewer->GetFileName());
    if (dynamic_cast<FAnimationViewer*>(Viewer))
    {
        return "Animation: " + BaseLabel;
    }
    if (dynamic_cast<FAnimStateMachineGraphViewer*>(Viewer))
    {
        return "State Machine: " + BaseLabel;
    }
    if (dynamic_cast<FSkeletalMeshViewer*>(Viewer))
    {
        return "Skeletal Mesh: " + BaseLabel;
    }
    return BaseLabel;
}

FString ToLowerCopy(FString Value)
{
    std::transform(Value.begin(), Value.end(), Value.begin(),
        [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
    return Value;
}

bool MatchesFilter(const FString& Text, const char* Filter)
{
    if (!Filter || Filter[0] == '\0')
    {
        return true;
    }

    return ToLowerCopy(Text).find(ToLowerCopy(FString(Filter))) != FString::npos;
}

const wchar_t* GetViewerIconFileName(EEditorViewerIcon Icon)
{
    switch (Icon)
    {
    case EEditorViewerIcon::JumpToNextKey: return L"PlayControlsJumpToNextKey.png";
    case EEditorViewerIcon::JumpToPreviousKey: return L"PlayControlsJumpToPreviousKey.png";
    case EEditorViewerIcon::Looping: return L"PlayControlsLooping.png";
    case EEditorViewerIcon::LoopingSelectionRange: return L"PlayControlsLoopingSelectionRange.png";
    case EEditorViewerIcon::NoLooping: return L"PlayControlsNoLooping.png";
    case EEditorViewerIcon::Pause: return L"PlayControlsPause.png";
    case EEditorViewerIcon::PlayForward: return L"PlayControlsPlayForward.png";
    case EEditorViewerIcon::PlayReverse: return L"PlayControlsPlayReverse.png";
    case EEditorViewerIcon::Record: return L"PlayControlsRecord.png";
    case EEditorViewerIcon::SetPlaybackEnd: return L"PlayControlsSetPlaybackEnd.png";
    case EEditorViewerIcon::SetPlaybackStart: return L"PlayControlsSetPlaybackStart.png";
    case EEditorViewerIcon::Stop: return L"PlayControlsStop.png";
    case EEditorViewerIcon::ToEnd: return L"PlayControlsToEnd.png";
    case EEditorViewerIcon::ToFront: return L"PlayControlsToFront.png";
    case EEditorViewerIcon::ToNext: return L"PlayControlsToNext.png";
    case EEditorViewerIcon::ToPrevious: return L"PlayControlsToPrevious.png";
    default: return L"";
    }
}

FSkeletalMeshViewer* AsSkeletalMeshViewer(FEditorViewer* Viewer)
{
    return dynamic_cast<FSkeletalMeshViewer*>(Viewer);
}

FAnimationViewer* AsAnimationViewer(FEditorViewer* Viewer)
{
    return dynamic_cast<FAnimationViewer*>(Viewer);
}

FAnimStateMachineGraphViewer* AsAnimStateMachineGraphViewer(FEditorViewer* Viewer)
{
    return dynamic_cast<FAnimStateMachineGraphViewer*>(Viewer);
}

FSkeletalViewerShowFlags* GetViewerShowFlags(FEditorViewer* Viewer)
{
    if (FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer))
    {
        return &SkeletalViewer->GetClient().GetShowFlags();
    }

    if (FAnimationViewer* AnimationViewer = AsAnimationViewer(Viewer))
    {
        return &AnimationViewer->GetClient().GetShowFlags();
    }

    return nullptr;
}

ed::NodeId MakeStateNodeId(FAnimStateId StateId)
{
    return ed::NodeId(static_cast<uintptr_t>(StateId));
}

ImVec2 Add(const ImVec2& A, const ImVec2& B)
{
    return ImVec2(A.x + B.x, A.y + B.y);
}

ImVec2 Subtract(const ImVec2& A, const ImVec2& B)
{
    return ImVec2(A.x - B.x, A.y - B.y);
}

ImVec2 Scale(const ImVec2& Value, float Factor)
{
    return ImVec2(Value.x * Factor, Value.y * Factor);
}

float Dot(const ImVec2& A, const ImVec2& B)
{
    return A.x * B.x + A.y * B.y;
}

float LengthSquared(const ImVec2& Value)
{
    return Dot(Value, Value);
}

ImVec2 Normalize(const ImVec2& Value)
{
    const float Length = std::sqrt(LengthSquared(Value));
    if (Length <= 0.0001f)
    {
        return ImVec2(1.0f, 0.0f);
    }

    return ImVec2(Value.x / Length, Value.y / Length);
}

float DistanceToSegment(const ImVec2& Point, const ImVec2& Start, const ImVec2& End)
{
    const ImVec2 Segment = Subtract(End, Start);
    const float SegmentLengthSquared = LengthSquared(Segment);
    if (SegmentLengthSquared <= 0.0001f)
    {
        return std::sqrt(LengthSquared(Subtract(Point, Start)));
    }

    const float T = std::clamp(Dot(Subtract(Point, Start), Segment) / SegmentLengthSquared, 0.0f, 1.0f);
    const ImVec2 Projection = Add(Start, Scale(Segment, T));
    return std::sqrt(LengthSquared(Subtract(Point, Projection)));
}

ImVec2 GetRectBoundaryPoint(const ImVec2& Center, const ImVec2& HalfSize, const ImVec2& Direction)
{
    const float Tx = std::abs(Direction.x) > 0.0001f ? HalfSize.x / std::abs(Direction.x) : FLT_MAX;
    const float Ty = std::abs(Direction.y) > 0.0001f ? HalfSize.y / std::abs(Direction.y) : FLT_MAX;
    return Add(Center, Scale(Direction, std::min(Tx, Ty)));
}

constexpr float TransitionLaneSpacing = 14.0f;
constexpr float OppositeDirectionLaneOffset = 12.0f;
constexpr float TransitionHitRadiusPx = 10.0f;

struct FTransitionDrawSegment
{
    FAnimTransitionId TransitionId = InvalidAnimTransitionId;
    ed::NodeId FromNodeId;
    ImVec2 StartCanvas;
    ImVec2 EndCanvas;
    ImVec2 StartScreen;
    ImVec2 EndScreen;
};

float ComputeTransitionLaneOffset(
    const UAnimStateMachineAsset* Asset,
    const FAnimTransitionDesc& Transition,
    int32 TransitionIndex)
{
    if (!Asset)
    {
        return 0.0f;
    }

    int32 SameDirectionBefore = 0;
    int32 SameDirectionTotal = 0;
    int32 ReverseDirectionTotal = 0;
    const TArray<FAnimTransitionDesc>& Transitions = Asset->GetTransitions();
    for (int32 OtherIndex = 0; OtherIndex < static_cast<int32>(Transitions.size()); ++OtherIndex)
    {
        const FAnimTransitionDesc& Other = Transitions[OtherIndex];
        if (Other.FromStateId == Transition.FromStateId && Other.ToStateId == Transition.ToStateId)
        {
            if (OtherIndex < TransitionIndex)
            {
                ++SameDirectionBefore;
            }
            ++SameDirectionTotal;
        }
        else if (Other.FromStateId == Transition.ToStateId && Other.ToStateId == Transition.FromStateId)
        {
            ++ReverseDirectionTotal;
        }
    }

    if (SameDirectionTotal <= 1 && ReverseDirectionTotal == 0)
    {
        return 0.0f;
    }

    float LaneOffset =
        (static_cast<float>(SameDirectionBefore) - (static_cast<float>(SameDirectionTotal - 1) * 0.5f)) *
        TransitionLaneSpacing;
    if (ReverseDirectionTotal > 0)
    {
        LaneOffset += OppositeDirectionLaneOffset;
    }
    return LaneOffset;
}

bool BuildTransitionDrawSegment(
    const UAnimStateMachineAsset* Asset,
    const FAnimTransitionDesc& Transition,
    int32 TransitionIndex,
    FTransitionDrawSegment& OutSegment)
{
    const FAnimStateDesc* FromState = Asset ? Asset->FindStateById(Transition.FromStateId) : nullptr;
    const FAnimStateDesc* ToState = Asset ? Asset->FindStateById(Transition.ToStateId) : nullptr;
    if (!FromState || !ToState)
    {
        return false;
    }

    const ed::NodeId FromNodeId = MakeStateNodeId(FromState->Id);
    const ed::NodeId ToNodeId = MakeStateNodeId(ToState->Id);
    const ImVec2 FromPosition = ed::GetNodePosition(FromNodeId);
    const ImVec2 ToPosition = ed::GetNodePosition(ToNodeId);
    const ImVec2 FromSize = ed::GetNodeSize(FromNodeId);
    const ImVec2 ToSize = ed::GetNodeSize(ToNodeId);
    const ImVec2 FromCenter = Add(FromPosition, Scale(FromSize, 0.5f));
    const ImVec2 ToCenter = Add(ToPosition, Scale(ToSize, 0.5f));
    const ImVec2 Direction = Normalize(Subtract(ToCenter, FromCenter));
    const ImVec2 Normal(-Direction.y, Direction.x);
    const float LaneOffset = ComputeTransitionLaneOffset(Asset, Transition, TransitionIndex);

    ImVec2 Start = GetRectBoundaryPoint(FromCenter, Scale(FromSize, 0.5f), Direction);
    ImVec2 End = GetRectBoundaryPoint(ToCenter, Scale(ToSize, 0.5f), Scale(Direction, -1.0f));
    Start = Add(Start, Scale(Normal, LaneOffset));
    End = Add(End, Scale(Normal, LaneOffset));

    OutSegment.TransitionId = Transition.Id;
    OutSegment.FromNodeId = FromNodeId;
    OutSegment.StartCanvas = Start;
    OutSegment.EndCanvas = End;
    OutSegment.StartScreen = ed::CanvasToScreen(Start);
    OutSegment.EndScreen = ed::CanvasToScreen(End);
    return true;
}

FAnimTransitionId PickTransitionAtScreenPosition(
    const TArray<FTransitionDrawSegment>& Segments,
    const ImVec2& ScreenPosition)
{
    float BestDistance = TransitionHitRadiusPx;
    FAnimTransitionId BestTransitionId = InvalidAnimTransitionId;
    for (const FTransitionDrawSegment& Segment : Segments)
    {
        const float Distance = DistanceToSegment(ScreenPosition, Segment.StartScreen, Segment.EndScreen);
        if (Distance <= BestDistance)
        {
            BestDistance = Distance;
            BestTransitionId = Segment.TransitionId;
        }
    }
    return BestTransitionId;
}

void DrawStraightTransition(
    ImDrawList* DrawList,
    const ImVec2& Start,
    const ImVec2& End,
    ImU32 Color,
    float Thickness,
    bool bHovered,
    bool bSelected)
{
    if (!DrawList)
    {
        return;
    }

    const ImVec2 Direction = Normalize(Subtract(End, Start));
    DrawList->AddLine(Start, End, Color, Thickness);

    const float ArrowLength = bSelected ? 13.0f : 11.0f;
    const float ArrowWidth = bSelected ? 6.5f : 5.5f;
    const ImVec2 Normal(-Direction.y, Direction.x);
    const ImVec2 ArrowBase = Subtract(End, Scale(Direction, ArrowLength));
    const ImVec2 ArrowA = Add(ArrowBase, Scale(Normal, ArrowWidth));
    const ImVec2 ArrowB = Subtract(ArrowBase, Scale(Normal, ArrowWidth));
    DrawList->AddTriangleFilled(End, ArrowA, ArrowB, Color);

    if (bHovered || bSelected)
    {
        const ImU32 AccentColor = bSelected
            ? IM_COL32(255, 202, 93, 255)
            : IM_COL32(180, 205, 255, 210);
        DrawList->AddLine(Start, End, AccentColor, Thickness + 2.0f);
        DrawList->AddTriangleFilled(End, ArrowA, ArrowB, AccentColor);
    }
}

const FAnimStateEditorMetadata* FindStateEditorMetadata(
    const UAnimStateMachineAsset* Asset,
    FAnimStateId StateId)
{
    if (!Asset)
    {
        return nullptr;
    }

    for (const FAnimStateEditorMetadata& Metadata : Asset->GetStateEditorMetadata())
    {
        if (Metadata.StateId == StateId)
        {
            return &Metadata;
        }
    }

    return nullptr;
}

FString CompareOpToString(EAnimCompareOp CompareOp)
{
    switch (CompareOp)
    {
    case EAnimCompareOp::Equal: return "Equal";
    case EAnimCompareOp::NotEqual: return "NotEqual";
    case EAnimCompareOp::Greater: return "Greater";
    case EAnimCompareOp::GreaterEqual: return "GreaterEqual";
    case EAnimCompareOp::Less: return "Less";
    case EAnimCompareOp::LessEqual: return "LessEqual";
    case EAnimCompareOp::IsTrue: return "IsTrue";
    case EAnimCompareOp::IsFalse: return "IsFalse";
    default: return "Unknown";
    }
}

FString ParameterTypeToString(EAnimParameterType Type)
{
    switch (Type)
    {
    case EAnimParameterType::Bool: return "Bool";
    case EAnimParameterType::Int: return "Int";
    case EAnimParameterType::Float: return "Float";
    case EAnimParameterType::Vector: return "Vector";
    case EAnimParameterType::Trigger: return "Trigger";
    default: return "Unknown";
    }
}

FString EaseOptionToString(EAnimBlendEaseOption Option)
{
    switch (Option)
    {
    case EAnimBlendEaseOption::Linear: return "Linear";
    case EAnimBlendEaseOption::EaseIn: return "EaseIn";
    case EAnimBlendEaseOption::EaseOut: return "EaseOut";
    case EAnimBlendEaseOption::EaseInOut: return "EaseInOut";
    default: return "Unknown";
    }
}

EAnimParameterType ParameterTypeFromIndex(int32 Index)
{
    switch (Index)
    {
    case 1: return EAnimParameterType::Int;
    case 2: return EAnimParameterType::Float;
    case 3: return EAnimParameterType::Vector;
    case 4: return EAnimParameterType::Trigger;
    default: return EAnimParameterType::Bool;
    }
}

int32 ParameterTypeToIndex(EAnimParameterType Type)
{
    switch (Type)
    {
    case EAnimParameterType::Int: return 1;
    case EAnimParameterType::Float: return 2;
    case EAnimParameterType::Vector: return 3;
    case EAnimParameterType::Trigger: return 4;
    case EAnimParameterType::Bool:
    default: return 0;
    }
}

EAnimBlendEaseOption EaseOptionFromIndex(int32 Index)
{
    switch (Index)
    {
    case 1: return EAnimBlendEaseOption::EaseIn;
    case 2: return EAnimBlendEaseOption::EaseOut;
    case 3: return EAnimBlendEaseOption::EaseInOut;
    default: return EAnimBlendEaseOption::Linear;
    }
}

int32 EaseOptionToIndex(EAnimBlendEaseOption Option)
{
    switch (Option)
    {
    case EAnimBlendEaseOption::EaseIn: return 1;
    case EAnimBlendEaseOption::EaseOut: return 2;
    case EAnimBlendEaseOption::EaseInOut: return 3;
    case EAnimBlendEaseOption::Linear:
    default: return 0;
    }
}

TArray<EAnimCompareOp> GetCompareOpsForParameterType(EAnimParameterType Type)
{
    switch (Type)
    {
    case EAnimParameterType::Bool:
        return { EAnimCompareOp::Equal, EAnimCompareOp::NotEqual, EAnimCompareOp::IsTrue, EAnimCompareOp::IsFalse };
    case EAnimParameterType::Int:
    case EAnimParameterType::Float:
        return {
            EAnimCompareOp::Equal,
            EAnimCompareOp::NotEqual,
            EAnimCompareOp::Greater,
            EAnimCompareOp::GreaterEqual,
            EAnimCompareOp::Less,
            EAnimCompareOp::LessEqual
        };
    case EAnimParameterType::Vector:
        return { EAnimCompareOp::Equal, EAnimCompareOp::NotEqual };
    case EAnimParameterType::Trigger:
        return { EAnimCompareOp::IsTrue };
    default:
        return { EAnimCompareOp::Equal };
    }
}

EAnimCompareOp GetCompareOpFromIndex(EAnimParameterType Type, int32 Index)
{
    TArray<EAnimCompareOp> Ops = GetCompareOpsForParameterType(Type);
    if (Ops.empty())
    {
        return EAnimCompareOp::Equal;
    }

    const int32 ClampedIndex = std::clamp(Index, 0, static_cast<int32>(Ops.size()) - 1);
    return Ops[ClampedIndex];
}

int32 GetCompareOpIndex(EAnimParameterType Type, EAnimCompareOp CompareOp)
{
    TArray<EAnimCompareOp> Ops = GetCompareOpsForParameterType(Type);
    for (int32 Index = 0; Index < static_cast<int32>(Ops.size()); ++Index)
    {
        if (Ops[Index] == CompareOp)
        {
            return Index;
        }
    }
    return 0;
}

const FAnimStateMachineParameterDesc* GetParameterByIndex(
    const UAnimStateMachineAsset* Asset,
    int32 Index)
{
    if (!Asset || Index < 0 || Index >= static_cast<int32>(Asset->GetParameters().size()))
    {
        return nullptr;
    }

    return &Asset->GetParameters()[Index];
}

int32 FindParameterIndex(const UAnimStateMachineAsset* Asset, const FName& ParameterName)
{
    if (!Asset)
    {
        return -1;
    }

    const TArray<FAnimStateMachineParameterDesc>& Parameters = Asset->GetParameters();
    for (int32 Index = 0; Index < static_cast<int32>(Parameters.size()); ++Index)
    {
        if (Parameters[Index].Name == ParameterName)
        {
            return Index;
        }
    }

    return -1;
}

bool IsParameterReferencedByAnyTransition(const UAnimStateMachineAsset* Asset, const FName& ParameterName)
{
    if (!Asset || !ParameterName.IsValid())
    {
        return false;
    }

    for (const FAnimTransitionDesc& Transition : Asset->GetTransitions())
    {
        for (const FAnimTransitionCondition& Condition : Transition.Conditions)
        {
            if (Condition.ParameterName == ParameterName)
            {
                return true;
            }
        }
    }

    return false;
}

FAnimTransitionCondition BuildConditionFromUi(
    const FAnimStateMachineParameterDesc& Parameter,
    int32 CompareOpIndex,
    bool BoolValue,
    int32 IntValue,
    float FloatValue,
    const float VectorValue[3])
{
    FAnimTransitionCondition Condition;
    Condition.ParameterName = Parameter.Name;
    Condition.CompareOp = GetCompareOpFromIndex(Parameter.Type, CompareOpIndex);
    if (Parameter.Type == EAnimParameterType::Trigger)
    {
        Condition.CompareOp = EAnimCompareOp::IsTrue;
    }
    Condition.CompareValue.BoolValue = BoolValue;
    Condition.CompareValue.IntValue = IntValue;
    Condition.CompareValue.FloatValue = FloatValue;
    Condition.CompareValue.VectorValue = FVector(VectorValue[0], VectorValue[1], VectorValue[2]);
    return Condition;
}

FAnimTransitionCondition BuildDefaultConditionForParameter(const FAnimStateMachineParameterDesc& Parameter)
{
    float DefaultVector[3] = { 0.0f, 0.0f, 0.0f };
    return BuildConditionFromUi(Parameter, 0, true, 0, 0.0f, DefaultVector);
}

const FAnimTransitionDesc* FindTransitionBySignature(
    const UAnimStateMachineAsset* Asset,
    FAnimStateId FromStateId,
    FAnimStateId ToStateId,
    const TArray<FAnimTransitionCondition>& Conditions)
{
    if (!Asset)
    {
        return nullptr;
    }

    for (const FAnimTransitionDesc& Transition : Asset->GetTransitions())
    {
        if (Transition.FromStateId == FromStateId &&
            Transition.ToStateId == ToStateId &&
            Transition.Conditions.size() == Conditions.size())
        {
            bool bSameConditions = true;
            for (size_t Index = 0; Index < Conditions.size(); ++Index)
            {
                const FAnimTransitionCondition& A = Transition.Conditions[Index];
                const FAnimTransitionCondition& B = Conditions[Index];
                if (A.ParameterName != B.ParameterName ||
                    A.CompareOp != B.CompareOp ||
                    A.CompareValue.BoolValue != B.CompareValue.BoolValue ||
                    A.CompareValue.IntValue != B.CompareValue.IntValue ||
                    A.CompareValue.FloatValue != B.CompareValue.FloatValue ||
                    A.CompareValue.VectorValue != B.CompareValue.VectorValue)
                {
                    bSameConditions = false;
                    break;
                }
            }

            if (bSameConditions)
            {
                return &Transition;
            }
        }
    }

    return nullptr;
}

void DrawConditionValueEditor(
    const char* IdSuffix,
    EAnimParameterType Type,
    FAnimParameterValue& InOutValue)
{
    switch (Type)
    {
    case EAnimParameterType::Bool:
        ImGui::Checkbox((FString("Value##Bool") + IdSuffix).c_str(), &InOutValue.BoolValue);
        break;
    case EAnimParameterType::Int:
        ImGui::InputInt((FString("Value##Int") + IdSuffix).c_str(), &InOutValue.IntValue);
        break;
    case EAnimParameterType::Float:
        ImGui::InputFloat((FString("Value##Float") + IdSuffix).c_str(), &InOutValue.FloatValue, 0.05f, 0.25f, "%.3f");
        break;
    case EAnimParameterType::Vector:
    {
        float Values[3] =
        {
            InOutValue.VectorValue.X,
            InOutValue.VectorValue.Y,
            InOutValue.VectorValue.Z
        };
        if (ImGui::InputFloat3((FString("Value##Vector") + IdSuffix).c_str(), Values, "%.3f"))
        {
            InOutValue.VectorValue = FVector(Values[0], Values[1], Values[2]);
        }
        break;
    }
    case EAnimParameterType::Trigger:
        ImGui::TextDisabled("Trigger");
        break;
    default:
        break;
    }
}

void CopyToBuffer(char* Buffer, size_t BufferSize, const FString& Value)
{
    if (!Buffer || BufferSize == 0)
    {
        return;
    }

    std::snprintf(Buffer, BufferSize, "%s", Value.c_str());
}

FString MakeUniqueStateName(const UAnimStateMachineAsset* Asset, const FString& BaseName)
{
    const FString CleanBase = BaseName.empty() ? "New State" : BaseName;
    if (!Asset || !Asset->FindState(FName(CleanBase)))
    {
        return CleanBase;
    }

    for (int32 Index = 1; Index < 10000; ++Index)
    {
        FString Candidate = CleanBase + " " + std::to_string(Index);
        if (!Asset->FindState(FName(Candidate)))
        {
            return Candidate;
        }
    }

    return CleanBase + " 10000";
}

bool IsDuplicateStateName(
    const UAnimStateMachineAsset* Asset,
    const FString& Candidate,
    FAnimStateId IgnoredStateId)
{
    const FAnimStateDesc* ExistingState = Asset ? Asset->FindState(FName(Candidate)) : nullptr;
    return ExistingState && ExistingState->Id != IgnoredStateId;
}

void ApplyDetachedDocumentWindowClass()
{
    ImGuiWindowClass WindowClass;
    WindowClass.ClassId = 0x4A534457u; // "JSDW" - detached document window class
    WindowClass.ViewportFlagsOverrideSet =
        ImGuiViewportFlags_NoAutoMerge |
        ImGuiViewportFlags_NoDecoration;
    WindowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&WindowClass);
}

HWND GetCurrentViewportHwnd()
{
    ImGuiViewport* Viewport = ImGui::GetWindowViewport();
    if (!Viewport)
    {
        return nullptr;
    }
    return static_cast<HWND>(Viewport->PlatformHandleRaw ? Viewport->PlatformHandleRaw : Viewport->PlatformHandle);
}

ImGui_ImplWin32_CustomChromeRect MakeChromeRect(const ImVec2& Min, const ImVec2& Max, const ImVec2& WindowPos)
{
    return ImGui_ImplWin32_CustomChromeRect{
        static_cast<int>(Min.x - WindowPos.x),
        static_cast<int>(Min.y - WindowPos.y),
        static_cast<int>(Max.x - WindowPos.x),
        static_cast<int>(Max.y - WindowPos.y)
    };
}

void AddChromeRect(ImGui_ImplWin32_CustomChromeRect* Rects, int& Count, const ImVec2& Min, const ImVec2& Max, const ImVec2& WindowPos)
{
    if (Count >= 16)
    {
        return;
    }
    Rects[Count++] = MakeChromeRect(Min, Max, WindowPos);
}

bool IsViewportMaximized(HWND Hwnd)
{
    return Hwnd && ::IsZoomed(Hwnd) != FALSE;
}

void ToggleViewportMaximize(HWND Hwnd)
{
    if (!Hwnd)
    {
        return;
    }
    ::PostMessageW(Hwnd, WM_SYSCOMMAND, IsViewportMaximized(Hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
}

bool DrawDetachedWindowButton(
    const char* Id,
    const char* Tooltip,
    const ImVec2& Size,
    const ImVec4& HoverColor,
    const ImVec4& ActiveColor,
    const std::function<void(ImDrawList*, const ImVec2&, const ImVec2&, ImU32)>& DrawIcon)
{
    ImGui::PushID(Id);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HoverColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ActiveColor);

    const bool bClicked = ImGui::InvisibleButton("##Button", Size);
    const bool bHovered = ImGui::IsItemHovered();
    const bool bActive = ImGui::IsItemActive();
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();
    const ImU32 BgColor = ImGui::GetColorU32(
        bActive ? ActiveColor : (bHovered ? HoverColor : ImVec4(0.0f, 0.0f, 0.0f, 0.0f)));

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->AddRectFilled(Min, Max, BgColor, 0.0f);
    DrawIcon(DrawList, Min, Max, ImGui::GetColorU32(ImVec4(0.82f, 0.85f, 0.90f, 1.0f)));

    if (bHovered && Tooltip)
    {
        ImGui::SetTooltip("%s", Tooltip);
    }

    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return bClicked;
}

constexpr uint64 MeshEditHashOffset = 14695981039346656037ull;
constexpr uint64 MeshEditHashPrime = 1099511628211ull;

uint64 HashBytes(uint64 Seed, const void* Data, size_t Size)
{
    const unsigned char* Bytes = static_cast<const unsigned char*>(Data);
    for (size_t Index = 0; Index < Size; ++Index)
    {
        Seed ^= static_cast<uint64>(Bytes[Index]);
        Seed *= MeshEditHashPrime;
    }
    return Seed;
}

template <typename T>
uint64 HashValue(uint64 Seed, const T& Value)
{
    return HashBytes(Seed, &Value, sizeof(T));
}

uint64 HashString(uint64 Seed, const FString& Value)
{
    const uint64 Length = static_cast<uint64>(Value.size());
    Seed = HashValue(Seed, Length);
    return Value.empty() ? Seed : HashBytes(Seed, Value.data(), Value.size());
}

uint64 HashMatrix(uint64 Seed, const FMatrix& Matrix)
{
    return HashBytes(Seed, Matrix.M, sizeof(Matrix.M));
}

ImU32 AnimColorPanel()
{
    return IM_COL32(21, 24, 30, 255);
}

ImU32 AnimColorPanelAlt()
{
    return IM_COL32(27, 31, 38, 255);
}

ImU32 AnimColorBorder()
{
    return IM_COL32(58, 66, 78, 255);
}

ImU32 AnimColorAccent()
{
    return IM_COL32(96, 168, 255, 255);
}

ImU32 AnimColorNotify()
{
    return IM_COL32(255, 150, 72, 255);
}

bool DrawAnimButton(const char* Label, const ImVec2& Size, bool bPrimary, bool bActive, bool bEnabled = true)
{
    const ImVec4 Normal = bPrimary
        ? ImVec4(0.23f, 0.42f, 0.70f, 1.0f)
        : ImVec4(0.13f, 0.16f, 0.21f, 1.0f);
    const ImVec4 Hover = bPrimary
        ? ImVec4(0.30f, 0.52f, 0.84f, 1.0f)
        : ImVec4(0.18f, 0.22f, 0.29f, 1.0f);
    const ImVec4 Active = bActive
        ? ImVec4(0.38f, 0.58f, 0.86f, 1.0f)
        : (bPrimary ? ImVec4(0.20f, 0.36f, 0.60f, 1.0f) : ImVec4(0.10f, 0.13f, 0.18f, 1.0f));

    ImGui::PushStyleColor(ImGuiCol_Button, Normal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Active);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));

    if (!bEnabled)
    {
        ImGui::BeginDisabled();
    }
    const bool bClicked = ImGui::Button(Label, Size) && bEnabled;
    if (!bEnabled)
    {
        ImGui::EndDisabled();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    return bClicked;
}

bool DrawAnimIconButton(
    ID3D11ShaderResourceView* Icon,
    const char* FallbackLabel,
    const char* Tooltip,
    const ImVec2& Size,
    bool bPrimary,
    bool bActive,
    bool bEnabled = true)
{
    ImGui::PushID(Tooltip ? Tooltip : FallbackLabel);
    if (!bEnabled)
    {
        ImGui::BeginDisabled();
    }

    const bool bClicked = ImGui::InvisibleButton("##AnimIconButton", Size) && bEnabled;
    const bool bHovered = bEnabled && ImGui::IsItemHovered();
    const bool bPressed = bEnabled && ImGui::IsItemActive();
    const ImVec2 Min = ImGui::GetItemRectMin();
    const ImVec2 Max = ImGui::GetItemRectMax();

    const ImVec4 Normal = bPrimary
        ? ImVec4(0.22f, 0.39f, 0.65f, 1.0f)
        : ImVec4(0.12f, 0.15f, 0.19f, 1.0f);
    const ImVec4 Hover = bPrimary
        ? ImVec4(0.30f, 0.52f, 0.84f, 1.0f)
        : ImVec4(0.17f, 0.21f, 0.27f, 1.0f);
    const ImVec4 Active = bActive || bPressed
        ? ImVec4(0.35f, 0.54f, 0.80f, 1.0f)
        : Normal;

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImU32 Bg = ImGui::GetColorU32(bPressed || bActive ? Active : (bHovered ? Hover : Normal));
    const ImU32 Border = ImGui::GetColorU32(bActive ? ImVec4(0.62f, 0.78f, 1.0f, 1.0f) : ImVec4(0.24f, 0.28f, 0.35f, 1.0f));
    DrawList->AddRectFilled(Min, Max, Bg, 5.0f);
    DrawList->AddRect(Min, Max, Border, 5.0f);

    if (Icon)
    {
        const float IconSize = std::min(Size.x, Size.y) - 8.0f;
        const ImVec2 IconMin(
            Min.x + (Size.x - IconSize) * 0.5f,
            Min.y + (Size.y - IconSize) * 0.5f);
        const ImVec2 IconMax(IconMin.x + IconSize, IconMin.y + IconSize);
        DrawList->AddImage(reinterpret_cast<ImTextureID>(Icon), IconMin, IconMax);
    }
    else if (FallbackLabel)
    {
        const ImVec2 TextSize = ImGui::CalcTextSize(FallbackLabel);
        DrawList->AddText(
            ImVec2((Min.x + Max.x - TextSize.x) * 0.5f, (Min.y + Max.y - TextSize.y) * 0.5f),
            ImGui::GetColorU32(ImVec4(0.90f, 0.94f, 1.0f, 1.0f)),
            FallbackLabel);
    }

    if (!bEnabled)
    {
        ImGui::EndDisabled();
    }
    if (bHovered && Tooltip)
    {
        ImGui::SetTooltip("%s", Tooltip);
    }
    ImGui::PopID();
    return bClicked;
}

bool DrawAnimToggle(const char* Label, bool bValue, bool bEnabled = true)
{
    ImGui::PushStyleColor(ImGuiCol_Button, bValue ? ImVec4(0.22f, 0.34f, 0.50f, 1.0f) : ImVec4(0.12f, 0.15f, 0.19f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bValue ? ImVec4(0.28f, 0.43f, 0.62f, 1.0f) : ImVec4(0.17f, 0.21f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.31f, 0.46f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);

    if (!bEnabled)
    {
        ImGui::BeginDisabled();
    }
    const bool bClicked = ImGui::Button(Label, ImVec2(78.0f, 26.0f)) && bEnabled;
    if (!bEnabled)
    {
        ImGui::EndDisabled();
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return bClicked;
}

void DrawAnimMetric(const char* Label, const char* Value, float Width)
{
    const ImVec2 Pos = ImGui::GetCursorScreenPos();
    const ImVec2 Size(Width, 34.0f);
    ImGui::Dummy(Size);

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->AddRectFilled(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y), IM_COL32(14, 17, 22, 255), 5.0f);
    DrawList->AddRect(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y), AnimColorBorder(), 5.0f);
    DrawList->AddText(ImVec2(Pos.x + 9.0f, Pos.y + 5.0f), IM_COL32(128, 138, 152, 255), Label);
    DrawList->AddText(ImVec2(Pos.x + 9.0f, Pos.y + 18.0f), IM_COL32(230, 236, 244, 255), Value);
}

void DrawTrackLabel(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, const char* Label, ImU32 Accent)
{
    DrawList->AddRectFilled(Min, Max, IM_COL32(24, 28, 35, 255));
    DrawList->AddRectFilled(ImVec2(Min.x, Min.y), ImVec2(Min.x + 3.0f, Max.y), Accent);
    DrawList->AddText(ImVec2(Min.x + 10.0f, Min.y + 5.0f), IM_COL32(200, 208, 218, 255), Label);
}
}

void FEditorViewerWindowWidget::Initialize(UEditorEngine* InEditorEngine)
{
    FEditorWidget::Initialize(InEditorEngine);
    LoadViewerIcons();
}

FEditorViewerWindowWidget::~FEditorViewerWindowWidget()
{
    Shutdown();
}

void FEditorViewerWindowWidget::Shutdown()
{
    ReleaseViewerIcons();
    Children.clear();
    BoneToSocketIndices.clear();
    CachedMesh = nullptr;
    CachedSkComp = nullptr; 

    PendingPreviewPickerSocketIdx = -1; 
    RenameSocketIdx = -1;               
    bMeshDirty = false; 
    CleanMeshEditSignature = 0;
    bHasCleanMeshEditSignature = false;

    Viewer = nullptr;
    bOpen = false;
}

void FEditorViewerWindowWidget::LoadViewerIcons()
{
    if (!EditorEngine)
    {
        return;
    }

    ID3D11Device* Device = EditorEngine->GetRenderer().GetFD3DDevice().GetDevice();
    if (!Device)
    {
        return;
    }

    const std::wstring IconDir = FEditorResourcePaths::ViewerIconsAbsoluteDir();
    for (int32 Index = 0; Index < static_cast<int32>(EEditorViewerIcon::Count); ++Index)
    {
        ID3D11ShaderResourceView*& SRV = ViewerIconResources.Icons[Index];
        if (SRV)
        {
            continue;
        }

        const std::wstring IconPath = IconDir + GetViewerIconFileName(static_cast<EEditorViewerIcon>(Index));
        DirectX::CreateWICTextureFromFile(Device, IconPath.c_str(), nullptr, &SRV);
    }
}

void FEditorViewerWindowWidget::ReleaseViewerIcons()
{
    for (int32 Index = 0; Index < static_cast<int32>(EEditorViewerIcon::Count); ++Index)
    {
        if (ViewerIconResources.Icons[Index])
        {
            ViewerIconResources.Icons[Index]->Release();
            ViewerIconResources.Icons[Index] = nullptr;
        }
    }
}

ID3D11ShaderResourceView* FEditorViewerWindowWidget::GetViewerIcon(EEditorViewerIcon Icon) const
{
    const int32 Index = static_cast<int32>(Icon);
    if (Index < 0 || Index >= static_cast<int32>(EEditorViewerIcon::Count))
    {
        return nullptr;
    }

    return ViewerIconResources.Icons[Index];
}

FString FEditorViewerWindowWidget::GetWindowName() const
{
    char WindowName[64];
    sprintf_s(WindowName, "###ViewerWindow_%p", Viewer);
    return GetViewerAssetLabel(Viewer) + WindowName;
}

bool FEditorViewerWindowWidget::CanSaveMesh() const
{
	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
	if (!SkeletalViewer)
	{
		return false;
	}

	ASkeletalMeshActor* ViewTarget = SkeletalViewer->GetViewTarget();
	USkeletalMeshComponent* SkelComp = ViewTarget ? ViewTarget->GetSkeletalMeshComponent() : nullptr;
	return SkelComp && SkelComp->GetSkeletalMesh() && HasMeshAssetEdits();
}

bool FEditorViewerWindowWidget::IsMeshDirty() const
{
	return HasMeshAssetEdits();
}

void FEditorViewerWindowWidget::RequestSaveMesh()
{
	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
	if (!SkeletalViewer)
	{
		return;
	}

	ASkeletalMeshActor* ViewTarget = SkeletalViewer->GetViewTarget();
	USkeletalMeshComponent* SkelComp = ViewTarget ? ViewTarget->GetSkeletalMeshComponent() : nullptr;
	USkeletalMesh* Mesh = SkelComp ? SkelComp->GetSkeletalMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}

	if (FResourceManager::Get().SaveSkeletalMesh(Mesh))
	{
		ResetMeshDirtyBaseline();
	}
}

bool FEditorViewerWindowWidget::CanSaveGraph() const
{
	FAnimStateMachineGraphViewer* GraphViewer = AsAnimStateMachineGraphViewer(Viewer);
	return GraphViewer && GraphViewer->GetAsset() && GraphViewer->IsDirty();
}

bool FEditorViewerWindowWidget::IsGraphDirty() const
{
	FAnimStateMachineGraphViewer* GraphViewer = AsAnimStateMachineGraphViewer(Viewer);
	return GraphViewer && GraphViewer->IsDirty();
}

void FEditorViewerWindowWidget::RequestSaveGraph()
{
	FAnimStateMachineGraphViewer* GraphViewer = AsAnimStateMachineGraphViewer(Viewer);
	if (!GraphViewer)
	{
		return;
	}

	if (GraphViewer->SaveAsset())
	{
		if (EditorEngine)
		{
			EditorEngine->GetAssetService().RefreshAssetDatabase();
			EditorEngine->GetNotificationService().Info("State machine saved.");
		}
	}
	else if (EditorEngine)
	{
		EditorEngine->GetNotificationService().Warning("Failed to save state machine.");
	}
}

void FEditorViewerWindowWidget::RequestValidateGraph()
{
	FAnimStateMachineGraphViewer* GraphViewer = AsAnimStateMachineGraphViewer(Viewer);
	if (!GraphViewer)
	{
		return;
	}

	const bool bValid = GraphViewer->ValidateAsset();
	if (EditorEngine)
	{
		if (bValid)
		{
			EditorEngine->GetNotificationService().Info("State machine is valid.");
		}
		else
		{
			EditorEngine->GetNotificationService().Warning("State machine validation failed.");
		}
	}
}

FSkeletalMesh* FEditorViewerWindowWidget::ResolveCurrentMeshData() const
{
	if (CachedMesh)
	{
		return CachedMesh;
	}

	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
	if (!SkeletalViewer)
	{
		return nullptr;
	}

	ASkeletalMeshActor* ViewTarget = SkeletalViewer->GetViewTarget();
	USkeletalMeshComponent* SkelComp = ViewTarget ? ViewTarget->GetSkeletalMeshComponent() : nullptr;
	USkeletalMesh* Mesh = SkelComp ? SkelComp->GetSkeletalMesh() : nullptr;
	return Mesh ? Mesh->GetMeshData() : nullptr;
}

uint64 FEditorViewerWindowWidget::ComputeEditableMeshSignature(const FSkeletalMesh* MeshData) const
{
	if (!MeshData)
	{
		return 0;
	}

	uint64 Hash = MeshEditHashOffset;

	Hash = HashValue(Hash, static_cast<uint64>(MeshData->Bones.size()));
	for (const FBoneInfo& Bone : MeshData->Bones)
	{
		Hash = HashString(Hash, Bone.Name);
		Hash = HashValue(Hash, Bone.ParentIndex);
		Hash = HashMatrix(Hash, Bone.LocalBindTransform);
		Hash = HashMatrix(Hash, Bone.GlobalBindTransform);
		Hash = HashMatrix(Hash, Bone.InverseBindPose);
	}

	Hash = HashValue(Hash, static_cast<uint64>(MeshData->Sockets.size()));
	for (const FSkeletalMeshSocket& Socket : MeshData->Sockets)
	{
		Hash = HashString(Hash, Socket.Name.ToString());
		Hash = HashValue(Hash, Socket.BoneIndex);
		Hash = HashValue(Hash, Socket.RelativeLocation.X);
		Hash = HashValue(Hash, Socket.RelativeLocation.Y);
		Hash = HashValue(Hash, Socket.RelativeLocation.Z);
		Hash = HashValue(Hash, Socket.RelativeRotation.Pitch);
		Hash = HashValue(Hash, Socket.RelativeRotation.Yaw);
		Hash = HashValue(Hash, Socket.RelativeRotation.Roll);
		Hash = HashValue(Hash, Socket.RelativeScale.X);
		Hash = HashValue(Hash, Socket.RelativeScale.Y);
		Hash = HashValue(Hash, Socket.RelativeScale.Z);
	}

	return Hash;
}

void FEditorViewerWindowWidget::ResetMeshDirtyBaseline()
{
	FSkeletalMesh* MeshData = ResolveCurrentMeshData();
	if (!MeshData)
	{
		CleanMeshEditSignature = 0;
		bHasCleanMeshEditSignature = false;
		bMeshDirty = false;
		return;
	}

	CleanMeshEditSignature = ComputeEditableMeshSignature(MeshData);
	bHasCleanMeshEditSignature = true;
	bMeshDirty = false;
}

bool FEditorViewerWindowWidget::HasMeshAssetEdits() const
{
	FSkeletalMesh* MeshData = ResolveCurrentMeshData();
	if (!MeshData)
	{
		return false;
	}

	if (bMeshDirty)
	{
		return true;
	}

	return bHasCleanMeshEditSignature && ComputeEditableMeshSignature(MeshData) != CleanMeshEditSignature;
}

void FEditorViewerWindowWidget::Render(float DeltaTime)
{
    if (!bOpen)
        return;

    if (!EditorEngine)
        return;

	if (!Viewer)
        return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(13.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.055f, 0.060f, 0.072f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.20f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.15f, 0.17f, 0.22f, 1.0f));

	FString WindowName = GetWindowName();
	bool bDockRequested = false;
	bool bCloseRequested = false;

    // Detached document는 borderless secondary viewport로 띄우고,
    // Win32 backend에 titlebar hit-test 정보를 넘겨 native window처럼 움직이게 한다.
    ApplyDetachedDocumentWindowClass();
	// Make the viewer window reasonably large on first creation.
	ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (const ImGuiViewport* MainViewport = ImGui::GetMainViewport())
    {
        ImGui::SetNextWindowPos(
            ImVec2(MainViewport->Pos.x + 120.0f, MainViewport->Pos.y + 90.0f),
            ImGuiCond_FirstUseEver);
    }
	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse;
	if (ImGui::Begin(WindowName.c_str(), &bOpen, WindowFlags))
	{
        RenderDetachedDocumentChrome(bDockRequested, bCloseRequested);
        RenderDetachedDocumentToolbar(bDockRequested);
        RenderEmbedded(DeltaTime);
	}
	ImGui::End();

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(5);

	if (bDockRequested)
	{
		EditorEngine->GetMainPanel().RequestDockViewer(Viewer);
		return;
	}
	if (bCloseRequested)
	{
		bOpen = false;
	}

	if (!bOpen)
    {
        EditorEngine->RemoveViewer(Viewer);
        Shutdown();
    }
}

void FEditorViewerWindowWidget::RenderDetachedDocumentChrome(bool& bDockRequested, bool& bCloseRequested)
{
    if (!Viewer || !ImGui::BeginMenuBar())
    {
        return;
    }

    constexpr float WindowButtonWidth = 48.0f;
    constexpr float TitleBarHeight = FEditorChromeMetrics::ApplicationTitleBarHeight;
    constexpr float LogoSize = 28.0f;
    constexpr float LogoPaddingX = 4.0f;
    constexpr float MenuLogoGap = 8.0f;
    constexpr float MenuStartX = LogoPaddingX + LogoSize + MenuLogoGap;

    HWND ViewportHwnd = GetCurrentViewportHwnd();
    const ImVec2 WindowPos = ImGui::GetWindowPos();
    const ImVec2 WindowSize = ImGui::GetWindowSize();
    const float ButtonStartX = std::max(0.0f, WindowSize.x - WindowButtonWidth * 3.0f);

    ImGui_ImplWin32_CustomChromeRect ChromeRects[16] = {};
    int ChromeRectCount = 0;

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    ID3D11ShaderResourceView* HomeIcon = EditorEngine ? EditorEngine->GetMainPanel().GetHomeIconResource() : nullptr;
    const ImVec2 LogoMin(WindowPos.x + LogoPaddingX, WindowPos.y + (TitleBarHeight - LogoSize) * 0.5f);
    const ImVec2 LogoMax(LogoMin.x + LogoSize, LogoMin.y + LogoSize);
    if (HomeIcon)
    {
        DrawList->AddImage(reinterpret_cast<ImTextureID>(HomeIcon), LogoMin, LogoMax);
    }
    else
    {
        DrawList->AddRectFilled(LogoMin, LogoMax, ImGui::GetColorU32(ImVec4(0.95f, 0.78f, 0.12f, 1.0f)), 0.0f);
        DrawList->AddText(
            ImVec2(LogoMin.x + 4.0f, LogoMin.y + 5.0f),
            ImGui::GetColorU32(ImVec4(0.08f, 0.09f, 0.11f, 1.0f)),
            "JS");
    }
    AddChromeRect(ChromeRects, ChromeRectCount, LogoMin, LogoMax, WindowPos);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));

    ImGui::SetCursorPosX(MenuStartX);

    const bool bGraphViewer = AsAnimStateMachineGraphViewer(Viewer) != nullptr;
    const bool bCanSaveAsset = bGraphViewer ? CanSaveGraph() : CanSaveMesh();
    const char* SaveAssetLabel = bGraphViewer
        ? (IsGraphDirty() ? "Save State Machine *" : "Save State Machine")
        : (IsMeshDirty() ? "Save Mesh *" : "Save Mesh");

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem(SaveAssetLabel, "Ctrl+S", false, bCanSaveAsset))
        {
            bGraphViewer ? RequestSaveGraph() : RequestSaveMesh();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Close"))
        {
            bCloseRequested = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
        ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Asset"))
    {
        if (ImGui::MenuItem(SaveAssetLabel, nullptr, false, bCanSaveAsset))
        {
            bGraphViewer ? RequestSaveGraph() : RequestSaveMesh();
        }
        if (bGraphViewer)
        {
            if (ImGui::MenuItem("Validate State Machine", nullptr, false, true))
            {
                RequestValidateGraph();
            }
        }
        else
        {
            ImGui::MenuItem("Reimport Mesh", nullptr, false, false);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window"))
    {
        if (ImGui::MenuItem("Dock Back"))
        {
            bDockRequested = true;
        }
        if (ImGui::MenuItem("Close"))
        {
            bCloseRequested = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools"))
    {
        if (FSkeletalViewerShowFlags* ShowFlags = GetViewerShowFlags(Viewer))
        {
            ImGui::MenuItem("Bones", nullptr, &ShowFlags->bShowBones);
            ImGui::MenuItem("Bounding Box", nullptr, &ShowFlags->bShowBoundingBox);
            ImGui::MenuItem("Outline", nullptr, &ShowFlags->bShowOutline);
            ImGui::Separator();
            ImGui::MenuItem("Bone Weight Heatmap", nullptr, &ShowFlags->bShowBoneWeightHeatmap);
            ImGui::BeginDisabled(!ShowFlags->bShowBoneWeightHeatmap);
            if (ImGui::SliderFloat("Opacity", &ShowFlags->BoneWeightHeatmapOpacity, 0.05f, 1.0f, "%.2f"))
            {
                ShowFlags->BoneWeightHeatmapOpacity = std::clamp(ShowFlags->BoneWeightHeatmapOpacity, 0.05f, 1.0f);
            }
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextDisabled("No viewer tools");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help"))
    {
        if (AsAnimStateMachineGraphViewer(Viewer))
        {
            ImGui::TextDisabled("Animation State Machine Graph");
        }
        else
        {
            ImGui::TextDisabled(AsAnimationViewer(Viewer) ? "Animation Viewer" : "Skeletal Mesh Previewer");
        }
        ImGui::EndMenu();
    }

    const float MenuEndX = std::min(ButtonStartX, ImGui::GetCursorScreenPos().x - WindowPos.x + 8.0f);
    AddChromeRect(
        ChromeRects,
        ChromeRectCount,
        ImVec2(WindowPos.x, WindowPos.y),
        ImVec2(WindowPos.x + MenuEndX, WindowPos.y + TitleBarHeight),
        WindowPos);

    const FString AssetLabel = GetViewerAssetLabel(Viewer);
    const ImVec2 TitleSize = ImGui::CalcTextSize(AssetLabel.c_str());
    const float TitleX = std::clamp(
        MenuEndX + (ButtonStartX - MenuEndX - TitleSize.x) * 0.5f,
        MenuEndX + 8.0f,
        std::max(MenuEndX + 8.0f, ButtonStartX - TitleSize.x - 8.0f));
    DrawList->AddText(
        ImVec2(WindowPos.x + TitleX, WindowPos.y + (TitleBarHeight - TitleSize.y) * 0.5f),
        ImGui::GetColorU32(ImVec4(0.72f, 0.76f, 0.84f, 1.0f)),
        AssetLabel.c_str());

    const ImVec2 ButtonSize(WindowButtonWidth, TitleBarHeight);
    ImGui::SetCursorPos(ImVec2(ButtonStartX, 0.0f));
    if (DrawDetachedWindowButton(
        "DetachedMinimize",
        "Minimize",
        ButtonSize,
        ImVec4(0.14f, 0.16f, 0.20f, 1.0f),
        ImVec4(0.18f, 0.20f, 0.25f, 1.0f),
        [](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
        {
            const float Y = (Min.y + Max.y) * 0.5f + 4.0f;
            InDrawList->AddLine(ImVec2(Min.x + 17.0f, Y), ImVec2(Max.x - 17.0f, Y), Color, 1.6f);
        }))
    {
        if (ViewportHwnd)
        {
            ::PostMessageW(ViewportHwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        }
    }
    AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

    ImGui::SameLine(0.0f, 0.0f);
    if (DrawDetachedWindowButton(
        "DetachedMaximize",
        IsViewportMaximized(ViewportHwnd) ? "Restore" : "Maximize",
        ButtonSize,
        ImVec4(0.14f, 0.16f, 0.20f, 1.0f),
        ImVec4(0.18f, 0.20f, 0.25f, 1.0f),
        [ViewportHwnd](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
        {
            const bool bMaximized = IsViewportMaximized(ViewportHwnd);
            const ImVec2 A(Min.x + 17.0f, Min.y + 12.0f);
            const ImVec2 B(Max.x - 17.0f, Max.y - 12.0f);
            if (bMaximized)
            {
                InDrawList->AddRect(ImVec2(A.x + 3.0f, A.y), ImVec2(B.x + 3.0f, B.y - 3.0f), Color, 0.0f, 0, 1.4f);
                InDrawList->AddRect(ImVec2(A.x, A.y + 3.0f), ImVec2(B.x, B.y), Color, 0.0f, 0, 1.4f);
            }
            else
            {
                InDrawList->AddRect(A, B, Color, 0.0f, 0, 1.4f);
            }
        }))
    {
        ToggleViewportMaximize(ViewportHwnd);
    }
    AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

    ImGui::SameLine(0.0f, 0.0f);
    if (DrawDetachedWindowButton(
        "DetachedClose",
        "Close",
        ButtonSize,
        ImVec4(0.62f, 0.18f, 0.20f, 1.0f),
        ImVec4(0.46f, 0.10f, 0.13f, 1.0f),
        [](ImDrawList* InDrawList, const ImVec2& Min, const ImVec2& Max, ImU32 Color)
        {
            InDrawList->AddLine(ImVec2(Min.x + 17.0f, Min.y + 12.0f), ImVec2(Max.x - 17.0f, Max.y - 12.0f), Color, 1.6f);
            InDrawList->AddLine(ImVec2(Max.x - 17.0f, Min.y + 12.0f), ImVec2(Min.x + 17.0f, Max.y - 12.0f), Color, 1.6f);
        }))
    {
        bCloseRequested = true;
    }
    AddChromeRect(ChromeRects, ChromeRectCount, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), WindowPos);

    ImGui_ImplWin32_SetCustomChrome(ViewportHwnd, static_cast<int>(TitleBarHeight), ChromeRects, ChromeRectCount);
    ImGui::PopStyleVar(3);
    ImGui::EndMenuBar();
}

void FEditorViewerWindowWidget::RenderDetachedDocumentToolbar(bool& bDockRequested)
{
    if (!Viewer || !EditorEngine)
    {
        return;
    }

    constexpr ImGuiWindowFlags ToolbarFlags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##DetachedViewerToolbar", ImVec2(0.0f, 40.0f), false, ToolbarFlags);
    ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));

    const bool bCanSaveMesh = CanSaveMesh();
    if (!bCanSaveMesh)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(IsMeshDirty() ? "Save *" : "Save"))
    {
        RequestSaveMesh();
    }
    if (!bCanSaveMesh)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Dock"))
    {
        bDockRequested = true;
    }
    ImGui::SameLine(0.0f, 12.0f);
    EditorEngine->GetMainPanel().RenderViewerToolbarControls(Viewer);
    ImGui::EndChild();
}

void FEditorViewerWindowWidget::RenderEmbedded(float DeltaTime)
{
	if (!bOpen || !EditorEngine || !Viewer)
	{
		return;
	}

	if (AsSkeletalMeshViewer(Viewer))
    {
        RenderSkeletalMeshContent(DeltaTime);
	}
    else if (AsAnimationViewer(Viewer))
    {
        RenderAnimationContent(DeltaTime);
	}
    else if (AsAnimStateMachineGraphViewer(Viewer))
    {
        RenderAnimStateMachineGraphContent(DeltaTime);
    }
}

void FEditorViewerWindowWidget::RenderAnimStateMachineGraphContent(float DeltaTime)
{
    (void)DeltaTime;

    FAnimStateMachineGraphViewer* GraphViewer = AsAnimStateMachineGraphViewer(Viewer);
    UAnimStateMachineAsset* Asset = GraphViewer ? GraphViewer->GetAsset() : nullptr;
    if (!GraphViewer)
    {
        return;
    }

    ImVec2 FullSize = ImGui::GetContentRegionAvail();
    const bool bShowOutline = FullSize.x >= 760.0f;
    const float OutlineWidth = bShowOutline ? std::clamp(FullSize.x * 0.22f, 260.0f, 360.0f) : 0.0f;
    const float InspectorWidth = std::clamp(FullSize.x * 0.30f, 280.0f, 420.0f);
    const float HorizontalSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float GraphWidth = std::max(
        220.0f,
        FullSize.x - InspectorWidth - (bShowOutline ? OutlineWidth + HorizontalSpacing * 2.0f : HorizontalSpacing));

    if (bShowOutline)
    {
        ImGui::BeginChild("AnimStateMachineOutline", ImVec2(OutlineWidth, 0), true);
        ImGui::TextUnformatted("Parameters");
        ImGui::Separator();
        if (!Asset)
        {
            ImGui::TextDisabled("No asset");
        }
        else
        {
            const FString NewParameterName = StringUtils::Trim(GraphNewParameterNameBuffer);
            const bool bNewParameterNameEmpty = NewParameterName.empty();
            const bool bDuplicateParameterName = !bNewParameterNameEmpty && Asset->FindParameter(FName(NewParameterName)) != nullptr;
            const bool bCanAddParameter = !bNewParameterNameEmpty && !bDuplicateParameterName;

            const float AddButtonWidth = ImGui::CalcTextSize("Add").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float ParameterTypeWidth = 84.0f;
            const float ParameterRowSpacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
            const float ParameterNameWidth = std::max(
                96.0f,
                ImGui::GetContentRegionAvail().x - ParameterTypeWidth - AddButtonWidth - ParameterRowSpacing);
            ImGui::SetNextItemWidth(ParameterNameWidth);
            ImGui::InputTextWithHint("##NewParameterName", "Parameter", GraphNewParameterNameBuffer, sizeof(GraphNewParameterNameBuffer));
            ImGui::SameLine();
            static const char* ParameterTypeLabels[] = { "Bool", "Int", "Float", "Vector", "Trigger" };
            ImGui::SetNextItemWidth(ParameterTypeWidth);
            ImGui::Combo("##NewParameterType", &GraphNewParameterTypeIndex, ParameterTypeLabels, IM_ARRAYSIZE(ParameterTypeLabels));
            ImGui::SameLine();
            ImGui::BeginDisabled(!bCanAddParameter);
            if (ImGui::Button("Add##AddParameter", ImVec2(AddButtonWidth, 0.0f)))
            {
                if (Asset->AddParameter(FName(NewParameterName), ParameterTypeFromIndex(GraphNewParameterTypeIndex)))
                {
                    GraphViewer->MarkDirty();
                    CopyToBuffer(GraphNewParameterNameBuffer, sizeof(GraphNewParameterNameBuffer), "NewParameter");
                }
            }
            ImGui::EndDisabled();
            if (bDuplicateParameterName)
            {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.30f, 1.0f), "Parameter name already exists");
            }

            ImGui::Spacing();
            if (Asset->GetParameters().empty())
            {
                ImGui::TextDisabled("No parameters");
            }
            else
            {
                int32 SelectedParameterIndex = FindParameterIndex(Asset, GraphSelectedParameterName);
                if (SelectedParameterIndex < 0)
                {
                    SelectedParameterIndex = 0;
                    GraphSelectedParameterName = Asset->GetParameters()[0].Name;
                    CopyToBuffer(GraphRenameParameterBuffer, sizeof(GraphRenameParameterBuffer), GraphSelectedParameterName.ToString());
                }

                for (int32 Index = 0; Index < static_cast<int32>(Asset->GetParameters().size()); ++Index)
                {
                    const FAnimStateMachineParameterDesc& Parameter = Asset->GetParameters()[Index];
                    const bool bSelected = Index == SelectedParameterIndex;
                    const FString Label = Parameter.Name.ToString() + "  [" + ParameterTypeToString(Parameter.Type) + "]";
                    if (ImGui::Selectable(Label.c_str(), bSelected))
                    {
                        GraphSelectedParameterName = Parameter.Name;
                        CopyToBuffer(GraphRenameParameterBuffer, sizeof(GraphRenameParameterBuffer), Parameter.Name.ToString());
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                const FAnimStateMachineParameterDesc& SelectedParameter = Asset->GetParameters()[SelectedParameterIndex];
                ImGui::Text("Type: %s", ParameterTypeToString(SelectedParameter.Type).c_str());
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("Rename", GraphRenameParameterBuffer, sizeof(GraphRenameParameterBuffer));
                const FString RenameParameterName = StringUtils::Trim(GraphRenameParameterBuffer);
                const bool bCanRenameParameter =
                    !RenameParameterName.empty() &&
                    RenameParameterName != SelectedParameter.Name.ToString() &&
                    !Asset->FindParameter(FName(RenameParameterName));
                ImGui::BeginDisabled(!bCanRenameParameter);
                if (ImGui::Button("Apply Name"))
                {
                    const FName OldName = SelectedParameter.Name;
                    if (Asset->RenameParameter(OldName, FName(RenameParameterName)))
                    {
                        GraphSelectedParameterName = FName(RenameParameterName);
                        GraphViewer->MarkDirty();
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                {
                    const FName ParameterName = SelectedParameter.Name;
                    bool bDelete = true;
                    if (IsParameterReferencedByAnyTransition(Asset, ParameterName))
                    {
                        bDelete = MessageBoxW(
                            nullptr,
                            L"Remove parameter and all referencing conditions?",
                            L"Delete Parameter",
                            MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
                    }

                    if (bDelete)
                    {
                        const bool bRemoved = IsParameterReferencedByAnyTransition(Asset, ParameterName)
                            ? Asset->RemoveParameterAndConditions(ParameterName)
                            : Asset->RemoveParameter(ParameterName);
                        if (bRemoved)
                        {
                            GraphSelectedParameterName = FName();
                            GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
                            GraphViewer->MarkDirty();
                        }
                    }
                }

                if (ImGui::Button("Remove Unused"))
                {
                    if (Asset->RemoveUnusedParameters() > 0)
                    {
                        GraphSelectedParameterName = FName();
                        GraphViewer->MarkDirty();
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
    }

    ImGui::BeginChild("AnimStateMachineGraphCanvas", ImVec2(GraphWidth, 0), true);
    const ImVec2 GraphCanvasMin = ImGui::GetWindowPos();
    const ImVec2 GraphCanvasMax = Add(GraphCanvasMin, ImGui::GetWindowSize());
    // imgui-node-editor rewrites ImGuiIO mouse positions to canvas-local space during ed::Begin().
    // Keep screen-space input here so custom transition hit-testing matches CanvasToScreen() output.
    const ImVec2 GraphMouseScreenPosition = ImGui::GetIO().MousePos;
    const bool bGraphHoveredScreen = ImGui::IsMouseHoveringRect(GraphCanvasMin, GraphCanvasMax, true);
    const bool bGraphMouseClicked = ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left];
    const bool bGraphMouseRightReleased = ImGui::GetIO().MouseReleased[ImGuiMouseButton_Right];
    if (!Asset)
    {
        ImGui::TextDisabled("State machine asset is not loaded.");
        if (!GraphViewer->GetValidationMessage().empty())
        {
            ImGui::TextWrapped("%s", GraphViewer->GetValidationMessage().c_str());
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("AnimStateMachineInspector", ImVec2(InspectorWidth, 0), true);
        ImGui::TextDisabled("Inspector");
        ImGui::EndChild();
        return;
    }

    auto AddStateAtPosition = [&](const FString& RequestedName, const ImVec2& CanvasPosition) -> const FAnimStateDesc*
    {
        const FString NewStateName = MakeUniqueStateName(Asset, RequestedName);
        if (!Asset->AddState(FName(NewStateName), FName(), true))
        {
            return nullptr;
        }

        const FAnimStateDesc* NewState = Asset->FindState(FName(NewStateName));
        if (!NewState)
        {
            return nullptr;
        }

        Asset->SetStateEditorPosition(NewState->Id, CanvasPosition.x, CanvasPosition.y);
        ed::SetNodePosition(MakeStateNodeId(NewState->Id), CanvasPosition);
        ed::ClearSelection();
        ed::SelectNode(MakeStateNodeId(NewState->Id));
        GraphViewer->SetSelectedStateId(NewState->Id);
        GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
        GraphViewer->MarkDirty();
        CopyToBuffer(GraphNewStateNameBuffer, sizeof(GraphNewStateNameBuffer), MakeUniqueStateName(Asset, "New State"));
        return NewState;
    };

    auto SelectStateNode = [&](FAnimStateId StateId)
    {
        ed::ClearSelection();
        ed::SelectNode(MakeStateNodeId(StateId));
        GraphViewer->SetSelectedStateId(StateId);
        GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
    };

    auto CreateDefaultTransition = [&](FAnimStateId FromStateId, FAnimStateId ToStateId) -> bool
    {
        const FAnimStateDesc* FromState = Asset->FindStateById(FromStateId);
        const FAnimStateDesc* ToState = Asset->FindStateById(ToStateId);
        const FAnimStateMachineParameterDesc* Parameter = GetParameterByIndex(Asset, 0);
        if (!FromState || !ToState || FromStateId == ToStateId || !Parameter)
        {
            return false;
        }

        TArray<FAnimTransitionCondition> Conditions;
        Conditions.push_back(BuildDefaultConditionForParameter(*Parameter));
        if (const FAnimTransitionDesc* ExistingTransition = FindTransitionBySignature(Asset, FromStateId, ToStateId, Conditions))
        {
            ed::ClearSelection();
            GraphViewer->SetSelectedTransitionId(ExistingTransition->Id);
            GraphViewer->SetSelectedStateId(InvalidAnimStateId);
            return true;
        }

        if (!Asset->AddTransition(
            FromState->StateName,
            ToState->StateName,
            Conditions,
            0.0f,
            0,
            EAnimBlendEaseOption::Linear))
        {
            return false;
        }

        if (const FAnimTransitionDesc* NewTransition = FindTransitionBySignature(Asset, FromStateId, ToStateId, Conditions))
        {
            ed::ClearSelection();
            GraphViewer->SetSelectedTransitionId(NewTransition->Id);
            GraphViewer->SetSelectedStateId(InvalidAnimStateId);
        }
        GraphViewer->MarkDirty();
        return true;
    };

    ed::SetCurrentEditor(GraphViewer->GetNodeEditorContext());
    ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(0.08f, 0.085f, 0.10f, 1.0f));
    ed::PushStyleColor(ed::StyleColor_Grid, ImVec4(0.18f, 0.20f, 0.23f, 0.28f));
    ed::Begin("AnimStateMachineGraph", ImVec2(0, 0));
    const ImVec2 GraphMouseCanvasPosition = ed::ScreenToCanvas(GraphMouseScreenPosition);

    int32 DefaultPositionIndex = 0;
    for (const FAnimStateDesc& State : Asset->GetStates())
    {
        if (!GraphViewer->IsLayoutInitialized())
        {
            if (const FAnimStateEditorMetadata* Metadata = FindStateEditorMetadata(Asset, State.Id))
            {
                ed::SetNodePosition(MakeStateNodeId(State.Id), ImVec2(Metadata->NodeX, Metadata->NodeY));
            }
            else
            {
                const bool bEntryState = State.Id == Asset->GetEntryStateId();
                const int32 LayoutIndex = bEntryState ? 0 : ++DefaultPositionIndex;
                const int32 Column = LayoutIndex % 4;
                const int32 Row = LayoutIndex / 4;
                ed::SetNodePosition(
                    MakeStateNodeId(State.Id),
                    ImVec2(static_cast<float>(Column) * 240.0f, static_cast<float>(Row) * 140.0f));
            }
        }

        ed::BeginNode(MakeStateNodeId(State.Id));
        ImGui::BeginGroup();
        const bool bEntryState = State.Id == Asset->GetEntryStateId();
        if (bEntryState)
        {
            ImGui::TextColored(ImVec4(0.92f, 0.74f, 0.25f, 1.0f), "ENTRY");
        }
        else
        {
            ImGui::TextDisabled("STATE");
        }
        ImGui::TextUnformatted(State.StateName.ToString().c_str());
        if (!State.AnimationPath.empty())
        {
            ImGui::TextDisabled("%s", State.AnimationPath.c_str());
        }
        else if (!State.AnimationName.ToString().empty())
        {
            ImGui::TextDisabled("%s", State.AnimationName.ToString().c_str());
        }
        else
        {
            ImGui::TextDisabled("No animation");
        }
        ImGui::Dummy(ImVec2(180.0f, 4.0f));
        ImGui::EndGroup();
        ed::EndNode();
    }

    TArray<FTransitionDrawSegment> TransitionSegments;

    for (int32 TransitionIndex = 0; TransitionIndex < static_cast<int32>(Asset->GetTransitions().size()); ++TransitionIndex)
    {
        const FAnimTransitionDesc& Transition = Asset->GetTransitions()[TransitionIndex];
        FTransitionDrawSegment Segment;
        if (!BuildTransitionDrawSegment(Asset, Transition, TransitionIndex, Segment))
        {
            continue;
        }

        ImDrawList* GraphDrawList = ed::GetNodeBackgroundDrawList(Segment.FromNodeId);
        if (!GraphDrawList)
        {
            continue;
        }

        TransitionSegments.push_back(Segment);

        const bool bHoveredTransition =
            bGraphHoveredScreen &&
            DistanceToSegment(GraphMouseScreenPosition, Segment.StartScreen, Segment.EndScreen) <= TransitionHitRadiusPx;
        const bool bSelectedTransition = GraphViewer->GetSelectedTransitionId() == Transition.Id;
        const ImU32 Color = IM_COL32(184, 199, 224, bSelectedTransition ? 255 : 210);
        DrawStraightTransition(
            GraphDrawList,
            Segment.StartCanvas,
            Segment.EndCanvas,
            Color,
            bSelectedTransition ? 3.0f : 2.0f,
            bHoveredTransition,
            bSelectedTransition);
    }

    if (!GraphViewer->IsLayoutInitialized())
    {
        GraphViewer->MarkLayoutInitialized();
    }

    ed::NodeId ContextNodeId = ed::NodeId(0);
    const bool bNodeContextRequested = ed::ShowNodeContextMenu(&ContextNodeId);
    const bool bBackgroundContextRequested = !bNodeContextRequested && ed::ShowBackgroundContextMenu();

    ed::End();

    bool bOpenedCustomContextMenu = false;
    if (bNodeContextRequested)
    {
        GraphContextStateId = static_cast<uint32>(ContextNodeId.Get());
        SelectStateNode(GraphContextStateId);
        ImGui::OpenPopup("AnimSMNodeContextMenu");
        bOpenedCustomContextMenu = true;
    }

    if (!bOpenedCustomContextMenu && bGraphMouseRightReleased)
    {
        const FAnimTransitionId ContextTransitionId =
            PickTransitionAtScreenPosition(TransitionSegments, GraphMouseScreenPosition);
        if (ContextTransitionId != InvalidAnimTransitionId)
        {
            GraphContextTransitionId = ContextTransitionId;
            ed::ClearSelection();
            GraphViewer->SetSelectedTransitionId(ContextTransitionId);
            GraphViewer->SetSelectedStateId(InvalidAnimStateId);
            ImGui::OpenPopup("AnimSMTransitionContextMenu");
            bOpenedCustomContextMenu = true;
        }
    }

    if (!bOpenedCustomContextMenu && bBackgroundContextRequested)
    {
        ImGui::OpenPopup("AnimSMCanvasContextMenu");
    }

    if (ImGui::BeginPopup("AnimSMCanvasContextMenu"))
    {
        if (ImGui::MenuItem("Add State"))
        {
            AddStateAtPosition(GraphNewStateNameBuffer, GraphMouseCanvasPosition);
        }
        if (ImGui::MenuItem("Fit View"))
        {
            ed::NavigateToContent(0.0f);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AnimSMNodeContextMenu"))
    {
        const FAnimStateDesc* ContextState = Asset->FindStateById(GraphContextStateId);
        const bool bHasContextState = ContextState != nullptr;
        const bool bContextIsEntry = bHasContextState && ContextState->Id == Asset->GetEntryStateId();
        ImGui::BeginDisabled(!bHasContextState);
        if (ImGui::MenuItem("Select"))
        {
            SelectStateNode(GraphContextStateId);
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!bHasContextState || Asset->GetParameters().empty());
        if (ImGui::BeginMenu("Add Transition"))
        {
            bool bHasTargetState = false;
            for (const FAnimStateDesc& TargetState : Asset->GetStates())
            {
                if (TargetState.Id == GraphContextStateId)
                {
                    continue;
                }

                bHasTargetState = true;
                if (ImGui::MenuItem(TargetState.StateName.ToString().c_str()))
                {
                    CreateDefaultTransition(GraphContextStateId, TargetState.Id);
                }
            }
            if (!bHasTargetState)
            {
                ImGui::TextDisabled("No target states");
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();
        if (Asset->GetParameters().empty())
        {
            ImGui::TextDisabled("Add a parameter first");
        }

        ImGui::BeginDisabled(!bHasContextState || bContextIsEntry);
        if (ImGui::MenuItem("Set Entry"))
        {
            if (Asset->SetEntryStateId(GraphContextStateId))
            {
                GraphViewer->MarkDirty();
            }
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!bHasContextState || bContextIsEntry || Asset->GetStates().size() <= 1);
        if (ImGui::MenuItem("Delete State"))
        {
            if (Asset->RemoveState(GraphContextStateId))
            {
                GraphViewer->SetSelectedStateId(InvalidAnimStateId);
                GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
                GraphViewer->MarkDirty();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AnimSMTransitionContextMenu"))
    {
        const FAnimTransitionDesc* ContextTransition = Asset->FindTransitionById(GraphContextTransitionId);
        ImGui::BeginDisabled(!ContextTransition);
        if (ImGui::MenuItem("Select"))
        {
            GraphViewer->SetSelectedTransitionId(GraphContextTransitionId);
            GraphViewer->SetSelectedStateId(InvalidAnimStateId);
        }
        if (ImGui::MenuItem("Delete Transition"))
        {
            if (Asset->RemoveTransition(GraphContextTransitionId))
            {
                GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
                GraphViewer->MarkDirty();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    bool bClickedCustomTransition = false;
    FAnimTransitionId ClickedTransitionId = InvalidAnimTransitionId;
    if (bGraphMouseClicked)
    {
        ClickedTransitionId = PickTransitionAtScreenPosition(TransitionSegments, GraphMouseScreenPosition);
        bClickedCustomTransition = ClickedTransitionId != InvalidAnimTransitionId;
    }

    if (bClickedCustomTransition)
    {
        ed::ClearSelection();
        GraphViewer->SetSelectedTransitionId(ClickedTransitionId);
        GraphViewer->SetSelectedStateId(InvalidAnimStateId);
    }
    else if (bGraphMouseClicked && ed::IsBackgroundClicked())
    {
        GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
        GraphViewer->SetSelectedStateId(InvalidAnimStateId);
    }

    for (const FAnimStateDesc& State : Asset->GetStates())
    {
        const ImVec2 NodePosition = ed::GetNodePosition(MakeStateNodeId(State.Id));
        if (GraphViewer->UpdateObservedNodePosition(State.Id, NodePosition.x, NodePosition.y))
        {
            if (Asset->SetStateEditorPosition(State.Id, NodePosition.x, NodePosition.y))
            {
                GraphViewer->MarkDirty();
            }
        }
    }

    if (GraphViewer->IsFitToViewPending())
    {
        ed::NavigateToContent(0.0f);
        GraphViewer->MarkFitToViewDone();
    }

    if (!bClickedCustomTransition && ed::HasSelectionChanged())
    {
        ed::NodeId SelectedNodes[1] = { ed::NodeId(0) };
        if (ed::GetSelectedNodes(SelectedNodes, 1) > 0)
        {
            const FAnimStateId SelectedStateId = static_cast<uint32>(SelectedNodes[0].Get());
            GraphViewer->SetSelectedStateId(SelectedStateId);
            GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
        }
        else if (GraphViewer->GetSelectedTransitionId() == InvalidAnimTransitionId)
        {
            GraphViewer->SetSelectedStateId(InvalidAnimStateId);
            GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
        }
    }

    ed::PopStyleColor(2);
    ed::SetCurrentEditor(nullptr);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("AnimStateMachineInspector", ImVec2(InspectorWidth, 0), true);
    ImGui::TextUnformatted("State Machine");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", GraphViewer->IsDirty() ? "dirty" : "saved");
    ImGui::Separator();
    if (ImGui::Button(GraphViewer->IsDirty() ? "Save *" : "Save", ImVec2(90.0f, 0.0f)))
    {
        RequestSaveGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate", ImVec2(90.0f, 0.0f)))
    {
        RequestValidateGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit", ImVec2(70.0f, 0.0f)))
    {
        ed::SetCurrentEditor(GraphViewer->GetNodeEditorContext());
        ed::NavigateToContent(0.0f);
        ed::SetCurrentEditor(nullptr);
    }
    ImGui::Spacing();
    ImGui::Text("States: %zu", Asset->GetStates().size());
    ImGui::Text("Transitions: %zu", Asset->GetTransitions().size());
    ImGui::Text("Parameters: %zu", Asset->GetParameters().size());
    ImGui::Text("Entry: %s", Asset->GetEntryState().ToString().c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("State Commands");
    ImGui::InputTextWithHint("##NewStateName", "New State", GraphNewStateNameBuffer, sizeof(GraphNewStateNameBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Add State"))
    {
        const FString NewStateName = MakeUniqueStateName(Asset, GraphNewStateNameBuffer);
        if (Asset->AddState(FName(NewStateName), FName(), true))
        {
            const FAnimStateDesc* NewState = Asset->FindState(FName(NewStateName));
            if (NewState)
            {
                const float PositionX = static_cast<float>(Asset->GetStates().size() % 4) * 240.0f;
                const float PositionY = static_cast<float>(Asset->GetStates().size() / 4) * 140.0f;
                Asset->SetStateEditorPosition(NewState->Id, PositionX, PositionY);

                ed::SetCurrentEditor(GraphViewer->GetNodeEditorContext());
                ed::SetNodePosition(MakeStateNodeId(NewState->Id), ImVec2(PositionX, PositionY));
                ed::ClearSelection();
                ed::SelectNode(MakeStateNodeId(NewState->Id));
                ed::SetCurrentEditor(nullptr);

                GraphViewer->SetSelectedStateId(NewState->Id);
                GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
                GraphViewer->MarkDirty();
                CopyToBuffer(GraphNewStateNameBuffer, sizeof(GraphNewStateNameBuffer), MakeUniqueStateName(Asset, "New State"));
            }
        }
    }

    if (!GraphViewer->GetValidationMessage().empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.30f, 1.0f), "Validation");
        ImGui::TextWrapped("%s", GraphViewer->GetValidationMessage().c_str());
    }

    if (const FAnimStateDesc* SelectedState = Asset->FindStateById(GraphViewer->GetSelectedStateId()))
    {
        if (GraphInspectorStateBufferId != SelectedState->Id)
        {
            GraphInspectorStateBufferId = SelectedState->Id;
            CopyToBuffer(GraphStateNameBuffer, sizeof(GraphStateNameBuffer), SelectedState->StateName.ToString());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Selected State");
        ImGui::Text("Id: %u", SelectedState->Id);
        ImGui::InputText("Name", GraphStateNameBuffer, sizeof(GraphStateNameBuffer));

        const FString CandidateName = GraphStateNameBuffer;
        const bool bCanRename =
            !CandidateName.empty() &&
            !IsDuplicateStateName(Asset, CandidateName, SelectedState->Id) &&
            CandidateName != SelectedState->StateName.ToString();
        ImGui::BeginDisabled(!bCanRename);
        if (ImGui::Button("Apply Name"))
        {
            if (Asset->RenameState(SelectedState->Id, FName(CandidateName)))
            {
                GraphViewer->MarkDirty();
            }
        }
        ImGui::EndDisabled();
        if (CandidateName.empty())
        {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.30f, 1.0f), "State name is empty");
        }
        else if (IsDuplicateStateName(Asset, CandidateName, SelectedState->Id))
        {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.30f, 1.0f), "Duplicate state name");
        }

        ImGui::Spacing();
        bool bLoop = SelectedState->bLoop;
        if (ImGui::Checkbox("Loop", &bLoop))
        {
            if (Asset->SetStateLoop(SelectedState->Id, bLoop))
            {
                GraphViewer->MarkDirty();
            }
        }

        const FString CurrentAnimation = SelectedState->AnimationPath.empty()
            ? SelectedState->AnimationName.ToString()
            : SelectedState->AnimationPath;
        if (ImGui::BeginCombo("Animation", CurrentAnimation.empty() ? "<None>" : CurrentAnimation.c_str()))
        {
            const TArray<FString>& AnimationPaths = EditorEngine->GetAssetService().GetAnimationSequenceAssetPaths();
            if (AnimationPaths.empty())
            {
                ImGui::TextDisabled("No animation sequences");
            }
            for (const FString& AnimationPath : AnimationPaths)
            {
                const bool bSelected = AnimationPath == CurrentAnimation;
                if (ImGui::Selectable(AnimationPath.c_str(), bSelected))
                {
                    if (Asset->SetStateAnimationPathById(SelectedState->Id, AnimationPath))
                    {
                        GraphViewer->MarkDirty();
                    }
                }
                if (bSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const bool bIsEntry = SelectedState->Id == Asset->GetEntryStateId();
        ImGui::BeginDisabled(bIsEntry);
        if (ImGui::Button("Set Entry"))
        {
            if (Asset->SetEntryStateId(SelectedState->Id))
            {
                GraphViewer->MarkDirty();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(bIsEntry || Asset->GetStates().size() <= 1);
        if (ImGui::Button("Delete State"))
        {
            const FAnimStateId RemovedStateId = SelectedState->Id;
            if (Asset->RemoveState(RemovedStateId))
            {
                GraphViewer->SetSelectedStateId(InvalidAnimStateId);
                GraphInspectorStateBufferId = InvalidAnimStateId;
                GraphViewer->MarkDirty();
            }
        }
        ImGui::EndDisabled();
        if (bIsEntry)
        {
            ImGui::TextDisabled("Entry state cannot be deleted");
        }
        else if (Asset->GetStates().size() <= 1)
        {
            ImGui::TextDisabled("Last state cannot be deleted");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Create Transition");
        if (GraphTransitionTargetStateId == InvalidAnimStateId || !Asset->FindStateById(GraphTransitionTargetStateId))
        {
            for (const FAnimStateDesc& State : Asset->GetStates())
            {
                if (State.Id != SelectedState->Id)
                {
                    GraphTransitionTargetStateId = State.Id;
                    break;
                }
            }
        }

        const FAnimStateDesc* TargetState = Asset->FindStateById(GraphTransitionTargetStateId);
        if (ImGui::BeginCombo("Target", TargetState ? TargetState->StateName.ToString().c_str() : "<None>"))
        {
            for (const FAnimStateDesc& State : Asset->GetStates())
            {
                if (State.Id == SelectedState->Id)
                {
                    continue;
                }

                const bool bSelected = State.Id == GraphTransitionTargetStateId;
                if (ImGui::Selectable(State.StateName.ToString().c_str(), bSelected))
                {
                    GraphTransitionTargetStateId = State.Id;
                }
                if (bSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const FAnimStateMachineParameterDesc* TransitionParameter = GetParameterByIndex(Asset, GraphTransitionParameterIndex);
        if (!TransitionParameter && !Asset->GetParameters().empty())
        {
            GraphTransitionParameterIndex = 0;
            TransitionParameter = GetParameterByIndex(Asset, GraphTransitionParameterIndex);
        }
        if (ImGui::BeginCombo("Condition Parameter", TransitionParameter ? TransitionParameter->Name.ToString().c_str() : "<None>"))
        {
            for (int32 Index = 0; Index < static_cast<int32>(Asset->GetParameters().size()); ++Index)
            {
                const FAnimStateMachineParameterDesc& Parameter = Asset->GetParameters()[Index];
                const bool bSelected = Index == GraphTransitionParameterIndex;
                if (ImGui::Selectable(Parameter.Name.ToString().c_str(), bSelected))
                {
                    GraphTransitionParameterIndex = Index;
                    GraphTransitionCompareOpIndex = 0;
                }
                if (bSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (TransitionParameter)
        {
            TArray<EAnimCompareOp> Ops = GetCompareOpsForParameterType(TransitionParameter->Type);
            const EAnimCompareOp CurrentOp = GetCompareOpFromIndex(TransitionParameter->Type, GraphTransitionCompareOpIndex);
            if (ImGui::BeginCombo("Compare", CompareOpToString(CurrentOp).c_str()))
            {
                for (int32 Index = 0; Index < static_cast<int32>(Ops.size()); ++Index)
                {
                    const bool bSelected = Ops[Index] == CurrentOp;
                    if (ImGui::Selectable(CompareOpToString(Ops[Index]).c_str(), bSelected))
                    {
                        GraphTransitionCompareOpIndex = Index;
                    }
                    if (bSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            switch (TransitionParameter->Type)
            {
            case EAnimParameterType::Bool:
                ImGui::Checkbox("Condition Value", &GraphTransitionBoolValue);
                break;
            case EAnimParameterType::Int:
                ImGui::InputInt("Condition Value", &GraphTransitionIntValue);
                break;
            case EAnimParameterType::Float:
                ImGui::InputFloat("Condition Value", &GraphTransitionFloatValue, 0.05f, 0.25f, "%.3f");
                break;
            case EAnimParameterType::Vector:
                ImGui::InputFloat3("Condition Value", GraphTransitionVectorValue, "%.3f");
                break;
            case EAnimParameterType::Trigger:
                ImGui::TextDisabled("Trigger uses IsTrue");
                break;
            default:
                break;
            }
        }

        static const char* EaseLabels[] = { "Linear", "EaseIn", "EaseOut", "EaseInOut" };
        ImGui::InputFloat("Blend Time", &GraphTransitionBlendTime, 0.05f, 0.25f, "%.3f");
        GraphTransitionBlendTime = std::max(0.0f, GraphTransitionBlendTime);
        ImGui::InputInt("Priority", &GraphTransitionPriority);
        ImGui::Combo("Ease", &GraphTransitionEaseIndex, EaseLabels, IM_ARRAYSIZE(EaseLabels));

        const bool bCanCreateTransition =
            TargetState &&
            TransitionParameter &&
            TargetState->Id != SelectedState->Id;
        ImGui::BeginDisabled(!bCanCreateTransition);
        if (ImGui::Button("Create Transition"))
        {
            TArray<FAnimTransitionCondition> Conditions;
            Conditions.push_back(BuildConditionFromUi(
                *TransitionParameter,
                GraphTransitionCompareOpIndex,
                GraphTransitionBoolValue,
                GraphTransitionIntValue,
                GraphTransitionFloatValue,
                GraphTransitionVectorValue));

            if (Asset->AddTransition(
                SelectedState->StateName,
                TargetState->StateName,
                Conditions,
                GraphTransitionBlendTime,
                GraphTransitionPriority,
                EaseOptionFromIndex(GraphTransitionEaseIndex)))
            {
                GraphViewer->MarkDirty();
            }
        }
        ImGui::EndDisabled();
    }

    if (const FAnimTransitionDesc* SelectedTransition = Asset->FindTransitionById(GraphViewer->GetSelectedTransitionId()))
    {
        if (GraphInspectorTransitionBufferId != SelectedTransition->Id)
        {
            GraphInspectorTransitionBufferId = SelectedTransition->Id;
            GraphTransitionFromStateId = SelectedTransition->FromStateId;
            GraphTransitionToStateId = SelectedTransition->ToStateId;
            GraphTransitionBlendTime = SelectedTransition->BlendTime;
            GraphTransitionPriority = SelectedTransition->Priority;
            GraphTransitionEaseIndex = EaseOptionToIndex(SelectedTransition->EaseOption);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Selected Transition");
        ImGui::Text("Id: %u", SelectedTransition->Id);
        if (ImGui::Button("Delete Transition"))
        {
            const FAnimTransitionId RemovedTransitionId = SelectedTransition->Id;
            if (Asset->RemoveTransition(RemovedTransitionId))
            {
                GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
                GraphInspectorTransitionBufferId = InvalidAnimTransitionId;
                GraphViewer->MarkDirty();
            }
        }

        SelectedTransition = Asset->FindTransitionById(GraphViewer->GetSelectedTransitionId());
        if (SelectedTransition)
        {
            const FAnimStateDesc* FromState = Asset->FindStateById(GraphTransitionFromStateId);
            const FAnimStateDesc* ToState = Asset->FindStateById(GraphTransitionToStateId);
            if (ImGui::BeginCombo("From", FromState ? FromState->StateName.ToString().c_str() : "<None>"))
            {
                for (const FAnimStateDesc& State : Asset->GetStates())
                {
                    const bool bSelected = State.Id == GraphTransitionFromStateId;
                    if (ImGui::Selectable(State.StateName.ToString().c_str(), bSelected))
                    {
                        GraphTransitionFromStateId = State.Id;
                    }
                    if (bSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("To", ToState ? ToState->StateName.ToString().c_str() : "<None>"))
            {
                for (const FAnimStateDesc& State : Asset->GetStates())
                {
                    const bool bSelected = State.Id == GraphTransitionToStateId;
                    if (ImGui::Selectable(State.StateName.ToString().c_str(), bSelected))
                    {
                        GraphTransitionToStateId = State.Id;
                    }
                    if (bSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            const bool bCanReconnect =
                GraphTransitionFromStateId != InvalidAnimStateId &&
                GraphTransitionToStateId != InvalidAnimStateId &&
                (GraphTransitionFromStateId != SelectedTransition->FromStateId ||
                 GraphTransitionToStateId != SelectedTransition->ToStateId);
            ImGui::BeginDisabled(!bCanReconnect);
            if (ImGui::Button("Apply Reconnect"))
            {
                if (Asset->ReconnectTransition(
                    SelectedTransition->Id,
                    GraphTransitionFromStateId,
                    GraphTransitionToStateId))
                {
                    GraphViewer->MarkDirty();
                }
            }
            ImGui::EndDisabled();

            if (ImGui::InputFloat("Blend", &GraphTransitionBlendTime, 0.05f, 0.25f, "%.3f"))
            {
                GraphTransitionBlendTime = std::max(0.0f, GraphTransitionBlendTime);
                if (Asset->UpdateTransition(
                    SelectedTransition->Id,
                    GraphTransitionBlendTime,
                    GraphTransitionPriority,
                    EaseOptionFromIndex(GraphTransitionEaseIndex)))
                {
                    GraphViewer->MarkDirty();
                }
            }
            if (ImGui::InputInt("Transition Priority", &GraphTransitionPriority))
            {
                if (Asset->UpdateTransition(
                    SelectedTransition->Id,
                    GraphTransitionBlendTime,
                    GraphTransitionPriority,
                    EaseOptionFromIndex(GraphTransitionEaseIndex)))
                {
                    GraphViewer->MarkDirty();
                }
            }
            static const char* EaseLabels[] = { "Linear", "EaseIn", "EaseOut", "EaseInOut" };
            if (ImGui::Combo("Transition Ease", &GraphTransitionEaseIndex, EaseLabels, IM_ARRAYSIZE(EaseLabels)))
            {
                if (Asset->UpdateTransition(
                    SelectedTransition->Id,
                    GraphTransitionBlendTime,
                    GraphTransitionPriority,
                    EaseOptionFromIndex(GraphTransitionEaseIndex)))
                {
                    GraphViewer->MarkDirty();
                }
            }

            SelectedTransition = Asset->FindTransitionById(GraphViewer->GetSelectedTransitionId());
            if (SelectedTransition)
            {
                ImGui::Spacing();
                ImGui::Text("Conditions: %zu", SelectedTransition->Conditions.size());
                for (int32 ConditionIndex = 0; ConditionIndex < static_cast<int32>(SelectedTransition->Conditions.size()); ++ConditionIndex)
                {
                    const FAnimTransitionCondition& SourceCondition = SelectedTransition->Conditions[ConditionIndex];
                    TArray<FAnimTransitionCondition> Conditions = SelectedTransition->Conditions;
                    FAnimTransitionCondition& EditableCondition = Conditions[ConditionIndex];
                    const FAnimStateMachineParameterDesc* ConditionParameter = Asset->FindParameter(EditableCondition.ParameterName);
                    ImGui::PushID(ConditionIndex);
                    ImGui::Spacing();
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.13f, 0.15f, 0.78f));
                    ImGui::BeginChild("ConditionBlock", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
                    ImGui::Text("Condition %d", ConditionIndex + 1);

                    int32 ConditionParameterIndex = FindParameterIndex(Asset, EditableCondition.ParameterName);
                    const FString CurrentParameterLabel = ConditionParameter ? ConditionParameter->Name.ToString() : FString("<Missing>");
                    if (ImGui::BeginCombo("Parameter", CurrentParameterLabel.c_str()))
                    {
                        for (int32 ParameterIndex = 0; ParameterIndex < static_cast<int32>(Asset->GetParameters().size()); ++ParameterIndex)
                        {
                            const FAnimStateMachineParameterDesc& Parameter = Asset->GetParameters()[ParameterIndex];
                            const bool bSelected = ParameterIndex == ConditionParameterIndex;
                            if (ImGui::Selectable(Parameter.Name.ToString().c_str(), bSelected))
                            {
                                EditableCondition.ParameterName = Parameter.Name;
                                EditableCondition.CompareOp = GetCompareOpFromIndex(Parameter.Type, 0);
                                EditableCondition.CompareValue = FAnimParameterValue();
                                if (Asset->SetTransitionConditions(SelectedTransition->Id, Conditions))
                                {
                                    GraphViewer->MarkDirty();
                                }
                            }
                            if (bSelected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ConditionParameter = Asset->FindParameter(EditableCondition.ParameterName);
                    if (ConditionParameter)
                    {
                        TArray<EAnimCompareOp> Ops = GetCompareOpsForParameterType(ConditionParameter->Type);
                        int32 CompareIndex = GetCompareOpIndex(ConditionParameter->Type, EditableCondition.CompareOp);
                        if (ImGui::BeginCombo("Compare", CompareOpToString(EditableCondition.CompareOp).c_str()))
                        {
                            for (int32 OpIndex = 0; OpIndex < static_cast<int32>(Ops.size()); ++OpIndex)
                            {
                                const bool bSelected = Ops[OpIndex] == EditableCondition.CompareOp;
                                if (ImGui::Selectable(CompareOpToString(Ops[OpIndex]).c_str(), bSelected))
                                {
                                    CompareIndex = OpIndex;
                                    EditableCondition.CompareOp = Ops[OpIndex];
                                    if (Asset->SetTransitionConditions(SelectedTransition->Id, Conditions))
                                    {
                                        GraphViewer->MarkDirty();
                                    }
                                }
                                if (bSelected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }

                        FAnimParameterValue PreviousValue = EditableCondition.CompareValue;
                        DrawConditionValueEditor("Condition", ConditionParameter->Type, EditableCondition.CompareValue);
                        if (PreviousValue.BoolValue != EditableCondition.CompareValue.BoolValue ||
                            PreviousValue.IntValue != EditableCondition.CompareValue.IntValue ||
                            PreviousValue.FloatValue != EditableCondition.CompareValue.FloatValue ||
                            PreviousValue.VectorValue != EditableCondition.CompareValue.VectorValue)
                        {
                            if (Asset->SetTransitionConditions(SelectedTransition->Id, Conditions))
                            {
                                GraphViewer->MarkDirty();
                            }
                        }
                    }

                    if (ImGui::Button("Delete Condition"))
                    {
                        if (Conditions.size() <= 1)
                        {
                            const bool bDeleteTransition = MessageBoxW(
                                nullptr,
                                L"Deleting the last condition will remove the transition.",
                                L"Delete Transition",
                                MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
                            if (bDeleteTransition && Asset->RemoveTransition(SelectedTransition->Id))
                            {
                                GraphViewer->SetSelectedTransitionId(InvalidAnimTransitionId);
                                GraphInspectorTransitionBufferId = InvalidAnimTransitionId;
                                GraphViewer->MarkDirty();
                            }
                        }
                        else
                        {
                            Conditions.erase(Conditions.begin() + ConditionIndex);
                            if (Asset->SetTransitionConditions(SelectedTransition->Id, Conditions))
                            {
                                GraphViewer->MarkDirty();
                            }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar(2);
                    ImGui::PopID();

                    if (!Asset->FindTransitionById(GraphViewer->GetSelectedTransitionId()))
                    {
                        break;
                    }
                }

                SelectedTransition = Asset->FindTransitionById(GraphViewer->GetSelectedTransitionId());
                if (SelectedTransition)
                {
                    ImGui::Spacing();
                    const FAnimStateMachineParameterDesc* DefaultConditionParameter = GetParameterByIndex(Asset, 0);
                    ImGui::BeginDisabled(!DefaultConditionParameter);
                    if (ImGui::Button("Add Condition"))
                    {
                        TArray<FAnimTransitionCondition> Conditions = SelectedTransition->Conditions;
                        Conditions.push_back(BuildDefaultConditionForParameter(*DefaultConditionParameter));
                        if (Asset->SetTransitionConditions(SelectedTransition->Id, Conditions))
                        {
                            GraphViewer->MarkDirty();
                        }
                    }
                    ImGui::EndDisabled();
                    if (!DefaultConditionParameter)
                    {
                        ImGui::TextDisabled("Add a parameter first");
                    }
                }
            }
        }
    }
    ImGui::EndChild();
}

void FEditorViewerWindowWidget::RenderSkeletalMeshContent(float DeltaTime)
{
    (void)DeltaTime;

    FSceneViewport& SceneViewport = Viewer->GetViewport();
    FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);

    ID3D11ShaderResourceView* SRV = SceneViewport.GetOutSRV();

    if (!SRV)
    {
        ImGui::TextDisabled("Viewer render target is not ready.");
        return;
    }

    ImVec2 FullSize = ImGui::GetContentRegionAvail();

    float CenterWidth = std::max(160.0f, FullSize.x - LeftPanelWidth - RightPanelWidth - (ImGui::GetStyle().ItemSpacing.x * 2.0f));

    // =====================================================
    // LEFT: Skeleton Tree
    // =====================================================
    ImGui::BeginChild("SkeletonPanel", ImVec2(LeftPanelWidth, 0), true);

    ImGui::Text("Skeleton");

    ASkeletalMeshActor* ViewTarget = SkeletalViewer ? SkeletalViewer->GetViewTarget() : nullptr;
    USkeletalMeshComponent* SkelMeshComp = ViewTarget ? ViewTarget->GetSkeletalMeshComponent() : nullptr;
    USkeletalMesh* SkeletalMesh = SkelMeshComp ? SkelMeshComp->GetSkeletalMesh() : nullptr;
    FSkeletalMesh* MeshData = SkeletalMesh ? SkeletalMesh->GetMeshData() : nullptr;

    // 헬퍼들이 참조할 transient 캐시 (Render 호출 범위에서만 유효)
    CachedSkComp = SkelMeshComp;

    if (!MeshData)
    {
        CachedMesh = nullptr;
        Children.clear();
        BoneToSocketIndices.clear();
        if (SkeletalViewer)
        {
            SkeletalViewer->ClearSelection();
        }
        ResetMeshDirtyBaseline();
        ImGui::TextDisabled("No skeletal mesh");
    }
    else if (CachedMesh != MeshData)
    {
        CachedMesh = MeshData;
        if (SkeletalViewer)
        {
            SkeletalViewer->ClearSelection();
        }

        RebuildBoneTreeCaches(MeshData);
        ResetMeshDirtyBaseline();
    }

    if (MeshData)
    {
        ApplyPendingBoneTreeOpenState(MeshData);
        for (int32 j = 0; j < MeshData->Bones.size(); ++j)
        {
            if (MeshData->Bones[j].ParentIndex == -1)
            {
                DrawBoneNode(j, MeshData->Bones, Children);
            }
        }
    }

    // 컨텍스트 메뉴에서 PendingPreviewPickerSocketIdx가 셋되면 모달 오픈.
    // (BeginPopupContextItem 안에서 OpenPopup하지 않고 *바깥*에서 하는 것이 안전 — popup 스택 충돌 방지)
    if (PendingPreviewPickerSocketIdx >= 0 && !ImGui::IsPopupOpen("PickStaticMesh"))
    {
        ImGui::OpenPopup("PickStaticMesh");
    }
    DrawPreviewPickerModal();

    if (RenameSocketIdx >= 0 && !ImGui::IsPopupOpen("RenameSocket"))
    {
        ImGui::OpenPopup("RenameSocket");
    }
    DrawRenameModal();

    // 선택된 socket의 properties 편집기 + Save 버튼 (좌측 패널 하단)
    ImGui::Separator();
    DrawSocketInspector();

    ImGui::EndChild();

    // Left Splitter
    ImGui::SameLine();
    ImGui::Button("##left_splitter", ImVec2(2.0f, -1.0f));
    if (ImGui::IsItemActive())
    {
        LeftPanelWidth += ImGui::GetIO().MouseDelta.x;
        LeftPanelWidth = std::clamp(LeftPanelWidth, 100.0f, FullSize.x * 0.4f);
    }
    ImGui::SameLine();

    // =====================================================
    // CENTER: Viewport
    // =====================================================

    ImGui::BeginChild("ViewportPanel", ImVec2(CenterWidth, 0), false);

    ImVec2 Size = ImGui::GetContentRegionAvail();

    Size.x = std::max(Size.x, 1.0f);
    Size.y = std::max(Size.y, 1.0f);

    ImGui::Dummy(Size);
    ImVec2 Min = ImGui::GetItemRectMin();
    ImVec2 Max = ImGui::GetItemRectMax();
    const POINT ClientMin = ImGuiScreenToClientPoint(EditorEngine ? EditorEngine->GetWindow() : nullptr, Min);
    const bool bViewportHovered = ImGui::IsItemHovered();
    const bool bViewportClicked =
        bViewportHovered &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Middle));

    FViewportRect NewRect;
    NewRect.X = (int32)ClientMin.x;
    NewRect.Y = (int32)ClientMin.y;
    NewRect.Width = (int32)(Max.x - Min.x);
    NewRect.Height = (int32)(Max.y - Min.y);

    SceneViewport.SetRect(NewRect);

    if (auto* Client = SceneViewport.GetClient())
    {
        Client->SetViewportSize((float)NewRect.Width, (float)NewRect.Height);
    }
    if (bViewportClicked)
    {
        EditorEngine->FocusViewportInput(&SceneViewport);
    }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    ID3D11DeviceContext* DC =
        EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();

    DrawList->AddCallback(SetOpaqueBlendStateCallback, DC);

    // Render viewport
    DrawList->AddImage((ImTextureID)SRV, Min, Max);

    DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    ImGui::EndChild();

    // Right Splitter
    ImGui::SameLine();
    ImGui::Button("##right_splitter", ImVec2(2.0f, -1.0f));
    if (ImGui::IsItemActive())
    {
        RightPanelWidth -= ImGui::GetIO().MouseDelta.x;
        const float MaxRightPanelWidth = std::max(100.0f, FullSize.x * 0.4f);
        RightPanelWidth = std::clamp(RightPanelWidth, 100.0f, MaxRightPanelWidth);
    }

    ImGui::SameLine();

    // =====================================================
    // RIGHT: Bone Details
    // =====================================================
    ImGui::BeginChild("BoneDetailsPanel", ImVec2(RightPanelWidth, 0), true);
    ImGui::Text("Details");
    ImGui::Separator();
    const int32 SelectedBoneIndex = SkeletalViewer ? SkeletalViewer->GetSelectedBoneIndex() : -1;
    const int32 SelectedSocketIndex = SkeletalViewer ? SkeletalViewer->GetSelectedSocketIndex() : -1;
    if (SelectedBoneIndex != -1 && SkelMeshComp)
    {
        RenderBoneDetails(SkelMeshComp);
    }
    else if (SelectedSocketIndex != -1 && SkelMeshComp)
    {
        // Socket details (Location, Rotation, Scale already in Left Panel, but showing something here is good)
        if (CachedMesh && SelectedSocketIndex < (int32)CachedMesh->Sockets.size())
        {
            ImGui::Text("Socket: %s", CachedMesh->Sockets[SelectedSocketIndex].Name.ToString().c_str());
            ImGui::Separator();
            ImGui::Text("Selected Socket for transformation.");
        }
    }
    else
    {
        ImGui::TextDisabled("No bone or socket selected.");
    }
    ImGui::EndChild();
}

void FEditorViewerWindowWidget::RenderAnimationContent(float DeltaTime)
{
    (void)DeltaTime;

    FSceneViewport& SceneViewport = Viewer->GetViewport();
    FAnimationViewer* AnimationViewer = AsAnimationViewer(Viewer);

    ID3D11ShaderResourceView* SRV = SceneViewport.GetOutSRV();

    if (!SRV)
    {
        ImGui::TextDisabled("Viewer render target is not ready.");
        return;
    }

    ImVec2 FullSize = ImGui::GetContentRegionAvail();

    const float TimelineSplitterHeight = AnimationViewer ? 5.0f : 0.0f;
    auto ClampTimelinePanelHeight = [&]()
    {
        const float VerticalSpacing = ImGui::GetStyle().ItemSpacing.y * 2.0f;
        const float MaxTimelineHeight = std::max(
            120.0f,
            FullSize.y - 180.0f - TimelineSplitterHeight - VerticalSpacing);
        const float MinTimelineHeight = std::min(220.0f, MaxTimelineHeight);
        DownPanelHeight = std::clamp(DownPanelHeight, MinTimelineHeight, MaxTimelineHeight);
    };

    if (AnimationViewer)
    {
        ClampTimelinePanelHeight();
    }

    float CenterWidth = std::max(160.0f, FullSize.x - RightPanelWidth - (ImGui::GetStyle().ItemSpacing.x * 2.0f));
    float CenterHeight = std::max(180.0f, FullSize.y - DownPanelHeight - TimelineSplitterHeight - (ImGui::GetStyle().ItemSpacing.y * 2.0f));
    CenterHeight = AnimationViewer ? CenterHeight : FullSize.y;

    // =====================================================
    // CENTER: Viewport
    // =====================================================

    ImGui::BeginChild("CenterPanel", ImVec2(CenterWidth, 0), false);
    {
        ImGui::BeginChild("ViewportPanel", ImVec2(0, CenterHeight), false);

        ImVec2 Size = ImGui::GetContentRegionAvail();

        Size.x = std::max(Size.x, 1.0f);
        Size.y = std::max(Size.y, 1.0f);

        ImGui::Dummy(Size);
        ImVec2 Min = ImGui::GetItemRectMin();
        ImVec2 Max = ImGui::GetItemRectMax();
        const POINT ClientMin = ImGuiScreenToClientPoint(EditorEngine ? EditorEngine->GetWindow() : nullptr, Min);
        const bool bViewportHovered = ImGui::IsItemHovered();
        const bool bViewportClicked =
            bViewportHovered &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Middle));

        FViewportRect NewRect;
        NewRect.X = (int32)ClientMin.x;
        NewRect.Y = (int32)ClientMin.y;
        NewRect.Width = (int32)(Max.x - Min.x);
        NewRect.Height = (int32)(Max.y - Min.y);

        SceneViewport.SetRect(NewRect);

        if (auto* Client = SceneViewport.GetClient())
        {
            Client->SetViewportSize((float)NewRect.Width, (float)NewRect.Height);
        }
        if (bViewportClicked)
        {
            EditorEngine->FocusViewportInput(&SceneViewport);
        }

        ImDrawList* DrawList = ImGui::GetWindowDrawList();

        ID3D11DeviceContext* DC =
            EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();

        DrawList->AddCallback(SetOpaqueBlendStateCallback, DC);
        // Render viewport
        DrawList->AddImage((ImTextureID)SRV, Min, Max);
        DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

        if (AnimationViewer)
        {
            const FString& AnimationPath = AnimationViewer->GetAnimationSequencePath();
            const FString AnimationLabel = AnimationPath.empty()
                ? FString("No Animation")
                : GetBaseFileNameWithoutExtension(AnimationPath);
            const float CurrentTime = AnimationViewer->GetAnimationTime();
            const float Duration = AnimationViewer->GetAnimationLength();
            const bool bPlaying = AnimationViewer->IsAnimationPlaying() && !AnimationViewer->IsAnimationPaused();

            char TimeBuffer[64];
            std::snprintf(TimeBuffer, sizeof(TimeBuffer), "%.2fs / %.2fs", CurrentTime, Duration);

            const ImVec2 OverlayMin(Min.x + 14.0f, Min.y + 14.0f);
            const ImVec2 OverlayMax(OverlayMin.x + 260.0f, OverlayMin.y + 58.0f);
            DrawList->AddRectFilled(OverlayMin, OverlayMax, IM_COL32(8, 11, 16, 186), 7.0f);
            DrawList->AddRect(OverlayMin, OverlayMax, IM_COL32(78, 91, 110, 190), 7.0f);
            DrawList->AddCircleFilled(ImVec2(OverlayMin.x + 18.0f, OverlayMin.y + 18.0f), 5.0f,
                bPlaying ? IM_COL32(96, 220, 150, 255) : IM_COL32(170, 180, 192, 255), 16);
            DrawList->AddText(ImVec2(OverlayMin.x + 32.0f, OverlayMin.y + 9.0f), IM_COL32(235, 240, 248, 255), AnimationLabel.c_str());
            DrawList->AddText(ImVec2(OverlayMin.x + 32.0f, OverlayMin.y + 31.0f), IM_COL32(150, 162, 178, 255), TimeBuffer);

            const float NormalizedTime = Duration > 0.0f ? std::clamp(CurrentTime / Duration, 0.0f, 1.0f) : 0.0f;
            const ImVec2 BarMin(Min.x + 18.0f, Max.y - 22.0f);
            const ImVec2 BarMax(Max.x - 18.0f, Max.y - 16.0f);
            DrawList->AddRectFilled(BarMin, BarMax, IM_COL32(10, 13, 18, 210), 3.0f);
            DrawList->AddRectFilled(BarMin, ImVec2(BarMin.x + (BarMax.x - BarMin.x) * NormalizedTime, BarMax.y), AnimColorAccent(), 3.0f);
        }

        ImGui::EndChild();
        if (AnimationViewer)
        {
            const ImVec2 SplitterMin = ImGui::GetCursorScreenPos();
            const float SplitterWidth = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##AnimViewportTimelineSplitter", ImVec2(SplitterWidth, TimelineSplitterHeight));
            const ImVec2 SplitterMax(SplitterMin.x + SplitterWidth, SplitterMin.y + TimelineSplitterHeight);
            ImDrawList* SplitterDrawList = ImGui::GetWindowDrawList();
            const bool bSplitterHovered = ImGui::IsItemHovered();
            SplitterDrawList->AddRectFilled(
                SplitterMin,
                SplitterMax,
                bSplitterHovered || ImGui::IsItemActive() ? IM_COL32(79, 103, 135, 255) : IM_COL32(44, 50, 61, 255),
                2.0f);
            if (ImGui::IsItemActive())
            {
                DownPanelHeight -= ImGui::GetIO().MouseDelta.y;
                ClampTimelinePanelHeight();
            }

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.23f, 0.27f, 0.34f, 1.0f));
            ImGui::BeginChild("AnimationSequencePanel", ImVec2(0, DownPanelHeight), true);
            {
                RenderAnimationSequencePanelContent(
                    AnimationViewer,
                    TimelineState);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::EndChild();

    // Right Splitter
    ImGui::SameLine();
    ImGui::Button("##right_splitter", ImVec2(2.0f, -1.0f));
    if (ImGui::IsItemActive())
    {
        RightPanelWidth -= ImGui::GetIO().MouseDelta.x;
        RightPanelWidth = std::clamp(RightPanelWidth, 100.0f, FullSize.x * 0.4f);
    }

    ImGui::SameLine();

    // =====================================================
    // RIGHT: Bone Details
    // =====================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.23f, 0.27f, 0.34f, 1.0f));
    ImGui::BeginChild("DetailsPanel", ImVec2(RightPanelWidth, 0), true);
    {
        ImGui::TextDisabled("INSPECTOR");
        ImGui::Text("Notify Details");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float DetailsHeight = ImGui::GetContentRegionAvail().y;
        const float PickerHeight = std::clamp(DetailsHeight * 0.48f, 190.0f, 320.0f);
        const float NotifyHeight = std::max(
            92.0f,
            DetailsHeight - PickerHeight - ImGui::GetStyle().ItemSpacing.y - 8.0f);

        ImGui::BeginChild("NotifyDetailsBody", ImVec2(0, NotifyHeight), false);
        {
            DrawSelectedNotifyDetails(AnimationViewer);
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginChild("AnimationPickerDetails", ImVec2(0, 0), false);
        {
            RenderAnimationPickerDetails(AnimationViewer, TimelineState);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void FEditorViewerWindowWidget::RenderAnimationSequencePanelContent(
    FAnimationViewer* AnimationViewer,
    FAnimTimelineUIState& State)
{
    ImDrawList* DrawList = ImGui::GetWindowDrawList();

    const float PanelWidth = ImGui::GetContentRegionAvail().x;
    const float TopBarHeight = 92.0f;
    const float HeaderHeight = 30.0f;
    const float RowHeight = 30.0f;
    constexpr float FPS = 30.0f;

    const FString EmptyAnimPath;
    const FString& AnimPath = AnimationViewer ? AnimationViewer->GetAnimationSequencePath() : EmptyAnimPath;
    UAnimationSequence* CurrentSequence = AnimationViewer ? AnimationViewer->GetAnimationSequence() : nullptr;
    const TArray<FAnimNotifyEvent>* Notifies = CurrentSequence ? &CurrentSequence->GetNotifies() : nullptr;
    const char* AnimName = AnimPath.empty() ? "None" : AnimPath.c_str();
    const float CurrentTime = AnimationViewer ? AnimationViewer->GetAnimationTime() : 0.0f;
    const float Duration = AnimationViewer ? AnimationViewer->GetAnimationLength() : 0.0f;
    const int32 TotalFrames = Duration > 0.0f
        ? std::max(1, static_cast<int32>(std::ceil(Duration * FPS)))
        : 0;
    const bool bHasAnimation = AnimationViewer && Duration > 0.0f;
    const bool bCanSaveAnimation = CurrentSequence != nullptr;
    const bool bAnimationDirty = CurrentSequence && CurrentSequence->GetDirtyFlag();
    const int32 NotifyCount = Notifies ? static_cast<int32>(Notifies->size()) : 0;
    const bool bIsPlaying = AnimationViewer && AnimationViewer->IsAnimationPlaying() && !AnimationViewer->IsAnimationPaused();
    const bool bIsPaused = AnimationViewer && AnimationViewer->IsAnimationPlaying() && AnimationViewer->IsAnimationPaused();
    const char* PlaybackState = bIsPlaying ? "Playing" : (bIsPaused ? "Paused" : "Stopped");
    auto SetTimelineTime = [&](float NewTime)
    {
        if (!AnimationViewer || !bHasAnimation)
        {
            return;
        }

        const float ClampedTime = std::clamp(NewTime, 0.0f, Duration);
        AnimationViewer->SetAnimationTime(ClampedTime);
        State.CurrentFrame = std::clamp(
            static_cast<int32>(std::round(ClampedTime * FPS)),
            0,
            TotalFrames);
    };
    if (bHasAnimation && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        State.CurrentFrame = std::clamp(
            static_cast<int32>(std::round(CurrentTime * FPS)),
            0,
            TotalFrames);
    }

    // =====================================================
    // TOP BAR
    // =====================================================
    ImGui::BeginChild("AnimSeqTopBar", ImVec2(0, TopBarHeight), false);
    {
        const ImVec2 HeaderMin = ImGui::GetCursorScreenPos();
        const ImVec2 HeaderMax(HeaderMin.x + PanelWidth, HeaderMin.y + TopBarHeight - 4.0f);
        DrawList->AddRectFilled(HeaderMin, HeaderMax, AnimColorPanelAlt(), 7.0f);
        DrawList->AddRect(HeaderMin, HeaderMax, AnimColorBorder(), 7.0f);
        DrawList->AddRectFilled(HeaderMin, ImVec2(HeaderMin.x + 4.0f, HeaderMax.y), AnimColorAccent(), 7.0f, ImDrawFlags_RoundCornersLeft);

        ImGui::SetCursorScreenPos(ImVec2(HeaderMin.x + 14.0f, HeaderMin.y + 8.0f));
        ImGui::TextDisabled("ANIMATION");
        ImGui::SameLine();

        const float CurrentLabelWidth = std::min(std::max(180.0f, PanelWidth * 0.24f), 320.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.08f, 0.11f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.23f, 0.27f, 0.34f, 1.0f));
        ImGui::BeginChild(
            "AnimCurrentSequenceLabel",
            ImVec2(CurrentLabelWidth, 28.0f),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            ImGui::TextUnformatted(AnimName);
        }
        ImGui::EndChild();
        if (ImGui::IsItemHovered() && !AnimPath.empty())
        {
            ImGui::SetTooltip("%s", AnimPath.c_str());
        }
        ImGui::PopStyleColor(2);

        const ImVec2 TransportButtonSize(30.0f, 28.0f);

        ImGui::SameLine();
        if (DrawAnimIconButton(GetViewerIcon(EEditorViewerIcon::ToFront), "|<", "To Front", TransportButtonSize, false, false, bHasAnimation))
        {
            SetTimelineTime(0.0f);
        }

        ImGui::SameLine();
        if (DrawAnimIconButton(GetViewerIcon(EEditorViewerIcon::ToPrevious), "<", "Previous Frame", TransportButtonSize, false, false, bHasAnimation))
        {
            SetTimelineTime(CurrentTime - (1.0f / FPS));
        }

        ImGui::SameLine();
        if (DrawAnimIconButton(
            GetViewerIcon(bIsPlaying ? EEditorViewerIcon::Pause : EEditorViewerIcon::PlayForward),
            bIsPlaying ? "II" : ">",
            bIsPlaying ? "Pause" : "Play",
            TransportButtonSize,
            true,
            bIsPlaying,
            bHasAnimation))
        {
            if (bIsPlaying)
            {
                AnimationViewer->PauseAnimation();
            }
            else if (AnimationViewer)
            {
                AnimationViewer->PlayAnimation();
            }
        }

        ImGui::SameLine();
        if (DrawAnimIconButton(GetViewerIcon(EEditorViewerIcon::ToNext), ">", "Next Frame", TransportButtonSize, false, false, bHasAnimation))
        {
            SetTimelineTime(CurrentTime + (1.0f / FPS));
        }

        ImGui::SameLine();
        if (DrawAnimIconButton(GetViewerIcon(EEditorViewerIcon::ToEnd), ">|", "To End", TransportButtonSize, false, false, bHasAnimation))
        {
            SetTimelineTime(Duration);
        }

        ImGui::SameLine();
        if (DrawAnimIconButton(GetViewerIcon(EEditorViewerIcon::Stop), "S", "Stop", TransportButtonSize, false, false, bHasAnimation))
        {
            if (AnimationViewer)
            {
                AnimationViewer->StopAnimation();
            }
            State.CurrentFrame = 0;
        }

        ImGui::SameLine();
        if (DrawAnimButton(bAnimationDirty ? "* Save" : "Save", ImVec2(68.0f, 28.0f), bAnimationDirty, false, bCanSaveAnimation))
        {
            if (AnimationViewer)
            {
                AnimationViewer->SaveAnimation();
            }
        }

        ImGui::SameLine();
        bool bLooping = AnimationViewer ? AnimationViewer->IsLooping() : false;
        if (DrawAnimIconButton(
            GetViewerIcon(bLooping ? EEditorViewerIcon::Looping : EEditorViewerIcon::NoLooping),
            "Loop",
            bLooping ? "Loop On" : "Loop Off",
            TransportButtonSize,
            false,
            bLooping,
            AnimationViewer != nullptr) && AnimationViewer)
        {
            AnimationViewer->SetLooping(!bLooping);
        }

        ImGui::SameLine();
        bool bReversePlay = AnimationViewer ? AnimationViewer->IsReversePlaying() : false;
        if (DrawAnimIconButton(
            GetViewerIcon(bReversePlay ? EEditorViewerIcon::PlayReverse : EEditorViewerIcon::PlayForward),
            bReversePlay ? "Rev" : "Fwd",
            bReversePlay ? "Reverse" : "Forward",
            TransportButtonSize,
            false,
            bReversePlay,
            AnimationViewer != nullptr) && AnimationViewer)
        {
            AnimationViewer->SetReversePlay(!bReversePlay);
        }

        ImGui::SetCursorScreenPos(ImVec2(HeaderMin.x + 14.0f, HeaderMin.y + 50.0f));
        char FrameBuffer[48];
        std::snprintf(FrameBuffer, sizeof(FrameBuffer), "%d / %d", State.CurrentFrame, TotalFrames);
        char TimeBuffer[64];
        std::snprintf(TimeBuffer, sizeof(TimeBuffer), "%.3f / %.3f", CurrentTime, Duration);
        char NotifyBuffer[32];
        std::snprintf(NotifyBuffer, sizeof(NotifyBuffer), "%d", NotifyCount);
        DrawAnimMetric("STATE", PlaybackState, 96.0f);
        ImGui::SameLine();
        DrawAnimMetric("FRAME", FrameBuffer, 100.0f);
        ImGui::SameLine();
        DrawAnimMetric("TIME", TimeBuffer, 136.0f);
        ImGui::SameLine();
        DrawAnimMetric("FPS", "30.0", 74.0f);
        ImGui::SameLine();
        DrawAnimMetric("NOTIFY", NotifyBuffer, 82.0f);

        ImGui::SameLine();
        float PlayRate = AnimationViewer ? AnimationViewer->GetPlayRate() : 1.0f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::TextDisabled("Speed");
        ImGui::SameLine();
        ImGui::PushItemWidth(116.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.08f, 0.11f, 1.0f));
        if (ImGui::DragFloat("##AnimSpeedRate", &PlayRate, 0.01f, 0.001f, 4.0f, "%.2fx") && AnimationViewer)
        {
            AnimationViewer->SetPlayRate(PlayRate);
        }
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // =====================================================
    // LEFT TRACK LIST
    // =====================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.23f, 0.27f, 0.34f, 1.0f));
    ImGui::BeginChild("AnimTrackList", ImVec2(State.TrackListWidth, 0), true);
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

        ImGui::TextDisabled("TRACKS");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AnimTrackFilter", "Filter notifies", State.TrackFilter, sizeof(State.TrackFilter));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        char NotifyHeader[64];
        std::snprintf(NotifyHeader, sizeof(NotifyHeader), "Notifies (%d)", NotifyCount);
        if (ImGui::CollapsingHeader(NotifyHeader, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##AnimNotifyName", "Notify name", State.NotifyNameBuffer, sizeof(State.NotifyNameBuffer));

            if (DrawAnimButton("Add At Current Time", ImVec2(-1.0f, 28.0f), true, false, CurrentSequence != nullptr))
            {
                FAnimNotifyEvent Notify;
                Notify.TriggerTime = std::clamp(CurrentTime, 0.0f, Duration);
                Notify.NotifyName = FName(State.NotifyNameBuffer);
                CurrentSequence->AddNotify(Notify);
                if (Notifies)
                {
                    State.SelectedNotifyIndex = static_cast<int32>(Notifies->size()) - 1;
                }
            }

            if (State.SelectedNotifyIndex >= NotifyCount)
            {
                State.SelectedNotifyIndex = -1;
            }

            const bool bDeleteNotifyDisabled = State.SelectedNotifyIndex < 0 || !CurrentSequence;
            if (DrawAnimButton("Delete Selected", ImVec2(-1.0f, 26.0f), false, false, !bDeleteNotifyDisabled))
            {
                if (CurrentSequence->RemoveNotifyAt(State.SelectedNotifyIndex))
                {
                    State.SelectedNotifyIndex = -1;
                }
            }

            ImGui::Spacing();

            if (Notifies && !Notifies->empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.24f, 0.34f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.30f, 0.42f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.36f, 0.52f, 1.0f));
                for (int32 NotifyIndex = 0; NotifyIndex < static_cast<int32>(Notifies->size()); ++NotifyIndex)
                {
                    const FAnimNotifyEvent& Notify = (*Notifies)[NotifyIndex];
                    FString NotifyLabel = Notify.NotifyName.IsValid() ? Notify.NotifyName.ToString() : "<None>";
                    if (!MatchesFilter(NotifyLabel, State.TrackFilter))
                    {
                        continue;
                    }

                    char TimeLabel[64];
                    std::snprintf(TimeLabel, sizeof(TimeLabel), "  %.3fs", Notify.TriggerTime);
                    NotifyLabel += TimeLabel;

                    ImGui::PushID(NotifyIndex);
                    const bool bSelected = State.SelectedNotifyIndex == NotifyIndex;
                    if (ImGui::Selectable("##NotifyRow", bSelected, 0, ImVec2(0.0f, 28.0f)))
                    {
                        State.SelectedNotifyIndex = NotifyIndex;
                    }
                    const ImVec2 RowMin = ImGui::GetItemRectMin();
                    const ImVec2 RowMax = ImGui::GetItemRectMax();
                    ImDrawList* RowDrawList = ImGui::GetWindowDrawList();
                    RowDrawList->AddCircleFilled(ImVec2(RowMin.x + 10.0f, (RowMin.y + RowMax.y) * 0.5f), 4.0f,
                        bSelected ? IM_COL32(255, 205, 92, 255) : AnimColorNotify(), 12);
                    RowDrawList->AddText(ImVec2(RowMin.x + 22.0f, RowMin.y + 6.0f),
                        bSelected ? IM_COL32(255, 245, 220, 255) : IM_COL32(220, 226, 235, 255),
                        NotifyLabel.c_str());
                    ImGui::PopID();
                }
                ImGui::PopStyleColor(3);
            }
            else
            {
                ImGui::TextDisabled("No notifies");
            }
        }

        if (ImGui::CollapsingHeader("Curves", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("No curve tracks in this viewer.");
        }

        if (ImGui::CollapsingHeader("Additive Layer Tracks", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("No additive layers.");
        }

        if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("No animation attributes.");
        }

        ImGui::PopStyleVar(2);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    // =====================================================
    // SPLITTER
    // =====================================================
    ImGui::SameLine();

    ImGui::Button("##AnimTimelineSplitter", ImVec2(2.0f, -1.0f));
    if (ImGui::IsItemActive())
    {
        State.TrackListWidth += ImGui::GetIO().MouseDelta.x;
        const float MaxTrackListWidth = std::max(180.0f, PanelWidth * 0.5f);
        State.TrackListWidth = std::clamp(State.TrackListWidth, 180.0f, MaxTrackListWidth);
    }

    ImGui::SameLine();

    // =====================================================
    // RIGHT TIMELINE
    // =====================================================
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.08f, 0.11f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.23f, 0.27f, 0.34f, 1.0f));
    ImGui::BeginChild(
        "AnimTimeline",
        ImVec2(0, 0),
        true,
        ImGuiWindowFlags_HorizontalScrollbar);
    {
        ImVec2 TimelineOrigin = ImGui::GetCursorScreenPos();
        ImVec2 Available = ImGui::GetContentRegionAvail();
        Available.x = std::max(1.0f, Available.x);
        Available.y = std::max(1.0f, Available.y);
        constexpr float TimelineGutter = 96.0f;

        const float TimelineContentWidth =
            std::max(Available.x, TimelineGutter + TotalFrames * State.FrameWidth + 80.0f);

        const float TimelineContentHeight =
            std::max(Available.y, HeaderHeight + RowHeight * 8.0f);

        ImGui::InvisibleButton(
            "##AnimTimelineCanvas",
            ImVec2(TimelineContentWidth, TimelineContentHeight),
            ImGuiButtonFlags_MouseButtonLeft);

        const float MaxScrollX = std::max(0.0f, TimelineContentWidth - Available.x);
        const float MaxScrollY = std::max(0.0f, TimelineContentHeight - Available.y);
        const float ClampedScrollX = std::clamp(ImGui::GetScrollX(), 0.0f, MaxScrollX);
        const float ClampedScrollY = std::clamp(ImGui::GetScrollY(), 0.0f, MaxScrollY);
        if (ClampedScrollX != ImGui::GetScrollX())
        {
            ImGui::SetScrollX(ClampedScrollX);
        }
        if (ClampedScrollY != ImGui::GetScrollY())
        {
            ImGui::SetScrollY(ClampedScrollY);
        }

        const ImVec2 TimelineVisibleMin(
            TimelineOrigin.x + ClampedScrollX,
            TimelineOrigin.y + ClampedScrollY);
        const ImVec2 TimelineVisibleMax(
            TimelineVisibleMin.x + Available.x,
            TimelineVisibleMin.y + Available.y);

        const bool bHovered = ImGui::IsItemHovered();
        const bool bActive = ImGui::IsItemActive();

        ImVec2 CanvasMin = TimelineOrigin;
        ImVec2 CanvasMax = ImVec2(
            CanvasMin.x + TimelineContentWidth,
            CanvasMin.y + TimelineContentHeight);

        DrawList->PushClipRect(TimelineVisibleMin, TimelineVisibleMax, true);

        // Background
        DrawList->AddRectFilled(
            CanvasMin,
            CanvasMax,
            AnimColorPanel(),
            6.0f);
        DrawList->AddRect(CanvasMin, CanvasMax, AnimColorBorder(), 6.0f);

        // Header background
        DrawList->AddRectFilled(
            CanvasMin,
            ImVec2(CanvasMax.x, CanvasMin.y + HeaderHeight),
            IM_COL32(30, 35, 43, 255),
            6.0f,
            ImDrawFlags_RoundCornersTop);

        // Rows
        const char* TrackLabels[8] = {
            "Notifies",
            " ",
            " ",
            " ",
            " ",
            " ",
            " ",
            " ",
            /*"Curves",
            "Events",
            "Montage",
            "Blend",
            "Root",
            "Attrs",
            "Meta"*/
        };

        for (int32 Row = 0; Row < 8; ++Row)
        {
            float Y0 = CanvasMin.y + HeaderHeight + Row * RowHeight;
            float Y1 = Y0 + RowHeight;

            ImU32 RowColor = (Row % 2 == 0)
                                 ? IM_COL32(23, 27, 34, 255)
                                 : IM_COL32(27, 31, 39, 255);

            DrawList->AddRectFilled(
                ImVec2(CanvasMin.x, Y0),
                ImVec2(CanvasMax.x, Y1),
                RowColor);

            DrawList->AddLine(
                ImVec2(CanvasMin.x, Y1),
                ImVec2(CanvasMax.x, Y1),
                IM_COL32(45, 52, 64, 255));

            DrawTrackLabel(
                DrawList,
                ImVec2(CanvasMin.x, Y0),
                ImVec2(CanvasMin.x + TimelineGutter, Y1),
                TrackLabels[Row],
                Row == 0 ? AnimColorNotify() : IM_COL32(75, 94, 118, 255));
        }

        // Frame grid + numbers
        for (int32 Frame = 0; Frame <= TotalFrames; ++Frame)
        {
            const float X = CanvasMin.x + TimelineGutter + Frame * State.FrameWidth;

            const bool bMajorFrame = Frame % 5 == 0;

            // 간격이 좁으면 5의 배수만 숫자 표시
            const bool bShowFrameNumber =
                State.FrameWidth >= 24.0f || bMajorFrame;

            ImU32 LineColor = bMajorFrame
                                  ? IM_COL32(78, 88, 104, 255)
                                  : IM_COL32(43, 50, 61, 255);

            const float LineThickness = bMajorFrame ? 1.2f : 1.0f;

            DrawList->AddLine(
                ImVec2(X, CanvasMin.y),
                ImVec2(X, CanvasMax.y),
                LineColor,
                LineThickness);

            // 위쪽 작은 tick
            const float TickHeight = bMajorFrame ? 10.0f : 4.0f;

            DrawList->AddLine(
                ImVec2(X, CanvasMin.y),
                ImVec2(X, CanvasMin.y + TickHeight),
                bMajorFrame ? IM_COL32(174, 190, 212, 255) : IM_COL32(104, 116, 132, 255),
                1.0f);

            if (bShowFrameNumber)
            {
                char Buffer[32];
                snprintf(Buffer, sizeof(Buffer), "%d", Frame);

                DrawList->AddText(
                    ImVec2(X + 4.0f, CanvasMin.y + 4.0f),
                    bMajorFrame ? IM_COL32(220, 230, 244, 255) : IM_COL32(140, 150, 164, 255),
                    Buffer);
            }
        }

        const float SafeFPS = FPS > 0.0f ? FPS : 30.0f;

        struct FTimelineKeyRect
        {
            ImVec2 Min;
            ImVec2 Max;

            bool Contains(const ImVec2& Point) const
            {
                return Point.x >= Min.x && Point.x <= Max.x && Point.y >= Min.y && Point.y <= Max.y;
            }
        };

        auto DrawKey = [&](float Time, float DurationSeconds, int32 Row, ImU32 Color, bool bSelected, const char* Label) -> FTimelineKeyRect
        {
            float Frame = Time * SafeFPS;
            float X = CanvasMin.x + TimelineGutter + Frame * State.FrameWidth;
            float Y = CanvasMin.y + HeaderHeight + Row * RowHeight;
            FTimelineKeyRect KeyRect{
                ImVec2(X - 8.0f, Y + 5.0f),
                ImVec2(X + 8.0f, Y + RowHeight - 5.0f)
            };

            const float CenterY = Y + RowHeight * 0.5f;
            if (DurationSeconds > 0.0f)
            {
                const float EndX = CanvasMin.x + TimelineGutter + (Time + DurationSeconds) * SafeFPS * State.FrameWidth;
                DrawList->AddRectFilled(
                    ImVec2(X, CenterY - 5.0f),
                    ImVec2(std::max(X + 4.0f, EndX), CenterY + 5.0f),
                    bSelected ? IM_COL32(255, 196, 80, 92) : IM_COL32(255, 150, 72, 70),
                    3.0f);
            }

            const ImVec2 Diamond[4] = {
                ImVec2(X, CenterY - 9.0f),
                ImVec2(X + 8.0f, CenterY),
                ImVec2(X, CenterY + 9.0f),
                ImVec2(X - 8.0f, CenterY)
            };
            DrawList->AddConvexPolyFilled(Diamond, 4, Color);
            DrawList->AddPolyline(Diamond, 4, IM_COL32(20, 24, 30, 255), ImDrawFlags_Closed, 1.4f);

            if (Label && State.FrameWidth >= 16.0f)
            {
                const ImVec2 TextSize = ImGui::CalcTextSize(Label);
                const ImVec2 LabelMin(X + 12.0f, Y + 5.0f);
                const ImVec2 LabelMax(LabelMin.x + TextSize.x + 10.0f, LabelMin.y + 20.0f);
                DrawList->AddRectFilled(LabelMin, LabelMax, IM_COL32(11, 14, 19, bSelected ? 220 : 175), 4.0f);
                DrawList->AddRect(LabelMin, LabelMax, bSelected ? IM_COL32(255, 205, 92, 180) : IM_COL32(88, 99, 116, 150), 4.0f);
                DrawList->AddText(
                    ImVec2(LabelMin.x + 5.0f, LabelMin.y + 3.0f),
                    bSelected ? IM_COL32(255, 240, 210, 255) : IM_COL32(235, 205, 160, 255),
                    Label);
            }

            return KeyRect;
        };

        bool bNotifyMarkerClicked = false;
        if (Notifies)
        {
            for (int32 NotifyIndex = 0; NotifyIndex < static_cast<int32>(Notifies->size()); ++NotifyIndex)
            {
                const FAnimNotifyEvent& Notify = (*Notifies)[NotifyIndex];
                if (!Notify.NotifyName.IsValid())
                {
                    continue;
                }

                const bool bSelectedNotify = State.SelectedNotifyIndex == NotifyIndex;
                const FTimelineKeyRect KeyRect = DrawKey(
                    std::clamp(Notify.TriggerTime, 0.0f, Duration),
                    0.0f,
                    0,
                    bSelectedNotify ? IM_COL32(255, 195, 76, 255) : IM_COL32(220, 120, 64, 255),
                    bSelectedNotify,
                    Notify.NotifyName.ToString().c_str());

                if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && KeyRect.Contains(ImGui::GetIO().MousePos))
                {
                    State.SelectedNotifyIndex = NotifyIndex;
                    bNotifyMarkerClicked = true;
                }
            }
        }

        // Scrubber / playhead
        float CurrentFrameFloat = CurrentTime * SafeFPS;
        CurrentFrameFloat = std::clamp(CurrentFrameFloat, 0.0f, static_cast<float>(TotalFrames));

        const int32 DisplayFrame = static_cast<int32>(std::floor(CurrentFrameFloat));
        const float FrameAlpha = CurrentFrameFloat - static_cast<float>(DisplayFrame);

        const float X0 = CanvasMin.x + TimelineGutter + DisplayFrame * State.FrameWidth;
        const float X1 = CanvasMin.x + TimelineGutter + (DisplayFrame + 1) * State.FrameWidth;
        const float PlayheadX = std::lerp(X0, X1, FrameAlpha);

        DrawList->AddLine(
            ImVec2(PlayheadX + 2.0f, CanvasMin.y),
            ImVec2(PlayheadX + 2.0f, CanvasMax.y),
            IM_COL32(0, 0, 0, 110),
            3.0f);
        DrawList->AddRectFilled(
            ImVec2(PlayheadX - 8.0f, CanvasMin.y + 3.0f),
            ImVec2(PlayheadX + 8.0f, CanvasMin.y + HeaderHeight - 3.0f),
            AnimColorAccent(),
            4.0f);

        DrawList->AddLine(
            ImVec2(PlayheadX, CanvasMin.y),
            ImVec2(PlayheadX, CanvasMax.y),
            IM_COL32(190, 220, 255, 255),
            2.0f);

        char FrameText[64];
        snprintf(
            FrameText,
            sizeof(FrameText),
            "%d + %.2f",
            DisplayFrame,
            FrameAlpha);

        DrawList->AddText(
            ImVec2(PlayheadX + 10.0f, CanvasMin.y + HeaderHeight + 4.0f),
            IM_COL32(230, 242, 255, 255),
            FrameText);

        auto SetTimeFromMouseX = [&]()
        {
            if (!AnimationViewer)
            {
                return;
            }

            const float MouseX = ImGui::GetIO().MousePos.x;

            float NewFrameFloat = (MouseX - CanvasMin.x - TimelineGutter) / State.FrameWidth;
            NewFrameFloat = std::clamp(NewFrameFloat, 0.0f, static_cast<float>(TotalFrames));

            const float NewTime = NewFrameFloat / SafeFPS;
            State.CurrentFrame = static_cast<int32>(std::round(NewFrameFloat));
            AnimationViewer->SetAnimationTime(NewTime);
        };

        if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bNotifyMarkerClicked)
        {
            SetTimeFromMouseX();
        }

        if (bActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            SetTimeFromMouseX();
        }

        // Ctrl + Wheel zoom
        if (bHovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
        {
            State.FrameWidth += ImGui::GetIO().MouseWheel * 2.0f;
            State.FrameWidth = std::clamp(State.FrameWidth, 8.0f, 80.0f);
        }

        DrawList->PopClipRect();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void FEditorViewerWindowWidget::RenderAnimationPickerDetails(
    FAnimationViewer* AnimationViewer,
    FAnimTimelineUIState& State)
{
    ImGui::TextDisabled("ANIMATION SEQUENCE");
    ImGui::Spacing();

    const FString EmptyAnimPath;
    const FString& AnimPath = AnimationViewer ? AnimationViewer->GetAnimationSequencePath() : EmptyAnimPath;
    const FString CurrentLabel = AnimPath.empty() ? FString("None") : GetBaseFileNameWithoutExtension(AnimPath);

    ImGui::TextDisabled("Current");
    ImGui::TextWrapped("%s", CurrentLabel.c_str());
    if (!AnimPath.empty() && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", AnimPath.c_str());
    }

    if (AnimationViewer && !AnimPath.empty())
    {
        if (DrawAnimButton("Clear Animation", ImVec2(-1.0f, 26.0f), false, false, true))
        {
            AnimationViewer->ClearAnimationSequence();
            State.CurrentFrame = 0;
            State.SelectedNotifyIndex = -1;
        }
        ImGui::Spacing();
    }

    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 34.0f));
    ImGui::InputTextWithHint(
        "##AnimationSequenceSearch",
        "Search animation sequences",
        State.AnimationSearchFilter,
        sizeof(State.AnimationSearchFilter));

    ImGui::SameLine();
    const bool bHasSearchText = State.AnimationSearchFilter[0] != '\0';
    if (DrawAnimButton("X", ImVec2(28.0f, 0.0f), false, false, bHasSearchText))
    {
        State.AnimationSearchFilter[0] = '\0';
    }

    ImGui::Spacing();

    if (!AnimationViewer || !EditorEngine)
    {
        ImGui::TextDisabled("No animation viewer.");
        return;
    }

    const TArray<FString>& AnimationPaths = EditorEngine->GetAssetService().GetAnimationSequenceAssetPaths();
    int32 CompatibleCount = 0;
    int32 VisibleCount = 0;

    const ImGuiTableFlags TableFlags =
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("AnimationSequencePickerTable", 2, TableFlags, ImVec2(0.0f, 0.0f)))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (const FString& AnimationPath : AnimationPaths)
        {
            if (!AnimationViewer->IsAnimationSequenceCompatible(AnimationPath))
            {
                continue;
            }

            ++CompatibleCount;
            const FString AnimationName = GetBaseFileNameWithoutExtension(AnimationPath);
            if (!MatchesFilter(AnimationName, State.AnimationSearchFilter) &&
                !MatchesFilter(AnimationPath, State.AnimationSearchFilter))
            {
                continue;
            }

            ++VisibleCount;
            const bool bSelected = AnimationPath == AnimPath;

            ImGui::PushID(AnimationPath.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(AnimationName.c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns))
            {
                AnimationViewer->SetAnimationSequence(AnimationPath);
                State.CurrentFrame = 0;
                State.SelectedNotifyIndex = -1;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", AnimationPath.c_str());
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", AnimationPath.c_str());
            ImGui::PopID();
        }

        if (AnimationPaths.empty() || CompatibleCount == 0 || VisibleCount == 0)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (AnimationPaths.empty())
            {
                ImGui::TextDisabled("No animation sequences.");
            }
            else if (CompatibleCount == 0)
            {
                ImGui::TextDisabled("No compatible animation sequences.");
            }
            else
            {
                ImGui::TextDisabled("No results.");
            }
        }

        ImGui::EndTable();
    }
}

void FEditorViewerWindowWidget::DrawSelectedNotifyDetails(FAnimationViewer* AnimationViewer)
{
    if (!AnimationViewer)
    {
        ImGui::TextDisabled("No animation viewer.");
        return;
    }

    UAnimationSequence* AnimationSequence = AnimationViewer->GetAnimationSequence();
    if (!AnimationSequence)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("No animation sequence loaded.");
        return;
    }

    TArray<FAnimNotifyEvent>& Notifies = AnimationSequence->GetNotifies();

    const int32 NotifyIndex = TimelineState.SelectedNotifyIndex;
    if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(Notifies.size()))
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Select a notify marker or row.");
        ImGui::Spacing();
        ImGui::Text("Notify Count: %d", static_cast<int32>(Notifies.size()));
        return;
    }

    FAnimNotifyEvent& Notify = Notifies[NotifyIndex];

    bool bChanged = false;

    const FString NotifyNameString = Notify.NotifyName.ToString();
    const FString HeaderName = NotifyNameString.empty() ? FString("<None>") : NotifyNameString;
    const ImVec2 HeaderMin = ImGui::GetCursorScreenPos();
    const ImVec2 HeaderMax(HeaderMin.x + ImGui::GetContentRegionAvail().x, HeaderMin.y + 56.0f);
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->AddRectFilled(HeaderMin, HeaderMax, AnimColorPanelAlt(), 6.0f);
    DrawList->AddRect(HeaderMin, HeaderMax, AnimColorBorder(), 6.0f);
    DrawList->AddCircleFilled(ImVec2(HeaderMin.x + 18.0f, HeaderMin.y + 20.0f), 5.0f, AnimColorNotify(), 16);
    DrawList->AddText(ImVec2(HeaderMin.x + 32.0f, HeaderMin.y + 10.0f), IM_COL32(238, 242, 248, 255), HeaderName.c_str());

    char SubLabel[96];
    std::snprintf(SubLabel, sizeof(SubLabel), "Index %d | %.3fs", NotifyIndex, Notify.TriggerTime);
    DrawList->AddText(ImVec2(HeaderMin.x + 32.0f, HeaderMin.y + 31.0f), IM_COL32(145, 156, 172, 255), SubLabel);
    ImGui::Dummy(ImVec2(0.0f, 62.0f));

    ImGui::TextDisabled("TIMING");

    float TriggerTime = Notify.TriggerTime;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat("Trigger Time", &TriggerTime, 0.01f, 0.0f, AnimationViewer->GetAnimationLength(), "%.3f s"))
    {
        Notify.TriggerTime = std::max(0.0f, TriggerTime);
        bChanged = true;
    }

    if (DrawAnimButton("Jump To Notify", ImVec2(-1.0f, 28.0f), false, false, true))
    {
        AnimationViewer->SetAnimationTime(Notify.TriggerTime);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("IDENTITY");

    static char NotifyNameBuffer[128] = {};

    std::snprintf(
        NotifyNameBuffer,
        sizeof(NotifyNameBuffer),
        "%s",
        NotifyNameString.c_str());

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("Notify Name", NotifyNameBuffer, sizeof(NotifyNameBuffer)))
    {
        Notify.NotifyName = FName(NotifyNameBuffer);
        bChanged = true;
    }

    if (bChanged)
    {
        AnimationSequence->MarkDirty();
    }
}

void FEditorViewerWindowWidget::RenderBoneDetails(USkeletalMeshComponent* SkelComp)
{
	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
    const int32 SelectedBoneIndex = SkeletalViewer ? SkeletalViewer->GetSelectedBoneIndex() : -1;
    if (!SkelComp || SelectedBoneIndex == -1) return;

    const FBoneInfo& Bone = SkelComp->GetSkeletalMesh()->GetMeshData()->Bones[SelectedBoneIndex];
    ImGui::Text("Bone: %s (Index: %d)", Bone.Name.c_str(), SelectedBoneIndex);
    ImGui::Spacing();

    FMatrix LocalTransform = SkelComp->GetBoneLocalTransform(SelectedBoneIndex);
    FVector Location, Scale;
    FMatrix RotationMatrix;
    LocalTransform.Decompose(Location, RotationMatrix, Scale);

    // 외부(기즈모 등)에서 회전이 변경되었는지 확인
    FVector CurrentEuler = RotationMatrix.GetEuler();
    FVector& CachedRotation = SkeletalViewer->GetCachedBoneRotation();

	if ((CurrentEuler - FMatrix::MakeRotationEuler(CachedRotation).GetEuler()).Size() > 0.01f)
    {
        CachedRotation = CurrentEuler;
    }

    bool bEdited = false;

    auto DrawTransformField = [&](const char* Label, FVector& Value, float Speed) {
        float Arr[3] = { Value.X, Value.Y, Value.Z };
        if (ImGui::DragFloat3(Label, Arr, Speed))
        {
            Value = FVector(Arr[0], Arr[1], Arr[2]);
            return true;
        }
        return false;
    };

    ImGui::Text("Transform (Local)");
    if (DrawTransformField("Location", Location, 0.1f)) bEdited = true;
    if (DrawTransformField("Rotation", CachedRotation, 0.1f)) bEdited = true;
    if (DrawTransformField("Scale", Scale, 0.01f)) bEdited = true;

    if (bEdited)
    {
        FMatrix NewLocal = FMatrix::MakeTRS(Location, FMatrix::MakeRotationEuler(CachedRotation), Scale);
        SkelComp->SetBoneLocalTransform(SelectedBoneIndex, NewLocal);

        // Gizmo 위치 업데이트
        FEditorViewportClient* EditorClient = Viewer ? Viewer->GetViewportClient() : nullptr;
        if (EditorClient && EditorClient->GetGizmo())
        {
            EditorClient->GetGizmo()->UpdateGizmoTransform();
        }
    }
}

void FEditorViewerWindowWidget::DrawBoneNode(int32 BoneIndex, const TArray<FBoneInfo>& Bones, const TArray<TArray<int32>>& Children)
{
	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
	if (!SkeletalViewer)
	{
		return;
	}

    const FBoneInfo& Bone = Bones[BoneIndex];

    // socket까지 자식으로 그리므로 "자식 없음"은 bone-children + socket-children 모두 비어야 성립.
    const bool bHasBoneChildren   = Children[BoneIndex].size() > 0;
    const bool bHasSocketChildren = BoneIndex < static_cast<int32>(BoneToSocketIndices.size())
                                    && BoneToSocketIndices[BoneIndex].size() > 0;

    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    if (!bHasBoneChildren && !bHasSocketChildren)
    {
        Flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (SkeletalViewer->GetSelectedBoneIndex() == BoneIndex)
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool bOpen = ImGui::TreeNodeEx(
        (void*)(intptr_t)BoneIndex,
        Flags,
        "%s",
        Bone.Name.c_str());

    // 클릭 → bone 선택. socket 선택은 해제 (상호 배타).
    if (ImGui::IsItemClicked())
    {
        SkeletalViewer->SelectBone(BoneIndex);
    }

    // 우클릭 컨텍스트
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Add Socket"))
        {
            AddSocketOnBone(BoneIndex);
        }
        ImGui::Separator();

        const bool bCanToggleChildren = bHasBoneChildren || bHasSocketChildren;
        if (ImGui::MenuItem("Expand Children", nullptr, false, bCanToggleChildren))
        {
            QueueBoneSubtreeOpenState(BoneIndex, true);
        }
        if (ImGui::MenuItem("Collapse Children", nullptr, false, bCanToggleChildren))
        {
            QueueBoneSubtreeOpenState(BoneIndex, false);
        }
        ImGui::EndPopup();
    }

    if (bOpen)
    {
        // (1) 자식 bone들
        for (int32 ChildIndex : Children[BoneIndex])
        {
            DrawBoneNode(ChildIndex, Bones, Children);
        }

        // (2) 이 bone에 매달린 socket들 (자식 bone 다음에 표시)
        if (bHasSocketChildren)
        {
            for (int32 SocketIdx : BoneToSocketIndices[BoneIndex])
            {
                DrawSocketNode(SocketIdx);
            }
        }

        ImGui::TreePop();
    }
}

void FEditorViewerWindowWidget::QueueBoneSubtreeOpenState(int32 BoneIdx, bool bOpen)
{
    PendingBoneTreeOpenStateRoot = BoneIdx;
    bPendingBoneTreeOpenStateValue = bOpen;
}

void FEditorViewerWindowWidget::ApplyPendingBoneTreeOpenState(const FSkeletalMesh* MeshData)
{
    if (!MeshData || PendingBoneTreeOpenStateRoot < 0)
    {
        return;
    }

    SetBoneSubtreeOpenState(PendingBoneTreeOpenStateRoot, Children, bPendingBoneTreeOpenStateValue);
    PendingBoneTreeOpenStateRoot = -1;
}

void FEditorViewerWindowWidget::SetBoneSubtreeOpenState(
    int32 BoneIdx,
    const TArray<TArray<int32>>& InChildren,
    bool bOpen)
{
    if (BoneIdx < 0 || BoneIdx >= static_cast<int32>(InChildren.size()))
    {
        return;
    }

    ImGuiStorage* Storage = ImGui::GetStateStorage();
    if (!Storage)
    {
        return;
    }

    const void* NodePtr = reinterpret_cast<void*>(static_cast<intptr_t>(BoneIdx));
    const ImGuiID NodeId = ImGui::GetID(NodePtr);

    // Expand는 부모가 먼저 열려야 화면에서 즉시 전체 subtree가 보인다.
    if (bOpen)
    {
        Storage->SetInt(NodeId, 1);
    }

    ImGui::PushID(NodePtr);
    for (int32 ChildIndex : InChildren[BoneIdx])
    {
        SetBoneSubtreeOpenState(ChildIndex, InChildren, bOpen);
    }
    ImGui::PopID();

    // Collapse는 자식부터 닫고 마지막에 부모를 닫아야,
    // 부모를 다시 열었을 때 이전에 열려 있던 하위 노드가 되살아나지 않는다.
    if (!bOpen)
    {
        Storage->SetInt(NodeId, 0);
    }
}

void FEditorViewerWindowWidget::DrawSocketNode(int32 SocketIdx)
{
	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
	if (!SkeletalViewer)
	{
		return;
	}

    if (!CachedMesh) return;
    if (SocketIdx < 0 || SocketIdx >= static_cast<int32>(CachedMesh->Sockets.size())) return;

    const FSkeletalMeshSocket& Socket = CachedMesh->Sockets[SocketIdx];

    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_Leaf |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_NoTreePushOnOpen;   // leaf니까 자식 push 불필요

    if (SkeletalViewer->GetSelectedSocketIndex() == SocketIdx)
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    // bone ID 공간(int32 직접)과 충돌하지 않게 high-bit 네임스페이스.
    const void* NodeId = reinterpret_cast<const void*>(
        static_cast<uintptr_t>(0x80000000u | static_cast<uint32>(SocketIdx)));

    // socket을 시각적으로 구분 — cyan-ish, "◇" prefix
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.0f, 1.0f));
    ImGui::TreeNodeEx(NodeId, Flags, "\xe2\x97\x87 %s", Socket.Name.ToString().c_str());   // ◇
    ImGui::PopStyleColor();

    // 클릭 → socket 선택. bone 선택은 해제.
    if (ImGui::IsItemClicked())
    {
        SkeletalViewer->SelectSocket(SocketIdx);
    }

    // 우클릭 컨텍스트
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Add Preview Mesh..."))
        {
            // 모달은 popup 바깥에서 OpenPopup해야 안정적 — 여기선 트리거 idx만 기록.
            PendingPreviewPickerSocketIdx = SocketIdx;
        }

        const bool bHasPreview = HasPreview(Socket.Name);
        if (ImGui::MenuItem("Remove Preview Mesh", nullptr, false, bHasPreview))
        {
            if (EditorEngine)
            {
                SkeletalViewer->ClearSocketPreview(Socket.Name);
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Rename"))
        {
            RenameSocketIdx = SocketIdx;
            std::snprintf(RenameBuffer, sizeof(RenameBuffer), "%s",
                          Socket.Name.ToString().c_str());
        }

        if (ImGui::MenuItem("Delete Socket"))
        {
            DeleteSocket(SocketIdx);
        }

        ImGui::EndPopup();
    }
}

void FEditorViewerWindowWidget::RebuildBoneTreeCaches(const FSkeletalMesh* MeshData)
{
    Children.clear();
    BoneToSocketIndices.clear();
    if (!MeshData) return;

    const int32 BoneCount = static_cast<int32>(MeshData->Bones.size());
    Children.resize(BoneCount);

    for (int32 i = 0; i < BoneCount; ++i)
    {
        const int32 Parent = MeshData->Bones[i].ParentIndex;
        if (Parent >= 0)
        {
            Children[Parent].push_back(i);
        }
    }

    RebuildBoneToSocketIndices(MeshData);
}

void FEditorViewerWindowWidget::RebuildBoneToSocketIndices(const FSkeletalMesh* MeshData)
{
    BoneToSocketIndices.clear();
    if (!MeshData) return;

    const int32 BoneCount = static_cast<int32>(MeshData->Bones.size());
    BoneToSocketIndices.resize(BoneCount);

    for (int32 i = 0; i < static_cast<int32>(MeshData->Sockets.size()); ++i)
    {
        const int32 B = MeshData->Sockets[i].BoneIndex;
        if (B >= 0 && B < BoneCount)
        {
            BoneToSocketIndices[B].push_back(i);
        }
    }
}

void FEditorViewerWindowWidget::AddSocketOnBone(int32 BoneIdx)
{
	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
    if (!CachedMesh) return;
	if (!SkeletalViewer) return;
    if (BoneIdx < 0 || BoneIdx >= static_cast<int32>(CachedMesh->Bones.size())) return;

    FSkeletalMeshSocket NewSocket;
    NewSocket.Name = FName(GenerateUniqueSocketName());
    NewSocket.BoneIndex = BoneIdx;
    // Loc/Rot/Scale은 기본값(0, identity, 1)

    CachedMesh->Sockets.push_back(NewSocket);
    const int32 NewIdx = static_cast<int32>(CachedMesh->Sockets.size()) - 1;

    RebuildBoneToSocketIndices(CachedMesh);

    SkeletalViewer->SelectSocket(NewIdx);
    bMeshDirty = true;

    // socket-attached children의 transform이 새로 계산되도록 본 자세 dirty 전파 트리거.
    if (CachedSkComp)
    {
        CachedSkComp->MarkSkinningDirty();
    }
}

FString FEditorViewerWindowWidget::GenerateUniqueSocketName(const char* Base) const
{
    if (!CachedMesh) return FString(Base);

    auto Exists = [&](const FString& Candidate) -> bool {
        const FName CandidateName(Candidate);
        for (const FSkeletalMeshSocket& S : CachedMesh->Sockets)
        {
            if (S.Name == CandidateName) return true;
        }
        return false;
    };

    FString Candidate = Base;
    if (!Exists(Candidate)) return Candidate;

    for (int32 i = 1; i < 10000; ++i)
    {
        Candidate = FString(Base) + "_" + std::to_string(i);
        if (!Exists(Candidate)) return Candidate;
    }
    return Candidate;   // 폴백 — 거의 도달 불가
}

void FEditorViewerWindowWidget::DeleteSocket(int32 SocketIdx)
{
	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
    if (!CachedMesh) return;
	if (!SkeletalViewer) return;
    if (SocketIdx < 0 || SocketIdx >= static_cast<int32>(CachedMesh->Sockets.size())) return;

    // (1) 해당 socket에 매달린 preview mesh 먼저 정리
    const FName SocketName = CachedMesh->Sockets[SocketIdx].Name;
    if (EditorEngine)
    {
        SkeletalViewer->ClearSocketPreview(SocketName);
    }

    // (2) Sockets 배열에서 erase. 다른 socket들의 인덱스가 시프트됨.
    CachedMesh->Sockets.erase(CachedMesh->Sockets.begin() + SocketIdx);

    // (3) BoneToSocketIndices 통째 재빌드 (시프트된 인덱스 반영)
    RebuildBoneToSocketIndices(CachedMesh);

    // (4) 선택 상태 정리
    SkeletalViewer->NotifySocketDeleted(SocketIdx);

    bMeshDirty = true;

    if (CachedSkComp)
    {
        CachedSkComp->MarkSkinningDirty();
    }
}

bool FEditorViewerWindowWidget::HasPreview(const FName& SocketName) const
{
	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
    if (!EditorEngine || !SkeletalViewer) return false;
    return SkeletalViewer->FindPreviewMesh(SocketName) != nullptr;
}

void FEditorViewerWindowWidget::DrawSocketInspector()
{
    // Save 상태는 socket 선택 여부와 무관하게 항상 보이는 게 편함.
    auto DrawSaveButton = [&]() {
        const bool bCanSave = CanSaveMesh();
        if (!bCanSave) ImGui::BeginDisabled();
        const char* Label = IsMeshDirty() ? "Save Mesh *" : "Save Mesh";
        if (ImGui::Button(Label))
        {
            TriggerSaveMesh();
        }
        if (!bCanSave) ImGui::EndDisabled();
    };

	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);
    const int32 SelectedSocketIndex = SkeletalViewer ? SkeletalViewer->GetSelectedSocketIndex() : -1;
    if (!CachedMesh || SelectedSocketIndex < 0 ||
        SelectedSocketIndex >= static_cast<int32>(CachedMesh->Sockets.size()))
    {
        ImGui::TextDisabled("(no socket selected)");
        DrawSaveButton();
        return;
    }

    FSkeletalMeshSocket& Socket = CachedMesh->Sockets[SelectedSocketIndex];

    ImGui::Text("Socket: %s", Socket.Name.ToString().c_str());

    // Bone 콤보
    const TArray<FBoneInfo>& Bones = CachedMesh->Bones;
    const char* CurrentBoneName = (Socket.BoneIndex >= 0 && Socket.BoneIndex < (int32)Bones.size())
        ? Bones[Socket.BoneIndex].Name.c_str()
        : "<invalid>";

    if (ImGui::BeginCombo("Bone", CurrentBoneName))
    {
        for (int32 i = 0; i < static_cast<int32>(Bones.size()); ++i)
        {
            const bool bSelected = (Socket.BoneIndex == i);
            if (ImGui::Selectable(Bones[i].Name.c_str(), bSelected))
            {
                if (Socket.BoneIndex != i)
                {
                    Socket.BoneIndex = i;
                    RebuildBoneToSocketIndices(CachedMesh);   // 트리에서 새 본 밑으로 이동
                    bMeshDirty = true;
                    if (CachedSkComp) CachedSkComp->MarkSkinningDirty();
                }
            }
            if (bSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Location / Rotation / Scale
    // FVector / FRotator 모두 contiguous 3 float — &X / &Pitch로 DragFloat3에 전달.
    bool bChanged = false;
    bChanged |= ImGui::DragFloat3("Location", &Socket.RelativeLocation.X, 0.5f);
    bChanged |= ImGui::DragFloat3("Rotation (P/Y/R)", &Socket.RelativeRotation.Pitch, 0.5f);
    bChanged |= ImGui::DragFloat3("Scale", &Socket.RelativeScale.X, 0.01f, 0.001f, 100.0f);

    if (bChanged)
    {
        bMeshDirty = true;
        if (CachedSkComp) CachedSkComp->MarkSkinningDirty();
    }

    ImGui::Separator();
    DrawSaveButton();
}

void FEditorViewerWindowWidget::TriggerSaveMesh()
{
	RequestSaveMesh();
}

bool FEditorViewerWindowWidget::IsSocketNameUnique(const FString& Candidate, int32 IgnoreIdx) const
{
    if (!CachedMesh) return false;
    const FName CandidateName(Candidate);
    for (int32 i = 0; i < static_cast<int32>(CachedMesh->Sockets.size()); ++i)
    {
        if (i == IgnoreIdx) continue;
        if (CachedMesh->Sockets[i].Name == CandidateName) return false;
    }
    return true;
}

void FEditorViewerWindowWidget::DrawRenameModal()
{
    if (!ImGui::BeginPopupModal("RenameSocket", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);

    // 무효한 상태 — 즉시 닫기
    if (!CachedMesh || !SkeletalViewer || RenameSocketIdx < 0 ||
        RenameSocketIdx >= static_cast<int32>(CachedMesh->Sockets.size()))
    {
        RenameSocketIdx = -1;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Rename socket:");
    ImGui::InputText("##rename", RenameBuffer, sizeof(RenameBuffer));

    const FString Candidate(RenameBuffer);
    const bool bEmpty  = Candidate.empty();
    const bool bUnique = !bEmpty && IsSocketNameUnique(Candidate, RenameSocketIdx);
    const bool bValid  = !bEmpty && bUnique;

    if (bEmpty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Name cannot be empty");
    }
    else if (!bUnique)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Name already in use");
    }

    if (!bValid) ImGui::BeginDisabled();
    if (ImGui::Button("OK"))
    {
        // Preview mesh가 이 socket에 attach되어 있다면 key가 socket name이므로,
        // 이름 변경 시 preview를 깔끔히 재attach해야 함.
        const FName OldName = CachedMesh->Sockets[RenameSocketIdx].Name;
        const FName NewName(Candidate);

        FString PreviewPath;
        if (EditorEngine)
        {
            UStaticMeshComponent* Preview = SkeletalViewer->FindPreviewMesh(OldName);
            if (Preview && Preview->GetStaticMesh())
            {
                PreviewPath = Preview->GetStaticMesh()->GetAssetPathFileName();
                SkeletalViewer->ClearSocketPreview(OldName);
            }
        }

        CachedMesh->Sockets[RenameSocketIdx].Name = NewName;

        if (!PreviewPath.empty() && EditorEngine)
        {
            SkeletalViewer->SetSocketPreviewMesh(NewName, PreviewPath);
        }

        if (SkeletalViewer->GetSelectedSocketIndex() == RenameSocketIdx)
        {
            SkeletalViewer->SelectSocket(RenameSocketIdx);
        }

        bMeshDirty = true;
        if (CachedSkComp) CachedSkComp->MarkSkinningDirty();
        RenameSocketIdx = -1;
        ImGui::CloseCurrentPopup();
    }
    if (!bValid) ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        RenameSocketIdx = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void FEditorViewerWindowWidget::DrawPreviewPickerModal()
{
    if (!ImGui::BeginPopupModal("PickStaticMesh", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

	FSkeletalMeshViewer* SkeletalViewer = AsSkeletalMeshViewer(Viewer);

    static char Filter[256] = "";
    ImGui::InputText("Filter", Filter, sizeof(Filter));
    ImGui::Separator();

    const TArray<FString>& Paths = FResourceManager::Get().GetStaticMeshPaths();

    ImGui::BeginChild("PickList", ImVec2(420.0f, 300.0f), true);
    for (const FString& Path : Paths)
    {
        if (Filter[0] != '\0' && Path.find(Filter) == FString::npos)
        {
            continue;
        }

        if (ImGui::Selectable(Path.c_str()))
        {
            if (CachedMesh && EditorEngine && SkeletalViewer &&
                PendingPreviewPickerSocketIdx >= 0 &&
                PendingPreviewPickerSocketIdx < static_cast<int32>(CachedMesh->Sockets.size()))
            {
                const FName SocketName = CachedMesh->Sockets[PendingPreviewPickerSocketIdx].Name;
                SkeletalViewer->SetSocketPreviewMesh(SocketName, Path);
            }
            PendingPreviewPickerSocketIdx = -1;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Cancel"))
    {
        PendingPreviewPickerSocketIdx = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
