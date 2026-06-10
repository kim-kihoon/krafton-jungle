#include "Editor/UI/Asset/LuaBlueprint/LuaBlueprintEditorWidget.h"

#include "Input/InputKeyCodes.h"
#include "Lua/LuaDebugManager.h"
#include "LuaBlueprint/LuaBlueprintAsset.h"
#include "LuaBlueprint/LuaBlueprintManager.h"
#include "Asset/AssetRegistry.h"
#include "Object/Object.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_node_editor.h"

#include "Core/Types/PropertyTypes.h"
#include "Object/Reflection/UClass.h"
#include "Platform/Paths.h"
#include "Serialization/MemoryArchive.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace ed = ax::NodeEditor;

namespace
{
    inline ed::NodeId ToNodeId(uint32 Id)
    {
        return static_cast<ed::NodeId>(Id);
    }

    inline ed::PinId ToPinId(uint32 Id)
    {
        return static_cast<ed::PinId>(Id);
    }

    inline ed::LinkId ToLinkId(uint32 Id)
    {
        return static_cast<ed::LinkId>(Id);
    }

    inline uint32 NodeIdToU32(ed::NodeId Id)
    {
        return static_cast<uint32>(Id.Get());
    }

    inline uint32 PinIdToU32(ed::PinId Id)
    {
        return static_cast<uint32>(Id.Get());
    }

    inline uint32 LinkIdToU32(ed::LinkId Id)
    {
        return static_cast<uint32>(Id.Get());
    }

    FString NormalizeLuaBlueprintDebugPath(const FString& InPath)
    {
        FString Path = InPath;
        if (!Path.empty() && Path.front() == '@')
        {
            Path.erase(Path.begin());
        }
        Path = FPaths::MakeProjectRelative(Path);
        std::replace(Path.begin(), Path.end(), '\\', '/');
        return Path;
    }

    bool IsLuaBlueprintExecutableDebugNode(const FLuaBlueprintNode& Node)
    {
        if (Node.Type == ELuaBlueprintNodeType::Comment)
        {
            return false;
        }
        for (const FLuaBlueprintPin& Pin : Node.Pins)
        {
            if (Pin.Type == ELuaBlueprintPinType::Exec)
            {
                return true;
            }
        }
        return false;
    }
    bool IsPausedOnLuaBlueprintNode(const ULuaBlueprintAsset* Blueprint, const FLuaBlueprintNode& Node)
    {
        if (!Blueprint || !FLuaDebugManager::IsPaused())
        {
            return false;
        }
        const FLuaDebugLocation& PausedLocation = FLuaDebugManager::GetPausedLocation();
        return PausedLocation.bLuaBlueprint
                && NormalizeLuaBlueprintDebugPath(PausedLocation.BlueprintPath) == NormalizeLuaBlueprintDebugPath(Blueprint->GetSourcePath())
                && PausedLocation.NodeId == Node.NodeId;
    }

    void RenderLuaDebugValueList(const char* Title, const TArray<FLuaDebugValueEntry>& Values, int32 MaxRows = 16)
    {
        if (!Title)
        {
            Title = "Values";
        }
        if (!ImGui::TreeNodeEx(Title, ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }
        if (Values.empty())
        {
            ImGui::TextDisabled("(none)");
            ImGui::TreePop();
            return;
        }

        const int32 Limit = (std::min)(MaxRows, static_cast<int32>(Values.size()));
        for (int32 Index = 0; Index < Limit; ++Index)
        {
            const FLuaDebugValueEntry& Entry = Values[Index];
            const FString Line = Entry.Name + " [" + Entry.TypeName + "] = " + Entry.Value;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            if (Entry.bError)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", Line.c_str());
            }
            else
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", Line.c_str());
            }
            ImGui::PopTextWrapPos();
        }
        if (static_cast<int32>(Values.size()) > Limit)
        {
            ImGui::TextDisabled("... %d more", static_cast<int32>(Values.size()) - Limit);
        }
        ImGui::TreePop();
    }

    void RenderLuaDebugInlineSnapshot(const FLuaDebugValueSnapshot& Snapshot)
    {
        int32 Rendered = 0;
        auto RenderOne = [&Rendered](const FLuaDebugValueEntry& Entry)
        {
            if (Rendered >= 5)
            {
                return;
            }
            const FString Line = Entry.Scope + ": " + Entry.Name + " = " + Entry.Value;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::max)(80.0f, ImGui::GetContentRegionAvail().x));
            if (Entry.bError)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", Line.c_str());
            }
            else
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", Line.c_str());
            }
            ImGui::PopTextWrapPos();
            ++Rendered;
        };

        for (const FLuaDebugValueEntry& Entry : Snapshot.NodeValues) RenderOne(Entry);
        for (const FLuaDebugValueEntry& Entry : Snapshot.BlueprintVariables) RenderOne(Entry);
        for (const FLuaDebugValueEntry& Entry : Snapshot.Watches) RenderOne(Entry);
        for (const FLuaDebugValueEntry& Entry : Snapshot.Locals) RenderOne(Entry);
        if (Rendered == 0)
        {
            ImGui::TextDisabled("No captured values");
        }
    }

    struct FScopedNodeEditorCurrent
    {
        ed::EditorContext* Previous = nullptr;
        ed::EditorContext* Desired  = nullptr;

        explicit FScopedNodeEditorCurrent(ed::EditorContext* InDesired) : Desired(InDesired)
        {
            Previous = ed::GetCurrentEditor();
            if (Desired && Previous != Desired)
            {
                ed::SetCurrentEditor(Desired);
            }
        }

        ~FScopedNodeEditorCurrent()
        {
            if (Desired && Previous != Desired)
            {
                ed::SetCurrentEditor(Previous);
            }
        }
    };

    void CopyToBuffer(char* Buffer, size_t BufferSize, const FString& Value)
    {
        if (!Buffer || BufferSize == 0) return;
        std::snprintf(Buffer, BufferSize, "%s", Value.c_str());
    }

    struct FDeferredTooltip
    {
        bool    bVisible = false;
        FString Title;
        FString Body;
    };

    void SetDeferredTooltip(FDeferredTooltip& Tooltip, const FString& Title, const FString& Body)
    {
        Tooltip.bVisible = true;
        Tooltip.Title = Title;
        Tooltip.Body = Body;
    }

    void RenderDeferredTooltip(const FDeferredTooltip& Tooltip)
    {
        if (!Tooltip.bVisible)
        {
            return;
        }

        ImGui::BeginTooltip();
        if (!Tooltip.Title.empty())
        {
            ImGui::TextUnformatted(Tooltip.Title.c_str());
            if (!Tooltip.Body.empty())
            {
                ImGui::Separator();
            }
        }
        if (!Tooltip.Body.empty())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
            ImGui::TextUnformatted(Tooltip.Body.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTooltip();
    }

    bool RenderBreakpointGlyphButton(bool bEnabled, bool bCanToggle, const char* Id, FDeferredTooltip& Tooltip)
    {
        const float LineHeight = ImGui::GetTextLineHeight();
        const float Size       = (std::max)(LineHeight + 4.0f, 18.0f);
        const ImVec2 Pos       = ImGui::GetCursorScreenPos();

        const bool bPressed = ImGui::InvisibleButton(Id ? Id : "##BreakpointGlyph", ImVec2(Size, Size));
        const bool bClicked = bCanToggle && bPressed;

        const ImVec2 Min = Pos;
        const ImVec2 Max(Pos.x + Size, Pos.y + Size);
        const ImVec2 Center((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f);
        const float  Radius = (std::max)(4.0f, Size * 0.28f);

        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        const bool bHovered = bCanToggle && ImGui::IsItemHovered();
        const ImU32 ActiveFill  = ImGui::ColorConvertFloat4ToU32(bHovered ? ImVec4(1.0f, 0.25f, 0.22f, 1.0f) : ImVec4(0.86f, 0.08f, 0.08f, 1.0f));
        const ImU32 ActiveRing  = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.70f, 0.70f, 1.0f));
        const ImU32 IdleRing    = ImGui::ColorConvertFloat4ToU32(bHovered ? ImVec4(0.88f, 0.88f, 0.88f, 1.0f) : ImVec4(0.58f, 0.58f, 0.58f, 1.0f));
        const ImU32 DisabledCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.36f, 0.36f, 0.36f, 0.65f));

        if (bEnabled)
        {
            DrawList->AddCircleFilled(Center, Radius, ActiveFill, 24);
            DrawList->AddCircle(Center, Radius + 1.0f, ActiveRing, 24, 1.0f);
        }
        else
        {
            DrawList->AddCircle(Center, Radius, bCanToggle ? IdleRing : DisabledCol, 24, 1.4f);
            if (!bCanToggle)
            {
                DrawList->AddCircleFilled(Center, Radius * 0.32f, DisabledCol, 12);
            }
        }

        if (ImGui::IsItemHovered())
        {
            if (bCanToggle)
            {
                SetDeferredTooltip(
                    Tooltip,
                    bEnabled ? "Disable breakpoint" : "Enable breakpoint",
                    "Toggle this node breakpoint. Shortcut: F9 while the node is selected."
                );
            }
            else
            {
                SetDeferredTooltip(
                    Tooltip,
                    "Pure data node",
                    "This node has no exec pin. Its value is inspected when an executable node consumes it."
                );
            }
        }

        return bCanToggle && bClicked;
    }

    // ── Material editor 와 동일한 스타일의 툴바 버튼 헬퍼 ──
    ImVec2 ToolbarButtonSize(const char* Label)
    {
        const ImVec2 TextSize = ImGui::CalcTextSize(Label ? Label : "");
        const float  PaddingX = ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f;
        return ImVec2((std::max)(86.0f, TextSize.x + PaddingX), 0.0f);
    }

    bool ToolbarButton(const char* Label)
    {
        return ImGui::Button(Label, ToolbarButtonSize(Label));
    }

    // 강조 색이 들어간 주요 액션 버튼(Compile/Save 등). Disabled 일 땐 회색 처리.
    bool ToolbarAccentButton(const char* Label, const ImVec4& Base, const ImVec4& Hover, bool bEnabled = true)
    {
        ImGui::BeginDisabled(!bEnabled);
        ImGui::PushStyleColor(ImGuiCol_Button, Base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hover);
        const bool bClicked = ImGui::Button(Label, ToolbarButtonSize(Label));
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        return bClicked;
    }

    // 핀이 어떤 링크에든 연결돼 있는지. 입력 핀은 단일 링크, 출력 핀은 다중 fan-out 가능.
    bool IsPinConnected(const ULuaBlueprintAsset* Blueprint, const FLuaBlueprintPin& Pin)
    {
        if (!Blueprint) return false;
        if (Pin.Kind == ELuaBlueprintPinKind::Input)
        {
            return Blueprint->FindLinkToInput(Pin.PinId) != nullptr;
        }
        for (const FLuaBlueprintLink& Link : Blueprint->GetLinks())
        {
            if (Link.FromPinId == Pin.PinId) return true;
        }
        return false;
    }

    // UE Blueprint 스타일 핀 아이콘. Exec 은 삼각형, 데이터는 원. 연결되면 채우고, 아니면 외곽선만.
    // ed::BeginPin/EndPin 사이에서 호출 — 예약된 사각 영역이 곧 핀의 hit-rect 가 된다.
    void DrawPinIcon(bool bConnected, bool bExec, const ImVec4& Color)
    {
        const float  Size = ImGui::GetTextLineHeight();
        const ImVec2 P    = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(Size, Size));

        ImDrawList*  DL  = ImGui::GetWindowDrawList();
        const ImU32  Col = ImGui::ColorConvertFloat4ToU32(Color);
        const ImVec2 C(P.x + Size * 0.5f, P.y + Size * 0.5f);
        const float  R = Size * 0.34f;

        if (bExec)
        {
            const ImVec2 A(C.x - R * 0.75f, C.y - R);
            const ImVec2 B(C.x - R * 0.75f, C.y + R);
            const ImVec2 D(C.x + R, C.y);
            if (bConnected) DL->AddTriangleFilled(A, B, D, Col);
            else DL->AddTriangle(A, B, D, Col, 1.6f);
        }
        else
        {
            if (bConnected) DL->AddCircleFilled(C, R, Col, 16);
            else DL->AddCircle(C, R, Col, 16, 1.6f);
        }
    }

    // Pin-drag 추천 메뉴 전용 타입 매칭. 실제 링크가 허용하는 광범위한 암시적 변환
    // (무엇이든 -> String/Bool, scalar -> Vector 등)을 추천에서 제외해 정확도를 높인다.
    // 추천은 정확 타입 / 숫자 자동 변환(Int<->Float) / wildcard(Any) / Exec 만 매칭한다.
    // (그래도 사용자가 핀 위로 직접 드롭하면 넓은 변환 규칙은 그대로 동작한다.)
    bool ArePinTypesRecommendable(ELuaBlueprintPinType OutputType, ELuaBlueprintPinType InputType)
    {
        if (OutputType == ELuaBlueprintPinType::Exec || InputType == ELuaBlueprintPinType::Exec)
        {
            return OutputType == ELuaBlueprintPinType::Exec && InputType == ELuaBlueprintPinType::Exec;
        }
        if (OutputType == InputType) return true;
        if (OutputType == ELuaBlueprintPinType::Any || InputType == ELuaBlueprintPinType::Any) return true;

        auto IsObjectPinType = [](ELuaBlueprintPinType T)
        {
            switch (T)
            {
            case ELuaBlueprintPinType::Object:
            case ELuaBlueprintPinType::Actor:
            case ELuaBlueprintPinType::Pawn:
            case ELuaBlueprintPinType::PlayerController:
            case ELuaBlueprintPinType::ActorComponent:
            case ELuaBlueprintPinType::SceneComponent:
            case ELuaBlueprintPinType::PrimitiveComponent:
            case ELuaBlueprintPinType::StaticMesh:
            case ELuaBlueprintPinType::StaticMeshComponent:
            case ELuaBlueprintPinType::SkinnedMeshComponent:
            case ELuaBlueprintPinType::SkeletalMeshComponent:
            case ELuaBlueprintPinType::CameraComponent:
            case ELuaBlueprintPinType::CineCameraComponent:
            case ELuaBlueprintPinType::Material:
            case ELuaBlueprintPinType::Texture:
            case ELuaBlueprintPinType::AnimInstance:
            case ELuaBlueprintPinType::LuaBlueprintComponent:
            case ELuaBlueprintPinType::LuaScriptComponent:
                return true;
            default:
                return false;
            }
        };
        auto IsDerivedObjectType = [](ELuaBlueprintPinType Derived, ELuaBlueprintPinType Base)
        {
            if (Derived == Base) return true;
            if (Base == ELuaBlueprintPinType::Object) return true;
            if (Base == ELuaBlueprintPinType::Actor && Derived == ELuaBlueprintPinType::Pawn) return true;
            if (Base == ELuaBlueprintPinType::ActorComponent &&
                (Derived == ELuaBlueprintPinType::SceneComponent ||
                 Derived == ELuaBlueprintPinType::PrimitiveComponent ||
                 Derived == ELuaBlueprintPinType::StaticMeshComponent ||
                 Derived == ELuaBlueprintPinType::SkinnedMeshComponent ||
                 Derived == ELuaBlueprintPinType::SkeletalMeshComponent ||
                 Derived == ELuaBlueprintPinType::CameraComponent ||
                 Derived == ELuaBlueprintPinType::CineCameraComponent ||
                 Derived == ELuaBlueprintPinType::LuaBlueprintComponent ||
                 Derived == ELuaBlueprintPinType::LuaScriptComponent)) return true;
            if (Base == ELuaBlueprintPinType::SceneComponent &&
                (Derived == ELuaBlueprintPinType::PrimitiveComponent ||
                 Derived == ELuaBlueprintPinType::StaticMeshComponent ||
                 Derived == ELuaBlueprintPinType::SkinnedMeshComponent ||
                 Derived == ELuaBlueprintPinType::SkeletalMeshComponent ||
                 Derived == ELuaBlueprintPinType::CameraComponent ||
                 Derived == ELuaBlueprintPinType::CineCameraComponent)) return true;
            if (Base == ELuaBlueprintPinType::PrimitiveComponent &&
                (Derived == ELuaBlueprintPinType::StaticMeshComponent ||
                 Derived == ELuaBlueprintPinType::SkinnedMeshComponent ||
                 Derived == ELuaBlueprintPinType::SkeletalMeshComponent)) return true;
            if (Base == ELuaBlueprintPinType::SkinnedMeshComponent &&
                Derived == ELuaBlueprintPinType::SkeletalMeshComponent) return true;
            if (Base == ELuaBlueprintPinType::CameraComponent &&
                Derived == ELuaBlueprintPinType::CineCameraComponent) return true;
            return false;
        };
        if (IsObjectPinType(OutputType) && IsObjectPinType(InputType))
        {
            return IsDerivedObjectType(OutputType, InputType);
        }
        if ((OutputType == ELuaBlueprintPinType::Int && InputType == ELuaBlueprintPinType::Float) ||
            (OutputType == ELuaBlueprintPinType::Float && InputType == ELuaBlueprintPinType::Int))
        {
            return true;
        }
        return false;
    }

    const char* NodeTypeLabel(ELuaBlueprintNodeType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintNodeType::EventBeginPlay:
            return "Event BeginPlay";
        case ELuaBlueprintNodeType::EventTick:
            return "Event Tick";
        case ELuaBlueprintNodeType::EventEndPlay:
            return "Event EndPlay";
        case ELuaBlueprintNodeType::EventOverlap:
            return "Event Overlap";
        case ELuaBlueprintNodeType::EventEndOverlap:
            return "Event EndOverlap";
        case ELuaBlueprintNodeType::EventHit:
            return "Event Hit";
        case ELuaBlueprintNodeType::EventEndHit:
            return "Event EndHit";
        case ELuaBlueprintNodeType::EventPostBeginPlay:
            return "Event PostBeginPlay";
        case ELuaBlueprintNodeType::EventPostStartMatch:
            return "Event PostStartMatch";
        case ELuaBlueprintNodeType::EventPlayerCameraReady:
            return "Event OnPlayerCameraReady";
        case ELuaBlueprintNodeType::EventInputAction:
            return "Event InputAction";
        case ELuaBlueprintNodeType::EventInputAxis:
            return "Event InputAxis";
        case ELuaBlueprintNodeType::Sequence:
            return "Sequence";
        case ELuaBlueprintNodeType::Branch:
            return "Branch";
        case ELuaBlueprintNodeType::ForLoop:
            return "For Loop";
        case ELuaBlueprintNodeType::WhileLoop:
            return "While Loop";
        case ELuaBlueprintNodeType::PrintString:
            return "Print String";
        case ELuaBlueprintNodeType::LiteralBool:
            return "Literal Bool";
        case ELuaBlueprintNodeType::LiteralInt:
            return "Literal Int";
        case ELuaBlueprintNodeType::LiteralFloat:
            return "Literal Float";
        case ELuaBlueprintNodeType::LiteralString:
            return "Literal String";
        case ELuaBlueprintNodeType::LiteralVector:
            return "Literal Vector";
        case ELuaBlueprintNodeType::GetVariable:
            return "Get Variable";
        case ELuaBlueprintNodeType::SetVariable:
            return "Set Variable";
        case ELuaBlueprintNodeType::GetProperty:
            return "Get Property";
        case ELuaBlueprintNodeType::SetProperty:
            return "Set Property";
        case ELuaBlueprintNodeType::CallFunction:
            return "Call Function";
        case ELuaBlueprintNodeType::CallFunctionSignature:
            return "Call Signature";
        case ELuaBlueprintNodeType::Self:
            return "Self (Owning Actor)";
        case ELuaBlueprintNodeType::AddFloat:
            return "Float + Float";
        case ELuaBlueprintNodeType::SubtractFloat:
            return "Float - Float";
        case ELuaBlueprintNodeType::MultiplyFloat:
            return "Float * Float";
        case ELuaBlueprintNodeType::DivideFloat:
            return "Float / Float";
        case ELuaBlueprintNodeType::AddInt:
            return "Int + Int";
        case ELuaBlueprintNodeType::SubtractInt:
            return "Int - Int";
        case ELuaBlueprintNodeType::MultiplyInt:
            return "Int * Int";
        case ELuaBlueprintNodeType::DivideInt:
            return "Int / Int";
        case ELuaBlueprintNodeType::ModInt:
            return "Int % Int";
        case ELuaBlueprintNodeType::EqualFloat:
            return "Float == Float";
        case ELuaBlueprintNodeType::NotEqualFloat:
            return "Float != Float";
        case ELuaBlueprintNodeType::LessFloat:
            return "Float < Float";
        case ELuaBlueprintNodeType::GreaterFloat:
            return "Float > Float";
        case ELuaBlueprintNodeType::LessEqualFloat:
            return "Float <= Float";
        case ELuaBlueprintNodeType::GreaterEqualFloat:
            return "Float >= Float";
        case ELuaBlueprintNodeType::EqualInt:
            return "Int == Int";
        case ELuaBlueprintNodeType::NotEqualInt:
            return "Int != Int";
        case ELuaBlueprintNodeType::LessInt:
            return "Int < Int";
        case ELuaBlueprintNodeType::GreaterInt:
            return "Int > Int";
        case ELuaBlueprintNodeType::And:
            return "Bool AND Bool";
        case ELuaBlueprintNodeType::Or:
            return "Bool OR Bool";
        case ELuaBlueprintNodeType::Not:
            return "NOT Bool";
        case ELuaBlueprintNodeType::AppendString:
            return "Append String";
        case ELuaBlueprintNodeType::MakeVector:
            return "Make Vector";
        case ELuaBlueprintNodeType::BreakVector:
            return "Break Vector";
        case ELuaBlueprintNodeType::AddVector:
            return "Vector + Vector";
        case ELuaBlueprintNodeType::SubtractVector:
            return "Vector - Vector";
        case ELuaBlueprintNodeType::ScaleVector:
            return "Vector * Float";
        case ELuaBlueprintNodeType::DotVector:
            return "Dot";
        case ELuaBlueprintNodeType::CrossVector:
            return "Cross";
        case ELuaBlueprintNodeType::VectorLength:
            return "Vector Length";
        case ELuaBlueprintNodeType::NormalizeVector:
            return "Normalize";
        case ELuaBlueprintNodeType::SpawnActor:
            return "Spawn Actor";
        case ELuaBlueprintNodeType::SpawnPawn:
            return "Spawn Pawn";
        case ELuaBlueprintNodeType::SpawnPawnAndPossess:
            return "Spawn Pawn and Possess";
        case ELuaBlueprintNodeType::DestroyActor:
            return "Destroy Actor";
        case ELuaBlueprintNodeType::FindActorByName:
            return "Find Actor by Name";
        case ELuaBlueprintNodeType::FindActorByClass:
            return "Find Actor of Class";
        case ELuaBlueprintNodeType::FindActorByTag:
            return "Find Actor with Tag";
        case ELuaBlueprintNodeType::FindActorsByTag:
            return "Find Actors with Tag";
        case ELuaBlueprintNodeType::FindActorsByClass:
            return "Find Actors of Class";
        case ELuaBlueprintNodeType::GetActorLocation:
            return "Get Actor Location";
        case ELuaBlueprintNodeType::SetActorLocation:
            return "Set Actor Location";
        case ELuaBlueprintNodeType::GetActorRotation:
            return "Get Actor Rotation";
        case ELuaBlueprintNodeType::SetActorRotation:
            return "Set Actor Rotation";
        case ELuaBlueprintNodeType::GetActorScale:
            return "Get Actor Scale";
        case ELuaBlueprintNodeType::SetActorScale:
            return "Set Actor Scale";
        case ELuaBlueprintNodeType::GetActorForward:
            return "Get Actor Forward";
        case ELuaBlueprintNodeType::GetActorRight:
            return "Get Actor Right";
        case ELuaBlueprintNodeType::AddActorWorldOffset:
            return "Add Actor World Offset";
        case ELuaBlueprintNodeType::ActorHasTag:
            return "Actor Has Tag";
        case ELuaBlueprintNodeType::ActorAddTag:
            return "Actor Add Tag";
        case ELuaBlueprintNodeType::ActorRemoveTag:
            return "Actor Remove Tag";
        case ELuaBlueprintNodeType::GetActorName:
            return "Get Actor Name";
        case ELuaBlueprintNodeType::GetOwnerActor:
            return "Get Owner Actor";
        case ELuaBlueprintNodeType::GetPlayerController:
            return "Get Player Controller";
        case ELuaBlueprintNodeType::GetController:
            return "Get Controller";
        case ELuaBlueprintNodeType::GetControlledPawn:
            return "Get Controlled Pawn";
        case ELuaBlueprintNodeType::Possess:
            return "Possess";
        case ELuaBlueprintNodeType::UnPossess:
            return "UnPossess";
        case ELuaBlueprintNodeType::IsPawnPossessed:
            return "Is Pawn Possessed";
        case ELuaBlueprintNodeType::GetInputComponent:
            return "Get Input Component";
        case ELuaBlueprintNodeType::VehicleSetThrottle:
            return "Vehicle Set Throttle";
        case ELuaBlueprintNodeType::VehicleSetBrake:
            return "Vehicle Set Brake";
        case ELuaBlueprintNodeType::VehicleSetSteering:
            return "Vehicle Set Steering";
        case ELuaBlueprintNodeType::VehicleSetHandbrake:
            return "Vehicle Set Handbrake";
        case ELuaBlueprintNodeType::VehicleReset:
            return "Vehicle Reset";
        case ELuaBlueprintNodeType::VehicleGetForwardSpeed:
            return "Vehicle Get Forward Speed";
        case ELuaBlueprintNodeType::ParticleSetTemplateByPath:
            return "Particle Set Template By Path";
        case ELuaBlueprintNodeType::ParticleActivate:
            return "Particle Activate";
        case ELuaBlueprintNodeType::ParticleDeactivate:
            return "Particle Deactivate";
        case ELuaBlueprintNodeType::ParticleReset:
            return "Particle Reset";
        case ELuaBlueprintNodeType::ParticleRebuild:
            return "Particle Rebuild";
        case ELuaBlueprintNodeType::ParticleSetLOD:
            return "Particle Set LOD";
        case ELuaBlueprintNodeType::AddForceToRoot:
            return "Add Force To Root";
        case ELuaBlueprintNodeType::AddTorqueToRoot:
            return "Add Torque To Root";
        case ELuaBlueprintNodeType::AddImpulseToRoot:
            return "Add Impulse To Root";
        case ELuaBlueprintNodeType::GetRootLinearVelocity:
            return "Get Root Linear Velocity";
        case ELuaBlueprintNodeType::SetRootLinearVelocity:
            return "Set Root Linear Velocity";
        case ELuaBlueprintNodeType::SetRootSimulatePhysics:
            return "Set Root Simulate Physics";
        case ELuaBlueprintNodeType::IsValid:
            return "Is Valid";
        case ELuaBlueprintNodeType::Cast:
            return "Cast";
        case ELuaBlueprintNodeType::GetRootComponent:
            return "Get Root Component";
        case ELuaBlueprintNodeType::GetRootPrimitiveComponent:
            return "Get Root Primitive Component";
        case ELuaBlueprintNodeType::GetComponentByName:
            return "Get Component by Name";
        case ELuaBlueprintNodeType::GetPrimitiveComponent:
            return "Get Primitive Component";
        case ELuaBlueprintNodeType::ActivateComponent:
            return "Activate";
        case ELuaBlueprintNodeType::DeactivateComponent:
            return "Deactivate";
        case ELuaBlueprintNodeType::AddForce:
            return "Add Force";
        case ELuaBlueprintNodeType::AddTorque:
            return "Add Torque";
        case ELuaBlueprintNodeType::AddImpulse:
            return "Add Impulse";
        case ELuaBlueprintNodeType::GetLinearVelocity:
            return "Get Linear Velocity";
        case ELuaBlueprintNodeType::SetLinearVelocity:
            return "Set Linear Velocity";
        case ELuaBlueprintNodeType::GetMass:
            return "Get Mass";
        case ELuaBlueprintNodeType::SetSimulatePhysics:
            return "Set Simulate Physics";
        case ELuaBlueprintNodeType::Lerp:
            return "Lerp";
        case ELuaBlueprintNodeType::Clamp:
            return "Clamp";
        case ELuaBlueprintNodeType::Min:
            return "Min";
        case ELuaBlueprintNodeType::Max:
            return "Max";
        case ELuaBlueprintNodeType::RandomFloat:
            return "Random Float";
        case ELuaBlueprintNodeType::RandomInt:
            return "Random Int";
        case ELuaBlueprintNodeType::Sin:
            return "Sin";
        case ELuaBlueprintNodeType::Cos:
            return "Cos";
        case ELuaBlueprintNodeType::Sqrt:
            return "Sqrt";
        case ELuaBlueprintNodeType::AbsFloat:
            return "Abs";
        case ELuaBlueprintNodeType::Floor:
            return "Floor";
        case ELuaBlueprintNodeType::Ceil:
            return "Ceil";
        case ELuaBlueprintNodeType::Distance:
            return "Distance";
        case ELuaBlueprintNodeType::GetGameTime:
            return "Get Game Time";
        case ELuaBlueprintNodeType::ForEachActorByClass:
            return "For Each Actor (Class)";
        case ELuaBlueprintNodeType::ForEachActorByTag:
            return "For Each Actor (Tag)";
        case ELuaBlueprintNodeType::ForEachArray:
            return "For Each Array";
        case ELuaBlueprintNodeType::Reroute:
            return "Reroute";
        case ELuaBlueprintNodeType::Comment:
            return "Comment";
        case ELuaBlueprintNodeType::CustomEvent:
            return "Custom Event";
        case ELuaBlueprintNodeType::CallCustomEvent:
            return "Call Custom Event";
        case ELuaBlueprintNodeType::Delay:
            return "Delay";
        case ELuaBlueprintNodeType::ToBool:
            return "To Bool";
        case ELuaBlueprintNodeType::ToInt:
            return "To Int";
        case ELuaBlueprintNodeType::ToFloat:
            return "To Float";
        case ELuaBlueprintNodeType::ToString:
            return "To String";
        case ELuaBlueprintNodeType::ToVector:
            return "To Vector";
        case ELuaBlueprintNodeType::BindEvent:
            return "Bind Event";
        case ELuaBlueprintNodeType::UnbindEvent:
            return "Unbind Event";
        case ELuaBlueprintNodeType::HasEventBinding:
            return "Has Event Binding";
        case ELuaBlueprintNodeType::SetTimer:
            return "Set Timer";
        case ELuaBlueprintNodeType::ClearTimer:
            return "Clear Timer";
        case ELuaBlueprintNodeType::IsTimerActive:
            return "Is Timer Active";
        case ELuaBlueprintNodeType::SetTimerForNextTick:
            return "Set Timer For Next Tick";
        case ELuaBlueprintNodeType::LineTrace:
            return "Line Trace";
        case ELuaBlueprintNodeType::CreateWidget:
            return "Create Widget";
        case ELuaBlueprintNodeType::AddWidgetToViewport:
            return "Add Widget To Viewport";
        case ELuaBlueprintNodeType::RemoveWidgetFromParent:
            return "Remove Widget From Parent";
        case ELuaBlueprintNodeType::SetWidgetText:
            return "Set Widget Text";
        case ELuaBlueprintNodeType::BindWidgetClick:
            return "Bind Widget Click";
        case ELuaBlueprintNodeType::LoadAudio:
            return "Load Audio";
        case ELuaBlueprintNodeType::AudioPlaySound:
            return "Play Sound";
        case ELuaBlueprintNodeType::PlayBGM:
            return "Play BGM";
        case ELuaBlueprintNodeType::StopBGM:
            return "Stop BGM";
        case ELuaBlueprintNodeType::PlayAudioLoop:
            return "Play Audio Loop";
        case ELuaBlueprintNodeType::StopAudioLoop:
            return "Stop Audio Loop";
        case ELuaBlueprintNodeType::SetAudioMasterVolume:
            return "Set Audio Master Volume";
        case ELuaBlueprintNodeType::LoadStaticMesh:
            return "Load Static Mesh";
        case ELuaBlueprintNodeType::GetStaticMeshComponent:
            return "Get Static Mesh Component";
        case ELuaBlueprintNodeType::GetStaticMesh:
            return "Get Static Mesh";
        case ELuaBlueprintNodeType::SetStaticMesh:
            return "Set Static Mesh";
        case ELuaBlueprintNodeType::SetStaticMeshByPath:
            return "Set Static Mesh By Path";
        case ELuaBlueprintNodeType::ClearStaticMesh:
            return "Clear Static Mesh";
        case ELuaBlueprintNodeType::GetLuaBlueprintComponent:
            return "Get Lua Blueprint Component";
        case ELuaBlueprintNodeType::GetLuaScriptComponent:
            return "Get Lua Script Component";
        case ELuaBlueprintNodeType::CallLuaBlueprintFunction:
            return "Call Lua Blueprint Function";
        case ELuaBlueprintNodeType::CallLuaScriptFunction:
            return "Call Lua Script Function";
        case ELuaBlueprintNodeType::CallLuaBlueprintFileFunction:
            return "Call Lua Blueprint File Function";
        case ELuaBlueprintNodeType::CallLuaScriptFileFunction:
            return "Call Lua Script File Function";
        case ELuaBlueprintNodeType::CustomLuaFunction:
            return "Custom Lua Function";
        case ELuaBlueprintNodeType::CallCustomLuaFunction:
            return "Call Custom Lua Function";
        case ELuaBlueprintNodeType::GetSkeletalMeshComponent:
            return "Get Skeletal Mesh Component";
        case ELuaBlueprintNodeType::SetSkeletalMeshByPath:
            return "Set Skeletal Mesh By Path";
        case ELuaBlueprintNodeType::ClearSkeletalMesh:
            return "Clear Skeletal Mesh";
        case ELuaBlueprintNodeType::PlayAnimationByPath:
            return "Play Animation By Path";
        case ELuaBlueprintNodeType::StopAnimation:
            return "Stop Animation";
        case ELuaBlueprintNodeType::SetAnimationByPath:
            return "Set Animation By Path";
        case ELuaBlueprintNodeType::SetAnimationPlayRate:
            return "Set Animation Play Rate";
        case ELuaBlueprintNodeType::SetAnimationLooping:
            return "Set Animation Looping";
        case ELuaBlueprintNodeType::SetAnimationPlaying:
            return "Set Animation Playing";
        case ELuaBlueprintNodeType::GetAnimInstance:
            return "Get Anim Instance";
        case ELuaBlueprintNodeType::SetAnimGraphVariableFloat:
            return "Set Anim Graph Float";
        case ELuaBlueprintNodeType::SetAnimGraphVariableBool:
            return "Set Anim Graph Bool";
        case ELuaBlueprintNodeType::SetAnimGraphVariableInt:
            return "Set Anim Graph Int";
        case ELuaBlueprintNodeType::GetAnimGraphVariableFloat:
            return "Get Anim Graph Float";
        case ELuaBlueprintNodeType::GetAnimGraphVariableBool:
            return "Get Anim Graph Bool";
        case ELuaBlueprintNodeType::GetAnimGraphVariableInt:
            return "Get Anim Graph Int";
        case ELuaBlueprintNodeType::AttachToComponent:
            return "Attach To Component";
        case ELuaBlueprintNodeType::GetAttachSocketName:
            return "Get Attach Socket Name";
        case ELuaBlueprintNodeType::HasSocket:
            return "Has Socket";
        case ELuaBlueprintNodeType::GetSocketWorldLocation:
            return "Get Socket World Location";
        case ELuaBlueprintNodeType::GetSocketWorldRotation:
            return "Get Socket World Rotation";
        case ELuaBlueprintNodeType::GetSocketWorldScale:
            return "Get Socket World Scale";
        case ELuaBlueprintNodeType::GetSocketForwardVector:
            return "Get Socket Forward Vector";
        case ELuaBlueprintNodeType::GetSocketRightVector:
            return "Get Socket Right Vector";
        case ELuaBlueprintNodeType::GetSocketUpVector:
            return "Get Socket Up Vector";
        case ELuaBlueprintNodeType::LoadMaterial:
            return "Load Material";
        case ELuaBlueprintNodeType::GetMaterial:
            return "Get Material";
        case ELuaBlueprintNodeType::SetMaterial:
            return "Set Material";
        case ELuaBlueprintNodeType::SetMaterialByPath:
            return "Set Material By Path";
        case ELuaBlueprintNodeType::CreateDynamicMaterialInstance:
            return "Create Dynamic Material Instance";
        case ELuaBlueprintNodeType::SetMaterialScalarParameter:
            return "Set Material Scalar Parameter";
        case ELuaBlueprintNodeType::SetMaterialVectorParameter:
            return "Set Material Vector Parameter";
        case ELuaBlueprintNodeType::SetMaterialColorParameter:
            return "Set Material Color Parameter";
        case ELuaBlueprintNodeType::SetMaterialTextureParameter:
            return "Set Material Texture Parameter";
        case ELuaBlueprintNodeType::LoadTexture:
            return "Load Texture";
        case ELuaBlueprintNodeType::GetCameraComponent:
            return "Get Camera Component";
        case ELuaBlueprintNodeType::GetActiveCamera:
            return "Get Active Camera";
        case ELuaBlueprintNodeType::PossessCamera:
            return "Possess Camera";
        case ELuaBlueprintNodeType::SetActiveCameraWithBlend:
            return "Set Active Camera With Blend";
        case ELuaBlueprintNodeType::SetViewTargetWithBlend:
            return "Set View Target With Blend";
        case ELuaBlueprintNodeType::SetCameraFOV:
            return "Set Camera FOV";
        case ELuaBlueprintNodeType::CameraLookAt:
            return "Camera Look At";
        case ELuaBlueprintNodeType::FadeIn:
            return "Fade In";
        case ELuaBlueprintNodeType::FadeOut:
            return "Fade Out";
        case ELuaBlueprintNodeType::SetVignette:
            return "Set Vignette";
        case ELuaBlueprintNodeType::ClearVignette:
            return "Clear Vignette";
        case ELuaBlueprintNodeType::StartCameraShakeAsset:
            return "Start Camera Shake Asset";
        case ELuaBlueprintNodeType::SetDepthOfField:
            return "Set Depth Of Field";
        case ELuaBlueprintNodeType::SetBokeh:
            return "Set Bokeh";
        case ELuaBlueprintNodeType::ClearDepthOfField:
            return "Clear Depth Of Field";
        case ELuaBlueprintNodeType::SetLetterbox:
            return "Set Letterbox";
        case ELuaBlueprintNodeType::ClearLetterbox:
            return "Clear Letterbox";
        }
        return "Node";
    }

    FString NodeHeaderDisplayLabel(const FLuaBlueprintNode& Node)
    {
        switch (Node.Type)
        {
        case ELuaBlueprintNodeType::GetVariable:
            if (Node.NameValue != FName::None)
            {
                return FString("Get ") + Node.NameValue.ToString();
            }
            break;
        case ELuaBlueprintNodeType::SetVariable:
            if (Node.NameValue != FName::None)
            {
                return FString("Set ") + Node.NameValue.ToString();
            }
            break;
        case ELuaBlueprintNodeType::CallFunctionSignature:
            if (Node.DisplayName != FName::None)
            {
                return Node.DisplayName.ToString();
            }
            if (Node.NameValue != FName::None)
            {
                return FString("Call ") + Node.NameValue.ToString();
            }
            if (!Node.StringValue.empty())
            {
                return FString("Call ") + Node.StringValue;
            }
            break;
        default:
            break;
        }
        return FString(NodeTypeLabel(Node.Type));
    }

    const char* NodeTypeHelpText(ELuaBlueprintNodeType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintNodeType::EventBeginPlay:
            return "Exec entry fired when the owning actor begins play.";
        case ELuaBlueprintNodeType::EventTick:
            return "Exec entry fired every frame. DeltaSeconds carries frame time.";
        case ELuaBlueprintNodeType::EventEndPlay:
            return "Exec entry fired when the owning actor ends play.";
        case ELuaBlueprintNodeType::EventOverlap:
            return "Exec entry fired on overlap begin, with actor/component payloads.";
        case ELuaBlueprintNodeType::EventEndOverlap:
            return "Exec entry fired on overlap end.";
        case ELuaBlueprintNodeType::EventHit:
            return "Exec entry fired when a blocking hit occurs.";
        case ELuaBlueprintNodeType::EventEndHit:
            return "Exec entry fired for the matching hit-end event if available.";
        case ELuaBlueprintNodeType::EventPostBeginPlay:
            return "Exec entry fired after all current world actors have completed BeginPlay.";
        case ELuaBlueprintNodeType::EventPostStartMatch:
            return "Exec entry fired after GameMode StartMatch has completed.";
        case ELuaBlueprintNodeType::EventPlayerCameraReady:
            return "Exec entry fired after the player camera manager has been created and camera registration is ready.";
        case ELuaBlueprintNodeType::Sequence:
            return "Runs multiple exec outputs in order.";
        case ELuaBlueprintNodeType::Branch:
            return "Chooses True or False exec flow from a Bool condition.";
        case ELuaBlueprintNodeType::ForLoop:
            return "Loops from FirstIndex to LastIndex and then fires Completed.";
        case ELuaBlueprintNodeType::WhileLoop:
            return "Repeats while Condition remains true. Keep the condition safe.";
        case ELuaBlueprintNodeType::PrintString:
            return "Prints text for debugging or quick feedback.";
        case ELuaBlueprintNodeType::LiteralBool:
            return "Constant Bool value.";
        case ELuaBlueprintNodeType::LiteralInt:
            return "Constant Int value.";
        case ELuaBlueprintNodeType::LiteralFloat:
            return "Constant Float value.";
        case ELuaBlueprintNodeType::LiteralString:
            return "Constant String value.";
        case ELuaBlueprintNodeType::LiteralVector:
            return "Constant Vector value.";
        case ELuaBlueprintNodeType::GetVariable:
            return "Reads a graph variable.";
        case ELuaBlueprintNodeType::SetVariable:
            return "Writes a graph variable and continues exec flow.";
        case ELuaBlueprintNodeType::GetProperty:
            return "Reads a named reflected property from the target object.";
        case ELuaBlueprintNodeType::SetProperty:
            return "Writes a named reflected property on the target object.";
        case ELuaBlueprintNodeType::CallFunction:
            return "Calls a reflected function by name.";
        case ELuaBlueprintNodeType::CallFunctionSignature:
            return "Calls a function using an explicit signature string.";
        case ELuaBlueprintNodeType::Self:
            return "Returns the owning actor/object for this Lua Blueprint.";
        case ELuaBlueprintNodeType::AddFloat:
            return "Float addition.";
        case ELuaBlueprintNodeType::SubtractFloat:
            return "Float subtraction.";
        case ELuaBlueprintNodeType::MultiplyFloat:
            return "Float multiplication.";
        case ELuaBlueprintNodeType::DivideFloat:
            return "Float division.";
        case ELuaBlueprintNodeType::AddInt:
            return "Integer addition.";
        case ELuaBlueprintNodeType::SubtractInt:
            return "Integer subtraction.";
        case ELuaBlueprintNodeType::MultiplyInt:
            return "Integer multiplication.";
        case ELuaBlueprintNodeType::DivideInt:
            return "Integer division.";
        case ELuaBlueprintNodeType::ModInt:
            return "Integer remainder.";
        case ELuaBlueprintNodeType::EqualFloat:
            return "Float equality comparison.";
        case ELuaBlueprintNodeType::NotEqualFloat:
            return "Float inequality comparison.";
        case ELuaBlueprintNodeType::LessFloat:
            return "Float less-than comparison.";
        case ELuaBlueprintNodeType::GreaterFloat:
            return "Float greater-than comparison.";
        case ELuaBlueprintNodeType::LessEqualFloat:
            return "Float less-or-equal comparison.";
        case ELuaBlueprintNodeType::GreaterEqualFloat:
            return "Float greater-or-equal comparison.";
        case ELuaBlueprintNodeType::EqualInt:
            return "Integer equality comparison.";
        case ELuaBlueprintNodeType::NotEqualInt:
            return "Integer inequality comparison.";
        case ELuaBlueprintNodeType::LessInt:
            return "Integer less-than comparison.";
        case ELuaBlueprintNodeType::GreaterInt:
            return "Integer greater-than comparison.";
        case ELuaBlueprintNodeType::And:
            return "Boolean AND.";
        case ELuaBlueprintNodeType::Or:
            return "Boolean OR.";
        case ELuaBlueprintNodeType::Not:
            return "Boolean NOT.";
        case ELuaBlueprintNodeType::AppendString:
            return "Concatenates strings.";
        case ELuaBlueprintNodeType::MakeVector:
            return "Builds a Vector from X/Y/Z components.";
        case ELuaBlueprintNodeType::BreakVector:
            return "Splits a Vector into X/Y/Z components.";
        case ELuaBlueprintNodeType::AddVector:
            return "Vector addition.";
        case ELuaBlueprintNodeType::SubtractVector:
            return "Vector subtraction.";
        case ELuaBlueprintNodeType::ScaleVector:
            return "Scales a Vector by a Float.";
        case ELuaBlueprintNodeType::DotVector:
            return "Dot product of two Vectors.";
        case ELuaBlueprintNodeType::CrossVector:
            return "Cross product of two Vectors.";
        case ELuaBlueprintNodeType::VectorLength:
            return "Returns Vector length.";
        case ELuaBlueprintNodeType::NormalizeVector:
            return "Returns a normalized Vector direction.";
        case ELuaBlueprintNodeType::SpawnActor:
            return "Spawns an actor of the configured class.";
        case ELuaBlueprintNodeType::DestroyActor:
            return "Destroys the target actor.";
        case ELuaBlueprintNodeType::FindActorByName:
            return "Finds one actor by name.";
        case ELuaBlueprintNodeType::FindActorByClass:
            return "Finds one actor by class.";
        case ELuaBlueprintNodeType::FindActorByTag:
            return "Finds one actor with the tag.";
        case ELuaBlueprintNodeType::FindActorsByTag:
            return "Finds all actors with the tag.";
        case ELuaBlueprintNodeType::FindActorsByClass:
            return "Finds all actors of the class.";
        case ELuaBlueprintNodeType::GetActorLocation:
            return "Reads target actor location.";
        case ELuaBlueprintNodeType::SetActorLocation:
            return "Sets target actor location.";
        case ELuaBlueprintNodeType::GetActorRotation:
            return "Reads target actor rotation.";
        case ELuaBlueprintNodeType::SetActorRotation:
            return "Sets target actor rotation.";
        case ELuaBlueprintNodeType::GetActorScale:
            return "Reads target actor scale.";
        case ELuaBlueprintNodeType::SetActorScale:
            return "Sets target actor scale.";
        case ELuaBlueprintNodeType::GetActorForward:
            return "Returns target actor forward vector.";
        case ELuaBlueprintNodeType::GetActorRight:
            return "Returns target actor right vector.";
        case ELuaBlueprintNodeType::AddActorWorldOffset:
            return "Moves an actor by a world-space offset.";
        case ELuaBlueprintNodeType::ActorHasTag:
            return "Checks whether an actor owns a tag.";
        case ELuaBlueprintNodeType::ActorAddTag:
            return "Adds a tag to an actor.";
        case ELuaBlueprintNodeType::ActorRemoveTag:
            return "Removes a tag from an actor.";
        case ELuaBlueprintNodeType::GetActorName:
            return "Returns the actor name.";
        case ELuaBlueprintNodeType::GetOwnerActor:
            return "Returns the owner actor when available.";
        case ELuaBlueprintNodeType::GetPlayerController:
            return "Returns the primary local PlayerController.";
        case ELuaBlueprintNodeType::GetController:
            return "Returns the Controller currently possessing a Pawn.";
        case ELuaBlueprintNodeType::GetControlledPawn:
            return "Returns the Pawn currently possessed by a Controller.";
        case ELuaBlueprintNodeType::Possess:
            return "Makes a PlayerController possess the target Pawn.";
        case ELuaBlueprintNodeType::UnPossess:
            return "Detaches the Controller from its current Pawn.";
        case ELuaBlueprintNodeType::IsPawnPossessed:
            return "Checks whether a Pawn has a Controller.";
        case ELuaBlueprintNodeType::GetInputComponent:
            return "Returns a Pawn's InputComponent.";
        case ELuaBlueprintNodeType::VehicleSetThrottle:
            return "Sets throttle on a target vehicle.";
        case ELuaBlueprintNodeType::VehicleSetBrake:
            return "Sets brake on a target vehicle.";
        case ELuaBlueprintNodeType::VehicleSetSteering:
            return "Sets steering on a target vehicle.";
        case ELuaBlueprintNodeType::VehicleSetHandbrake:
            return "Sets handbrake on a target vehicle.";
        case ELuaBlueprintNodeType::VehicleReset:
            return "Resets the target vehicle.";
        case ELuaBlueprintNodeType::VehicleGetForwardSpeed:
            return "Reads target vehicle forward speed.";
        case ELuaBlueprintNodeType::ParticleSetTemplateByPath:
            return "Sets a particle template asset by path.";
        case ELuaBlueprintNodeType::ParticleActivate:
            return "Activates target particle component.";
        case ELuaBlueprintNodeType::ParticleDeactivate:
            return "Deactivates target particle component.";
        case ELuaBlueprintNodeType::ParticleReset:
            return "Resets target particle component.";
        case ELuaBlueprintNodeType::ParticleRebuild:
            return "Rebuilds target particle instances.";
        case ELuaBlueprintNodeType::ParticleSetLOD:
            return "Sets target particle LOD index.";
        case ELuaBlueprintNodeType::IsValid:
            return "Checks whether an object reference is valid.";
        case ELuaBlueprintNodeType::Cast:
            return "Casts an object reference to the configured class.";
        case ELuaBlueprintNodeType::GetRootComponent:
            return "Gets an actor root component.";
        case ELuaBlueprintNodeType::GetComponentByName:
            return "Gets a component by name.";
        case ELuaBlueprintNodeType::GetPrimitiveComponent:
            return "Gets a primitive component from the actor/component target.";
        case ELuaBlueprintNodeType::ActivateComponent:
            return "Activates the target component.";
        case ELuaBlueprintNodeType::DeactivateComponent:
            return "Deactivates the target component.";
        case ELuaBlueprintNodeType::AddForce:
            return "Applies force to a primitive component.";
        case ELuaBlueprintNodeType::AddTorque:
            return "Applies torque to a primitive component.";
        case ELuaBlueprintNodeType::GetLinearVelocity:
            return "Reads linear velocity from a primitive component.";
        case ELuaBlueprintNodeType::SetLinearVelocity:
            return "Sets linear velocity on a primitive component.";
        case ELuaBlueprintNodeType::GetMass:
            return "Reads mass from a primitive component.";
        case ELuaBlueprintNodeType::SetSimulatePhysics:
            return "Enables or disables physics simulation.";
        case ELuaBlueprintNodeType::Lerp:
            return "Linearly interpolates between two values.";
        case ELuaBlueprintNodeType::Clamp:
            return "Clamps a value between Min and Max.";
        case ELuaBlueprintNodeType::Min:
            return "Returns the smaller value.";
        case ELuaBlueprintNodeType::Max:
            return "Returns the larger value.";
        case ELuaBlueprintNodeType::RandomFloat:
            return "Returns a random Float in range.";
        case ELuaBlueprintNodeType::RandomInt:
            return "Returns a random Int in range.";
        case ELuaBlueprintNodeType::Sin:
            return "Sine of the input angle/value.";
        case ELuaBlueprintNodeType::Cos:
            return "Cosine of the input angle/value.";
        case ELuaBlueprintNodeType::Sqrt:
            return "Square root.";
        case ELuaBlueprintNodeType::AbsFloat:
            return "Absolute Float value.";
        case ELuaBlueprintNodeType::Floor:
            return "Rounds down.";
        case ELuaBlueprintNodeType::Ceil:
            return "Rounds up.";
        case ELuaBlueprintNodeType::Distance:
            return "Distance between two vectors.";
        case ELuaBlueprintNodeType::GetGameTime:
            return "Returns current game time.";
        case ELuaBlueprintNodeType::ForEachActorByClass:
            return "Iterates actors of a class.";
        case ELuaBlueprintNodeType::ForEachActorByTag:
            return "Iterates actors with a tag.";
        case ELuaBlueprintNodeType::ForEachArray:
            return "Iterates elements in an array.";
        case ELuaBlueprintNodeType::Reroute:
            return "Visual pass-through node for cleaner wiring.";
        case ELuaBlueprintNodeType::Comment:
            return "Resizable group/comment box. Deleting it also deletes contained nodes.";
        case ELuaBlueprintNodeType::CustomEvent:
            return "Defines a custom exec entry that can be called or bound.";
        case ELuaBlueprintNodeType::CallCustomEvent:
            return "Calls a Custom Event by name.";
        case ELuaBlueprintNodeType::CustomLuaFunction:
            return "Defines a user-authored Lua function body stored inside this Blueprint. Double-click the node body or use Edit Code to open the editor.";
        case ELuaBlueprintNodeType::CallCustomLuaFunction:
            return "Calls a Custom Lua Function defined in this Blueprint and exposes Success and Return outputs.";
        case ELuaBlueprintNodeType::Delay:
            return "Waits for a duration before continuing exec flow.";
        case ELuaBlueprintNodeType::ToBool:
            return "Converts a value to Bool.";
        case ELuaBlueprintNodeType::ToInt:
            return "Converts a value to Int.";
        case ELuaBlueprintNodeType::ToFloat:
            return "Converts a value to Float.";
        case ELuaBlueprintNodeType::ToString:
            return "Converts a value to String.";
        case ELuaBlueprintNodeType::ToVector:
            return "Converts a value to Vector.";
        case ELuaBlueprintNodeType::BindEvent:
            return "Binds a reflected event to a Custom Event callback.";
        case ELuaBlueprintNodeType::UnbindEvent:
            return "Removes a reflected event binding.";
        case ELuaBlueprintNodeType::HasEventBinding:
            return "Checks whether a reflected event binding exists.";
        case ELuaBlueprintNodeType::SetTimer:
            return "Starts or restarts a graph timer that invokes a Custom Event.";
        case ELuaBlueprintNodeType::ClearTimer:
            return "Stops a graph timer by id.";
        case ELuaBlueprintNodeType::IsTimerActive:
            return "Checks whether a graph timer id is active.";
        case ELuaBlueprintNodeType::SetTimerForNextTick:
            return "Runs a Custom Event on the next tick and continues immediately.";
        case ELuaBlueprintNodeType::LineTrace:
            return "Performs a world ray query and returns hit payload fields.";
        case ELuaBlueprintNodeType::CreateWidget:
            return "Creates a UI widget/document object from a path.";
        case ELuaBlueprintNodeType::AddWidgetToViewport:
            return "Adds a widget to the viewport.";
        case ELuaBlueprintNodeType::RemoveWidgetFromParent:
            return "Removes a widget from its parent/viewport.";
        case ELuaBlueprintNodeType::SetWidgetText:
            return "Sets text on a named UI element.";
        case ELuaBlueprintNodeType::BindWidgetClick:
            return "Binds a UI element click to a Custom Event.";
        case ELuaBlueprintNodeType::LoadAudio:
            return "Loads/registers an audio asset under a key.";
        case ELuaBlueprintNodeType::AudioPlaySound:
            return "Plays a loaded one-shot sound.";
        case ELuaBlueprintNodeType::PlayBGM:
            return "Starts background music by key.";
        case ELuaBlueprintNodeType::StopBGM:
            return "Stops the active background music.";
        case ELuaBlueprintNodeType::PlayAudioLoop:
            return "Starts a named looping audio instance.";
        case ELuaBlueprintNodeType::StopAudioLoop:
            return "Stops a named looping audio instance.";
        case ELuaBlueprintNodeType::SetAudioMasterVolume:
            return "Sets the audio master volume.";
        case ELuaBlueprintNodeType::LoadStaticMesh:
            return "Loads a static mesh asset by path.";
        case ELuaBlueprintNodeType::GetStaticMeshComponent:
            return "Returns the first static mesh component owned by the actor.";
        case ELuaBlueprintNodeType::GetStaticMesh:
            return "Returns the static mesh asset assigned to a static mesh component.";
        case ELuaBlueprintNodeType::SetStaticMesh:
            return "Assigns a loaded static mesh asset to a static mesh component.";
        case ELuaBlueprintNodeType::SetStaticMeshByPath:
            return "Loads and assigns a static mesh asset by path on a static mesh component.";
        case ELuaBlueprintNodeType::ClearStaticMesh:
            return "Clears the static mesh assigned to a static mesh component.";
        case ELuaBlueprintNodeType::GetLuaBlueprintComponent:
            return "Returns the first Lua Blueprint component owned by the actor.";
        case ELuaBlueprintNodeType::GetLuaScriptComponent:
            return "Returns the first Lua Script component owned by the actor.";
        case ELuaBlueprintNodeType::CallLuaBlueprintFunction:
            return "Calls a no-argument function on a Lua Blueprint component through its own runtime environment.";
        case ELuaBlueprintNodeType::CallLuaScriptFunction:
            return "Calls a no-argument function on a Lua Script component through its own runtime environment.";
        case ELuaBlueprintNodeType::CallLuaBlueprintFileFunction:
            return "Loads a Lua Blueprint asset from the asset picker and calls a no-argument function in its cached runtime environment.";
        case ELuaBlueprintNodeType::CallLuaScriptFileFunction:
            return "Loads a Lua script file from the script picker and calls a no-argument function in its cached runtime environment.";
        case ELuaBlueprintNodeType::GetSkeletalMeshComponent:
            return "Returns the first skeletal mesh component owned by the actor.";
        case ELuaBlueprintNodeType::SetSkeletalMeshByPath:
            return "Loads and assigns a skeletal mesh asset by path on a skeletal/skinned mesh component.";
        case ELuaBlueprintNodeType::ClearSkeletalMesh:
            return "Clears the skeletal mesh assigned to a skinned mesh component.";
        case ELuaBlueprintNodeType::PlayAnimationByPath:
            return "Loads and plays an animation asset on a skeletal mesh component.";
        case ELuaBlueprintNodeType::StopAnimation:
            return "Stops animation playback on a skeletal mesh component.";
        case ELuaBlueprintNodeType::SetAnimationByPath:
            return "Loads and assigns an animation asset without forcing playback.";
        case ELuaBlueprintNodeType::SetAnimationPlayRate:
            return "Sets skeletal animation playback rate.";
        case ELuaBlueprintNodeType::SetAnimationLooping:
            return "Enables or disables skeletal animation looping.";
        case ELuaBlueprintNodeType::SetAnimationPlaying:
            return "Starts or pauses skeletal animation playback.";
        case ELuaBlueprintNodeType::GetAnimInstance:
            return "Returns the animation instance from a skeletal mesh component.";
        case ELuaBlueprintNodeType::SetAnimGraphVariableFloat:
            return "Writes a float variable on an Anim Graph instance. Use this to drive state machine transition rules from Lua Blueprint.";
        case ELuaBlueprintNodeType::SetAnimGraphVariableBool:
            return "Writes a bool variable on an Anim Graph instance. Use this to drive state machine transition rules from Lua Blueprint.";
        case ELuaBlueprintNodeType::SetAnimGraphVariableInt:
            return "Writes an int variable on an Anim Graph instance. Use this to drive state machine transition rules from Lua Blueprint.";
        case ELuaBlueprintNodeType::GetAnimGraphVariableFloat:
            return "Reads a float variable from an Anim Graph instance.";
        case ELuaBlueprintNodeType::GetAnimGraphVariableBool:
            return "Reads a bool variable from an Anim Graph instance.";
        case ELuaBlueprintNodeType::GetAnimGraphVariableInt:
            return "Reads an int variable from an Anim Graph instance.";
        case ELuaBlueprintNodeType::LoadMaterial:
            return "Loads a material asset by path.";
        case ELuaBlueprintNodeType::GetMaterial:
            return "Reads a material from a primitive component element slot.";
        case ELuaBlueprintNodeType::SetMaterial:
            return "Assigns a material object to a primitive component element slot.";
        case ELuaBlueprintNodeType::SetMaterialByPath:
            return "Loads and assigns a material asset by path to a primitive component element slot.";
        case ELuaBlueprintNodeType::CreateDynamicMaterialInstance:
            return "Creates a per-component dynamic material instance and assigns it back to the slot.";
        case ELuaBlueprintNodeType::SetMaterialScalarParameter:
            return "Sets a scalar parameter on a material or dynamic material instance.";
        case ELuaBlueprintNodeType::SetMaterialVectorParameter:
            return "Sets a Vector3 parameter on a material or dynamic material instance.";
        case ELuaBlueprintNodeType::SetMaterialColorParameter:
            return "Sets a Vector4/Color parameter on a material or dynamic material instance.";
        case ELuaBlueprintNodeType::SetMaterialTextureParameter:
            return "Sets a texture parameter on a material or dynamic material instance.";
        case ELuaBlueprintNodeType::LoadTexture:
            return "Loads a texture asset by path for texture material parameters.";
        case ELuaBlueprintNodeType::GetCameraComponent:
            return "Returns the first camera component owned by the actor.";
        case ELuaBlueprintNodeType::GetActiveCamera:
            return "Returns the active camera component from the camera manager.";
        case ELuaBlueprintNodeType::PossessCamera:
            return "Makes the player camera manager possess and drive the target camera.";
        case ELuaBlueprintNodeType::SetActiveCameraWithBlend:
            return "Blends the active camera manager view to a target camera component.";
        case ELuaBlueprintNodeType::SetViewTargetWithBlend:
            return "Blends the player view target to a target actor.";
        case ELuaBlueprintNodeType::SetCameraFOV:
            return "Sets camera field of view.";
        case ELuaBlueprintNodeType::CameraLookAt:
            return "Rotates a camera to look at a world-space target point.";
        case ELuaBlueprintNodeType::FadeIn:
            return "Runs camera fade-in through the camera manager.";
        case ELuaBlueprintNodeType::FadeOut:
            return "Runs camera fade-out through the camera manager.";
        case ELuaBlueprintNodeType::SetVignette:
            return "Enables and configures camera-manager vignette rendering.";
        case ELuaBlueprintNodeType::ClearVignette:
            return "Disables camera-manager vignette rendering.";
        case ELuaBlueprintNodeType::StartCameraShakeAsset:
            return "Starts a camera shake asset through the camera manager.";
        case ELuaBlueprintNodeType::SetDepthOfField:
            return "Overrides runtime depth of field focus distance, range and blur radius.";
        case ELuaBlueprintNodeType::SetBokeh:
            return "Overrides runtime bokeh radius, luma threshold and intensity.";
        case ELuaBlueprintNodeType::ClearDepthOfField:
            return "Clears runtime depth of field and bokeh overrides.";
        case ELuaBlueprintNodeType::SetLetterbox:
            return "Enables cine-camera letterbox with amount, thickness and color.";
        case ELuaBlueprintNodeType::ClearLetterbox:
            return "Disables cine-camera letterbox.";
        case ELuaBlueprintNodeType::EventInputAction:
            return "Executes when the owning possessed Pawn receives the configured action input.";
        case ELuaBlueprintNodeType::EventInputAxis:
            return "Executes every player-input frame with the configured axis value.";
        }
        return "Lua Blueprint node.";
    }

    bool RenderNodeHelpIcon(ELuaBlueprintNodeType Type, ELuaBlueprintNodeType& OutHoveredType)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (!ImGui::IsItemHovered())
        {
            return false;
        }

        // Do not open an ImGui tooltip while ax::NodeEditor is actively drawing a node.
        // The node editor applies its own canvas transform/clipping while nodes are emitted,
        // which can make regular BeginTooltip/SetTooltip appear at a seemingly unrelated
        // screen position. Capture only the hovered help target here and render one overlay
        // after ed::End(), constrained to the owning Lua Blueprint window/canvas.
        OutHoveredType = Type;
        return true;
    }

    void RenderNodeHelpTooltip(ELuaBlueprintNodeType Type, const ImRect& /*OwnerScreenRect*/, ImGuiID /*OwnerViewportId*/)
    {
        // 표준 툴팁 레이어(foreground)를 사용한다. 이전의 수동 ImGui::Begin 창은
        // NoFocusOnAppearing 으로 한 번 다른 창에 포커스가 넘어가면 z-order 가 뒤로 밀려
        // 블루프린트 패널 뒤에 가려졌다(=호버해도 도움말이 안 보이는 버그). 툴팁 레이어는
        // 포커스/창 순서와 무관하게 항상 최상단에 그려진다. ed::End() 이후 호출되므로
        // 노드 캔버스 변환의 영향도 받지 않아 위치가 정확하다.
        FDeferredTooltip Tooltip;
        SetDeferredTooltip(Tooltip, NodeTypeLabel(Type), NodeTypeHelpText(Type));
        RenderDeferredTooltip(Tooltip);
    }

    const char* PinTypeLabel(ELuaBlueprintPinType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintPinType::Exec:
            return "Exec";
        case ELuaBlueprintPinType::Bool:
            return "Bool";
        case ELuaBlueprintPinType::Int:
            return "Int";
        case ELuaBlueprintPinType::Float:
            return "Float";
        case ELuaBlueprintPinType::String:
            return "String";
        case ELuaBlueprintPinType::Vector:
            return "Vector";
        case ELuaBlueprintPinType::Object:
            return "Object";
        case ELuaBlueprintPinType::Actor:
            return "Actor";
        case ELuaBlueprintPinType::Pawn:
            return "Pawn";
        case ELuaBlueprintPinType::PlayerController:
            return "PlayerController";
        case ELuaBlueprintPinType::ActorComponent:
            return "ActorComponent";
        case ELuaBlueprintPinType::SceneComponent:
            return "SceneComponent";
        case ELuaBlueprintPinType::PrimitiveComponent:
            return "PrimitiveComponent";
        case ELuaBlueprintPinType::Rotator:
            return "Rotator";
        case ELuaBlueprintPinType::LinearColor:
            return "LinearColor";
        case ELuaBlueprintPinType::Vector4:
            return "Vector4";
        case ELuaBlueprintPinType::Matrix:
            return "Matrix";
        case ELuaBlueprintPinType::Class:
            return "Class";
        case ELuaBlueprintPinType::Enum:
            return "Enum";
        case ELuaBlueprintPinType::Name:
            return "Name";
        case ELuaBlueprintPinType::StaticMesh:
            return "StaticMesh";
        case ELuaBlueprintPinType::StaticMeshComponent:
            return "StaticMeshComponent";
        case ELuaBlueprintPinType::SkinnedMeshComponent:
            return "SkinnedMeshComponent";
        case ELuaBlueprintPinType::SkeletalMeshComponent:
            return "SkeletalMeshComponent";
        case ELuaBlueprintPinType::CameraComponent:
            return "CameraComponent";
        case ELuaBlueprintPinType::CineCameraComponent:
            return "CineCameraComponent";
        case ELuaBlueprintPinType::Material:
            return "Material";
        case ELuaBlueprintPinType::Texture:
            return "Texture";
        case ELuaBlueprintPinType::AnimInstance:
            return "AnimInstance";
        case ELuaBlueprintPinType::LuaBlueprintComponent:
            return "LuaBlueprintComponent";
        case ELuaBlueprintPinType::LuaScriptComponent:
            return "LuaScriptComponent";
        case ELuaBlueprintPinType::LuaFunction:
            return "LuaFunction";
        case ELuaBlueprintPinType::Any:
            return "Any";
        case ELuaBlueprintPinType::Array:
            return "Array";
        }
        return "Unknown";
    }

    const char* SeverityLabel(ELuaBlueprintDiagnosticSeverity Severity)
    {
        switch (Severity)
        {
        case ELuaBlueprintDiagnosticSeverity::Info:
            return "Info";
        case ELuaBlueprintDiagnosticSeverity::Warning:
            return "Warning";
        case ELuaBlueprintDiagnosticSeverity::Error:
            return "Error";
        }
        return "Unknown";
    }

    // UE Blueprint 의 카테고리별 헤더 컬러 컨벤션. 노드 분류를 한눈에 구분.
    ImVec4 NodeHeaderColor(ELuaBlueprintNodeType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintNodeType::EventBeginPlay:
        case ELuaBlueprintNodeType::EventTick:
        case ELuaBlueprintNodeType::EventInputAction:
        case ELuaBlueprintNodeType::EventInputAxis:
        case ELuaBlueprintNodeType::EventEndPlay:
        case ELuaBlueprintNodeType::EventOverlap:
        case ELuaBlueprintNodeType::EventEndOverlap:
        case ELuaBlueprintNodeType::EventHit:
        case ELuaBlueprintNodeType::EventEndHit:
        case ELuaBlueprintNodeType::EventPostBeginPlay:
        case ELuaBlueprintNodeType::EventPostStartMatch:
        case ELuaBlueprintNodeType::EventPlayerCameraReady:
            return ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
        case ELuaBlueprintNodeType::Branch:
        case ELuaBlueprintNodeType::Sequence:
        case ELuaBlueprintNodeType::ForLoop:
        case ELuaBlueprintNodeType::WhileLoop:
        case ELuaBlueprintNodeType::ForEachArray:
        case ELuaBlueprintNodeType::Delay:
        case ELuaBlueprintNodeType::SetTimer:
        case ELuaBlueprintNodeType::ClearTimer:
        case ELuaBlueprintNodeType::SetTimerForNextTick:
        case ELuaBlueprintNodeType::IsTimerActive:
            return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
        case ELuaBlueprintNodeType::CallFunction:
        case ELuaBlueprintNodeType::CallFunctionSignature:
        case ELuaBlueprintNodeType::CallCustomEvent:
        case ELuaBlueprintNodeType::CustomEvent:
        case ELuaBlueprintNodeType::CustomLuaFunction:
        case ELuaBlueprintNodeType::CallCustomLuaFunction:
            return ImVec4(0.45f, 0.70f, 1.00f, 1.0f);
        case ELuaBlueprintNodeType::BindEvent:
        case ELuaBlueprintNodeType::UnbindEvent:
        case ELuaBlueprintNodeType::HasEventBinding:
        case ELuaBlueprintNodeType::BindWidgetClick:
            return ImVec4(0.95f, 0.55f, 0.95f, 1.0f);
        case ELuaBlueprintNodeType::GetVariable:
        case ELuaBlueprintNodeType::SetVariable:
        case ELuaBlueprintNodeType::GetProperty:
        case ELuaBlueprintNodeType::SetProperty:
            return ImVec4(0.55f, 0.95f, 0.65f, 1.0f);
        case ELuaBlueprintNodeType::AddFloat:
        case ELuaBlueprintNodeType::SubtractFloat:
        case ELuaBlueprintNodeType::MultiplyFloat:
        case ELuaBlueprintNodeType::DivideFloat:
        case ELuaBlueprintNodeType::AddInt:
        case ELuaBlueprintNodeType::SubtractInt:
        case ELuaBlueprintNodeType::MultiplyInt:
        case ELuaBlueprintNodeType::DivideInt:
        case ELuaBlueprintNodeType::ModInt:
        case ELuaBlueprintNodeType::AddVector:
        case ELuaBlueprintNodeType::SubtractVector:
        case ELuaBlueprintNodeType::ScaleVector:
        case ELuaBlueprintNodeType::DotVector:
        case ELuaBlueprintNodeType::CrossVector:
        case ELuaBlueprintNodeType::VectorLength:
        case ELuaBlueprintNodeType::NormalizeVector:
        case ELuaBlueprintNodeType::MakeVector:
        case ELuaBlueprintNodeType::BreakVector:
        case ELuaBlueprintNodeType::AppendString:
            return ImVec4(0.75f, 0.65f, 1.00f, 1.0f);
        case ELuaBlueprintNodeType::EqualFloat:
        case ELuaBlueprintNodeType::NotEqualFloat:
        case ELuaBlueprintNodeType::LessFloat:
        case ELuaBlueprintNodeType::GreaterFloat:
        case ELuaBlueprintNodeType::LessEqualFloat:
        case ELuaBlueprintNodeType::GreaterEqualFloat:
        case ELuaBlueprintNodeType::EqualInt:
        case ELuaBlueprintNodeType::NotEqualInt:
        case ELuaBlueprintNodeType::LessInt:
        case ELuaBlueprintNodeType::GreaterInt:
        case ELuaBlueprintNodeType::And:
        case ELuaBlueprintNodeType::Or:
        case ELuaBlueprintNodeType::Not:
            return ImVec4(1.00f, 0.55f, 0.85f, 1.0f);
        case ELuaBlueprintNodeType::Self:
            return ImVec4(0.55f, 0.85f, 0.95f, 1.0f);
        case ELuaBlueprintNodeType::SpawnActor:
        case ELuaBlueprintNodeType::SpawnPawn:
        case ELuaBlueprintNodeType::SpawnPawnAndPossess:
        case ELuaBlueprintNodeType::DestroyActor:
        case ELuaBlueprintNodeType::FindActorByName:
        case ELuaBlueprintNodeType::FindActorByClass:
        case ELuaBlueprintNodeType::FindActorByTag:
        case ELuaBlueprintNodeType::FindActorsByTag:
        case ELuaBlueprintNodeType::FindActorsByClass:
        case ELuaBlueprintNodeType::GetActorLocation:
        case ELuaBlueprintNodeType::SetActorLocation:
        case ELuaBlueprintNodeType::GetActorRotation:
        case ELuaBlueprintNodeType::SetActorRotation:
        case ELuaBlueprintNodeType::GetActorScale:
        case ELuaBlueprintNodeType::SetActorScale:
        case ELuaBlueprintNodeType::GetActorForward:
        case ELuaBlueprintNodeType::GetActorRight:
        case ELuaBlueprintNodeType::AddActorWorldOffset:
        case ELuaBlueprintNodeType::ActorHasTag:
        case ELuaBlueprintNodeType::ActorAddTag:
        case ELuaBlueprintNodeType::ActorRemoveTag:
        case ELuaBlueprintNodeType::GetActorName:
        case ELuaBlueprintNodeType::GetOwnerActor:
        case ELuaBlueprintNodeType::LineTrace:
        case ELuaBlueprintNodeType::AddForceToRoot:
        case ELuaBlueprintNodeType::AddTorqueToRoot:
        case ELuaBlueprintNodeType::AddImpulseToRoot:
        case ELuaBlueprintNodeType::GetRootLinearVelocity:
        case ELuaBlueprintNodeType::SetRootLinearVelocity:
        case ELuaBlueprintNodeType::SetRootSimulatePhysics:
            return ImVec4(0.95f, 0.55f, 0.45f, 1.0f);
        case ELuaBlueprintNodeType::IsValid:
        case ELuaBlueprintNodeType::Cast:
            return ImVec4(0.55f, 0.85f, 0.95f, 1.0f);
        case ELuaBlueprintNodeType::GetRootComponent:
        case ELuaBlueprintNodeType::GetRootPrimitiveComponent:
        case ELuaBlueprintNodeType::GetComponentByName:
        case ELuaBlueprintNodeType::GetPrimitiveComponent:
        case ELuaBlueprintNodeType::ActivateComponent:
        case ELuaBlueprintNodeType::DeactivateComponent:
        case ELuaBlueprintNodeType::AddForce:
        case ELuaBlueprintNodeType::AddTorque:
        case ELuaBlueprintNodeType::AddImpulse:
        case ELuaBlueprintNodeType::GetLinearVelocity:
        case ELuaBlueprintNodeType::SetLinearVelocity:
        case ELuaBlueprintNodeType::GetMass:
        case ELuaBlueprintNodeType::SetSimulatePhysics:
            return ImVec4(0.45f, 0.85f, 0.65f, 1.0f);
        case ELuaBlueprintNodeType::LoadStaticMesh:
        case ELuaBlueprintNodeType::GetStaticMeshComponent:
        case ELuaBlueprintNodeType::GetStaticMesh:
        case ELuaBlueprintNodeType::SetStaticMesh:
        case ELuaBlueprintNodeType::SetStaticMeshByPath:
        case ELuaBlueprintNodeType::ClearStaticMesh:
            return ImVec4(0.45f, 0.85f, 0.90f, 1.0f);
        case ELuaBlueprintNodeType::GetLuaBlueprintComponent:
        case ELuaBlueprintNodeType::GetLuaScriptComponent:
        case ELuaBlueprintNodeType::CallLuaBlueprintFunction:
        case ELuaBlueprintNodeType::CallLuaScriptFunction:
        case ELuaBlueprintNodeType::CallLuaBlueprintFileFunction:
        case ELuaBlueprintNodeType::CallLuaScriptFileFunction:
            return ImVec4(0.55f, 0.90f, 0.75f, 1.0f);
        case ELuaBlueprintNodeType::GetSkeletalMeshComponent:
        case ELuaBlueprintNodeType::SetSkeletalMeshByPath:
        case ELuaBlueprintNodeType::ClearSkeletalMesh:
        case ELuaBlueprintNodeType::PlayAnimationByPath:
        case ELuaBlueprintNodeType::StopAnimation:
        case ELuaBlueprintNodeType::SetAnimationByPath:
        case ELuaBlueprintNodeType::SetAnimationPlayRate:
        case ELuaBlueprintNodeType::SetAnimationLooping:
        case ELuaBlueprintNodeType::SetAnimationPlaying:
        case ELuaBlueprintNodeType::GetAnimInstance:
        case ELuaBlueprintNodeType::SetAnimGraphVariableFloat:
        case ELuaBlueprintNodeType::SetAnimGraphVariableBool:
        case ELuaBlueprintNodeType::SetAnimGraphVariableInt:
        case ELuaBlueprintNodeType::GetAnimGraphVariableFloat:
        case ELuaBlueprintNodeType::GetAnimGraphVariableBool:
        case ELuaBlueprintNodeType::GetAnimGraphVariableInt:
            return ImVec4(0.55f, 0.80f, 1.00f, 1.0f);
        case ELuaBlueprintNodeType::LoadMaterial:
        case ELuaBlueprintNodeType::GetMaterial:
        case ELuaBlueprintNodeType::SetMaterial:
        case ELuaBlueprintNodeType::SetMaterialByPath:
        case ELuaBlueprintNodeType::CreateDynamicMaterialInstance:
        case ELuaBlueprintNodeType::SetMaterialScalarParameter:
        case ELuaBlueprintNodeType::SetMaterialVectorParameter:
        case ELuaBlueprintNodeType::SetMaterialColorParameter:
        case ELuaBlueprintNodeType::SetMaterialTextureParameter:
        case ELuaBlueprintNodeType::LoadTexture:
            return ImVec4(0.75f, 0.55f, 1.00f, 1.0f);
        case ELuaBlueprintNodeType::GetCameraComponent:
        case ELuaBlueprintNodeType::GetActiveCamera:
        case ELuaBlueprintNodeType::PossessCamera:
        case ELuaBlueprintNodeType::SetActiveCameraWithBlend:
        case ELuaBlueprintNodeType::SetViewTargetWithBlend:
        case ELuaBlueprintNodeType::SetCameraFOV:
        case ELuaBlueprintNodeType::CameraLookAt:
        case ELuaBlueprintNodeType::FadeIn:
        case ELuaBlueprintNodeType::FadeOut:
        case ELuaBlueprintNodeType::SetVignette:
        case ELuaBlueprintNodeType::ClearVignette:
        case ELuaBlueprintNodeType::StartCameraShakeAsset:
        case ELuaBlueprintNodeType::SetDepthOfField:
        case ELuaBlueprintNodeType::SetBokeh:
        case ELuaBlueprintNodeType::ClearDepthOfField:
        case ELuaBlueprintNodeType::SetLetterbox:
        case ELuaBlueprintNodeType::ClearLetterbox:
            return ImVec4(0.35f, 0.75f, 1.00f, 1.0f);
        case ELuaBlueprintNodeType::Lerp:
        case ELuaBlueprintNodeType::Clamp:
        case ELuaBlueprintNodeType::Min:
        case ELuaBlueprintNodeType::Max:
        case ELuaBlueprintNodeType::RandomFloat:
        case ELuaBlueprintNodeType::RandomInt:
        case ELuaBlueprintNodeType::Sin:
        case ELuaBlueprintNodeType::Cos:
        case ELuaBlueprintNodeType::Sqrt:
        case ELuaBlueprintNodeType::AbsFloat:
        case ELuaBlueprintNodeType::Floor:
        case ELuaBlueprintNodeType::Ceil:
        case ELuaBlueprintNodeType::Distance:
            return ImVec4(0.75f, 0.65f, 1.00f, 1.0f);
        case ELuaBlueprintNodeType::GetGameTime:
            return ImVec4(0.95f, 0.85f, 0.40f, 1.0f);
        case ELuaBlueprintNodeType::VehicleSetThrottle:
        case ELuaBlueprintNodeType::VehicleSetBrake:
        case ELuaBlueprintNodeType::VehicleSetSteering:
        case ELuaBlueprintNodeType::VehicleSetHandbrake:
        case ELuaBlueprintNodeType::VehicleReset:
        case ELuaBlueprintNodeType::VehicleGetForwardSpeed:
            return ImVec4(0.90f, 0.65f, 0.30f, 1.0f);
        case ELuaBlueprintNodeType::ParticleSetTemplateByPath:
        case ELuaBlueprintNodeType::ParticleActivate:
        case ELuaBlueprintNodeType::ParticleDeactivate:
        case ELuaBlueprintNodeType::ParticleReset:
        case ELuaBlueprintNodeType::ParticleRebuild:
        case ELuaBlueprintNodeType::ParticleSetLOD:
        case ELuaBlueprintNodeType::CreateWidget:
        case ELuaBlueprintNodeType::AddWidgetToViewport:
        case ELuaBlueprintNodeType::RemoveWidgetFromParent:
        case ELuaBlueprintNodeType::SetWidgetText:
        case ELuaBlueprintNodeType::LoadAudio:
        case ELuaBlueprintNodeType::AudioPlaySound:
        case ELuaBlueprintNodeType::PlayBGM:
        case ELuaBlueprintNodeType::StopBGM:
        case ELuaBlueprintNodeType::PlayAudioLoop:
        case ELuaBlueprintNodeType::StopAudioLoop:
        case ELuaBlueprintNodeType::SetAudioMasterVolume:
            return ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
        case ELuaBlueprintNodeType::ForEachActorByClass:
        case ELuaBlueprintNodeType::ForEachActorByTag:
            return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
        default:
            return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        }
    }

    // 핀 타입별 색상 — Material editor 패턴 그대로 차용해 통일성 유지.
    ImVec4 PinTypeColor(ELuaBlueprintPinType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintPinType::Exec:
            return ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
        case ELuaBlueprintPinType::Bool:
            return ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
        case ELuaBlueprintPinType::Int:
            return ImVec4(0.45f, 0.95f, 0.85f, 1.0f);
        case ELuaBlueprintPinType::Float:
            return ImVec4(0.55f, 0.95f, 0.45f, 1.0f);
        case ELuaBlueprintPinType::String:
            return ImVec4(0.95f, 0.45f, 0.85f, 1.0f);
        case ELuaBlueprintPinType::Vector:
            return ImVec4(0.95f, 0.85f, 0.30f, 1.0f);
        case ELuaBlueprintPinType::Object:
            return ImVec4(0.40f, 0.75f, 1.00f, 1.0f);
        case ELuaBlueprintPinType::Actor:
        case ELuaBlueprintPinType::Pawn:
        case ELuaBlueprintPinType::PlayerController:
            return ImVec4(0.95f, 0.55f, 0.45f, 1.0f);
        case ELuaBlueprintPinType::ActorComponent:
        case ELuaBlueprintPinType::SceneComponent:
        case ELuaBlueprintPinType::PrimitiveComponent:
        case ELuaBlueprintPinType::LuaBlueprintComponent:
        case ELuaBlueprintPinType::LuaScriptComponent:
            return ImVec4(0.40f, 0.85f, 0.65f, 1.0f);
        case ELuaBlueprintPinType::StaticMesh:
            return ImVec4(0.70f, 0.85f, 0.95f, 1.0f);
        case ELuaBlueprintPinType::StaticMeshComponent:
        case ELuaBlueprintPinType::SkinnedMeshComponent:
        case ELuaBlueprintPinType::SkeletalMeshComponent:
            return ImVec4(0.45f, 0.85f, 0.90f, 1.0f);
        case ELuaBlueprintPinType::CameraComponent:
        case ELuaBlueprintPinType::CineCameraComponent:
            return ImVec4(0.35f, 0.75f, 1.00f, 1.0f);
        case ELuaBlueprintPinType::Material:
        case ELuaBlueprintPinType::Texture:
            return ImVec4(0.75f, 0.55f, 1.00f, 1.0f);
        case ELuaBlueprintPinType::AnimInstance:
            return ImVec4(0.55f, 0.80f, 1.00f, 1.0f);
        case ELuaBlueprintPinType::LuaFunction:
            return ImVec4(0.95f, 0.70f, 0.35f, 1.0f);
        case ELuaBlueprintPinType::Rotator:
            return ImVec4(0.95f, 0.75f, 0.30f, 1.0f);
        case ELuaBlueprintPinType::LinearColor:
        case ELuaBlueprintPinType::Vector4:
            return ImVec4(0.80f, 0.65f, 1.00f, 1.0f);
        case ELuaBlueprintPinType::Matrix:
            return ImVec4(0.65f, 0.65f, 0.95f, 1.0f);
        case ELuaBlueprintPinType::Class:
            return ImVec4(0.35f, 0.65f, 1.00f, 1.0f);
        case ELuaBlueprintPinType::Enum:
        case ELuaBlueprintPinType::Name:
            return ImVec4(0.95f, 0.65f, 0.95f, 1.0f);
        case ELuaBlueprintPinType::Any:
            return ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
        case ELuaBlueprintPinType::Array:
            return ImVec4(0.55f, 0.60f, 1.00f, 1.0f);
        }
        return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    }


    bool LuaBlueprintPinNameMatches(const FString& PinName, const char* A, const char* B = nullptr, const char* C = nullptr)
    {
        return PinName == A || (B && PinName == B) || (C && PinName == C);
    }

    const char* LuaBlueprintAssetTypeForPin(const FLuaBlueprintNode& Node, const FLuaBlueprintPin& Pin)
    {
        if (Pin.Type != ELuaBlueprintPinType::String)
        {
            return nullptr;
        }

        const FString PinName = Pin.DisplayName.ToString();
        switch (Node.Type)
        {
        case ELuaBlueprintNodeType::LoadStaticMesh:
            return LuaBlueprintPinNameMatches(PinName, "Path", "MeshPath") ? "StaticMesh" : nullptr;
        case ELuaBlueprintNodeType::SetStaticMeshByPath:
            return LuaBlueprintPinNameMatches(PinName, "MeshPath", "Path") ? "StaticMesh" : nullptr;
        case ELuaBlueprintNodeType::SetSkeletalMeshByPath:
            return LuaBlueprintPinNameMatches(PinName, "MeshPath", "Path") ? "SkeletalMesh" : nullptr;
        case ELuaBlueprintNodeType::PlayAnimationByPath:
        case ELuaBlueprintNodeType::SetAnimationByPath:
            return LuaBlueprintPinNameMatches(PinName, "AnimationPath", "Path") ? "UAnimSequence" : nullptr;
        case ELuaBlueprintNodeType::LoadMaterial:
            return LuaBlueprintPinNameMatches(PinName, "Path", "MaterialPath") ? "Material" : nullptr;
        case ELuaBlueprintNodeType::SetMaterialByPath:
            return LuaBlueprintPinNameMatches(PinName, "MaterialPath", "Path") ? "Material" : nullptr;
        case ELuaBlueprintNodeType::LoadTexture:
            return LuaBlueprintPinNameMatches(PinName, "Path", "TexturePath") ? "Texture" : nullptr;
        case ELuaBlueprintNodeType::LoadAudio:
            return LuaBlueprintPinNameMatches(PinName, "Path", "AudioPath") ? "Audio" : nullptr;
        case ELuaBlueprintNodeType::ParticleSetTemplateByPath:
            // Existing ParticleSetTemplateByPath nodes create the input as "Path", while an older picker
            // mapping expected "TemplatePath". Accept both so saved graphs and new graphs get the asset picker.
            return LuaBlueprintPinNameMatches(PinName, "Path", "TemplatePath", "ParticlePath") ? "UParticleSystem" : nullptr;
        case ELuaBlueprintNodeType::CreateWidget:
            return LuaBlueprintPinNameMatches(PinName, "DocumentPath", "Path") ? "RmlDocument" : nullptr;
        case ELuaBlueprintNodeType::StartCameraShakeAsset:
            return LuaBlueprintPinNameMatches(PinName, "AssetPath", "Path") ? "CameraShake" : nullptr;
        case ELuaBlueprintNodeType::CallLuaBlueprintFileFunction:
            return LuaBlueprintPinNameMatches(PinName, "BlueprintPath", "Path") ? "ULuaBlueprintAsset" : nullptr;
        case ELuaBlueprintNodeType::CallLuaScriptFileFunction:
            return LuaBlueprintPinNameMatches(PinName, "ScriptFile", "Path") ? "Script" : nullptr;
        default:
            break;
        }
        return nullptr;
    }

    FString LuaBlueprintAssetPreviewText(const FString& CurrentPath)
    {
        FString Preview = "None";
        if (!CurrentPath.empty() && CurrentPath != "None")
        {
            size_t SlashPos = CurrentPath.find_last_of("/\\");
            Preview = (SlashPos == FString::npos) ? CurrentPath : CurrentPath.substr(SlashPos + 1);
            size_t DotPos = Preview.find_last_of('.');
            if (DotPos != FString::npos)
            {
                Preview = Preview.substr(0, DotPos);
            }
        }
        return Preview;
    }

    bool RenderLuaBlueprintAssetPicker(const char* AssetType, const FString& CurrentPath, const char* ComboId, FString& OutSelectedPath)
    {
        // Inspector / non-node-editor fallback. Inline node pins must not use this directly because
        // ImGui combo popups opened inside ax::NodeEditor canvas are transformed/clipped by the canvas.
        if (!AssetType || !AssetType[0])
        {
            return false;
        }

        const FString Preview = LuaBlueprintAssetPreviewText(CurrentPath);
        bool bChanged = false;
        if (ImGui::BeginCombo(ComboId, Preview.c_str()))
        {
            const TArray<FAssetListItem>& Items = FAssetRegistry::ListByTypeName(AssetType);

            const bool bSelectedNone = CurrentPath.empty() || CurrentPath == "None";
            if (ImGui::Selectable("None", bSelectedNone))
            {
                OutSelectedPath = "None";
                bChanged = true;
            }
            if (bSelectedNone)
            {
                ImGui::SetItemDefaultFocus();
            }

            if (Items.empty())
            {
                ImGui::Separator();
                ImGui::TextDisabled("No assets found");
            }
            for (const FAssetListItem& Item : Items)
            {
                const bool bSelected = Item.FullPath == CurrentPath;
                const char* Label = Item.DisplayName.empty() ? Item.FullPath.c_str() : Item.DisplayName.c_str();
                if (ImGui::Selectable(Label, bSelected))
                {
                    OutSelectedPath = Item.FullPath;
                    bChanged = true;
                }
                if (ImGui::IsItemHovered() && !Item.FullPath.empty())
                {
                    ImGui::SetTooltip("%s", Item.FullPath.c_str());
                }
                if (bSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return bChanged;
    }

    bool IsEventNode(ELuaBlueprintNodeType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintNodeType::EventBeginPlay:
        case ELuaBlueprintNodeType::EventTick:
        case ELuaBlueprintNodeType::EventEndPlay:
        case ELuaBlueprintNodeType::EventOverlap:
        case ELuaBlueprintNodeType::EventEndOverlap:
        case ELuaBlueprintNodeType::EventHit:
        case ELuaBlueprintNodeType::EventEndHit:
        case ELuaBlueprintNodeType::EventPostBeginPlay:
        case ELuaBlueprintNodeType::EventPostStartMatch:
        case ELuaBlueprintNodeType::EventPlayerCameraReady:
            return true;
        default:
            return false;
        }
    }

    bool HasNodeOfType(const ULuaBlueprintAsset* Blueprint, ELuaBlueprintNodeType Type)
    {
        if (!Blueprint) return false;
        for (const FLuaBlueprintNode& Node : Blueprint->GetNodes())
        {
            if (Node.Type == Type) return true;
        }
        return false;
    }

    bool IsLuaBlueprintCallableFunction(const FFunction* Function)
    {
        if (!Function)
        {
            return false;
        }

        // LuaBlueprint can execute reflected native functions through the Reflection bridge.
        // Keep events/delegate override hooks out of the normal node palette: they are exposed
        // through the dedicated Bind/Unbind/HasEvent nodes instead of direct Call nodes.
        const uint32 Flags = Function->GetFlags();
        if ((Flags & (FUNC_Callable | FUNC_Pure)) == 0)
        {
            return false;
        }
        if ((Flags & FUNC_Event) != 0)
        {
            return false;
        }
        return Function->Invoker != nullptr;
    }

    FString MakeReflectedFunctionSearchText(const UClass* Class, const FFunction* Function)
    {
        FString Text;
        if (Class)
        {
            Text += Class->GetName();
            Text += " ";
        }
        if (Function)
        {
            Text += Function->GetName();
            Text += " ";
            Text += Function->GetDisplayName();
            Text += " ";
            Text += Function->GetSignature();
            Text += " ";
            Text += Function->GetCategory();
            if (Function->OwnerClassName)
            {
                Text += " ";
                Text += Function->OwnerClassName;
            }
        }
        return Text;
    }

    bool ContainsCaseInsensitive(const char* Haystack, const char* Needle)
    {
        if (!Needle || !*Needle) return true;
        if (!Haystack) return false;
        const size_t HN = std::strlen(Haystack);
        const size_t NN = std::strlen(Needle);
        if (NN > HN) return false;
        for (size_t i = 0; i + NN <= HN; ++i)
        {
            bool bMatch = true;
            for (size_t j = 0; j < NN; ++j)
            {
                if (std::tolower(static_cast<unsigned char>(Haystack[i + j])) != std::tolower(
                    static_cast<unsigned char>(Needle[j])
                ))
                {
                    bMatch = false;
                    break;
                }
            }
            if (bMatch) return true;
        }
        return false;
    }

    ELuaBlueprintPinType NormalizeVariablePinTypeForEditor(ELuaBlueprintPinType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintPinType::Bool:
        case ELuaBlueprintPinType::Int:
        case ELuaBlueprintPinType::Float:
        case ELuaBlueprintPinType::String:
        case ELuaBlueprintPinType::Vector:
        case ELuaBlueprintPinType::Object:
        case ELuaBlueprintPinType::Array:
        case ELuaBlueprintPinType::Actor:
        case ELuaBlueprintPinType::Pawn:
        case ELuaBlueprintPinType::PlayerController:
        case ELuaBlueprintPinType::ActorComponent:
        case ELuaBlueprintPinType::SceneComponent:
        case ELuaBlueprintPinType::PrimitiveComponent:
        case ELuaBlueprintPinType::StaticMesh:
        case ELuaBlueprintPinType::StaticMeshComponent:
        case ELuaBlueprintPinType::SkinnedMeshComponent:
        case ELuaBlueprintPinType::SkeletalMeshComponent:
        case ELuaBlueprintPinType::CameraComponent:
        case ELuaBlueprintPinType::CineCameraComponent:
        case ELuaBlueprintPinType::Material:
        case ELuaBlueprintPinType::Texture:
        case ELuaBlueprintPinType::AnimInstance:
        case ELuaBlueprintPinType::LuaBlueprintComponent:
        case ELuaBlueprintPinType::LuaScriptComponent:
        case ELuaBlueprintPinType::Rotator:
        case ELuaBlueprintPinType::LinearColor:
        case ELuaBlueprintPinType::Vector4:
        case ELuaBlueprintPinType::Class:
        case ELuaBlueprintPinType::Enum:
        case ELuaBlueprintPinType::Name:
            return Type;
        default:
            return ELuaBlueprintPinType::Float;
        }
    }

    int VariablePinTypeToComboIndex(ELuaBlueprintPinType Type)
    {
        switch (NormalizeVariablePinTypeForEditor(Type))
        {
        case ELuaBlueprintPinType::Bool:
            return 0;
        case ELuaBlueprintPinType::Int:
            return 1;
        case ELuaBlueprintPinType::Float:
            return 2;
        case ELuaBlueprintPinType::String:
            return 3;
        case ELuaBlueprintPinType::Vector:
            return 4;
        case ELuaBlueprintPinType::Object:
            return 5;
        case ELuaBlueprintPinType::Array:
            return 6;
        case ELuaBlueprintPinType::Actor:
            return 7;
        case ELuaBlueprintPinType::Pawn:
            return 8;
        case ELuaBlueprintPinType::PlayerController:
            return 9;
        case ELuaBlueprintPinType::ActorComponent:
            return 10;
        case ELuaBlueprintPinType::SceneComponent:
            return 11;
        case ELuaBlueprintPinType::PrimitiveComponent:
            return 12;
        case ELuaBlueprintPinType::Rotator:
            return 13;
        case ELuaBlueprintPinType::LinearColor:
            return 14;
        case ELuaBlueprintPinType::Vector4:
            return 15;
        case ELuaBlueprintPinType::Class:
            return 16;
        case ELuaBlueprintPinType::Enum:
            return 17;
        case ELuaBlueprintPinType::Name:
            return 18;
        case ELuaBlueprintPinType::StaticMesh:
            return 19;
        case ELuaBlueprintPinType::StaticMeshComponent:
            return 20;
        case ELuaBlueprintPinType::SkinnedMeshComponent:
            return 21;
        case ELuaBlueprintPinType::SkeletalMeshComponent:
            return 22;
        case ELuaBlueprintPinType::CameraComponent:
            return 23;
        case ELuaBlueprintPinType::CineCameraComponent:
            return 24;
        case ELuaBlueprintPinType::Material:
            return 25;
        case ELuaBlueprintPinType::Texture:
            return 26;
        case ELuaBlueprintPinType::AnimInstance:
            return 27;
        case ELuaBlueprintPinType::LuaBlueprintComponent:
            return 28;
        case ELuaBlueprintPinType::LuaScriptComponent:
            return 29;
        default:
            return 2;
        }
    }

    ELuaBlueprintPinType ComboIndexToVariablePinType(int Index)
    {
        switch (Index)
        {
        case 0:
            return ELuaBlueprintPinType::Bool;
        case 1:
            return ELuaBlueprintPinType::Int;
        case 2:
            return ELuaBlueprintPinType::Float;
        case 3:
            return ELuaBlueprintPinType::String;
        case 4:
            return ELuaBlueprintPinType::Vector;
        case 5:
            return ELuaBlueprintPinType::Object;
        case 6:
            return ELuaBlueprintPinType::Array;
        case 7:
            return ELuaBlueprintPinType::Actor;
        case 8:
            return ELuaBlueprintPinType::Pawn;
        case 9:
            return ELuaBlueprintPinType::PlayerController;
        case 10:
            return ELuaBlueprintPinType::ActorComponent;
        case 11:
            return ELuaBlueprintPinType::SceneComponent;
        case 12:
            return ELuaBlueprintPinType::PrimitiveComponent;
        case 13:
            return ELuaBlueprintPinType::Rotator;
        case 14:
            return ELuaBlueprintPinType::LinearColor;
        case 15:
            return ELuaBlueprintPinType::Vector4;
        case 16:
            return ELuaBlueprintPinType::Class;
        case 17:
            return ELuaBlueprintPinType::Enum;
        case 18:
            return ELuaBlueprintPinType::Name;
        case 19:
            return ELuaBlueprintPinType::StaticMesh;
        case 20:
            return ELuaBlueprintPinType::StaticMeshComponent;
        case 21:
            return ELuaBlueprintPinType::SkinnedMeshComponent;
        case 22:
            return ELuaBlueprintPinType::SkeletalMeshComponent;
        case 23:
            return ELuaBlueprintPinType::CameraComponent;
        case 24:
            return ELuaBlueprintPinType::CineCameraComponent;
        case 25:
            return ELuaBlueprintPinType::Material;
        case 26:
            return ELuaBlueprintPinType::Texture;
        case 27:
            return ELuaBlueprintPinType::AnimInstance;
        case 28:
            return ELuaBlueprintPinType::LuaBlueprintComponent;
        case 29:
            return ELuaBlueprintPinType::LuaScriptComponent;
        default:
            return ELuaBlueprintPinType::Float;
        }
    }

    FName MakeUniqueVariableNameForEditor(const ULuaBlueprintAsset* Blueprint, const FName& DesiredName, int32 CurrentIndex)
    {
        if (!Blueprint) return DesiredName == FName::None ? FName("Variable") : DesiredName;

        FString BaseName = DesiredName == FName::None ? FString("Variable") : DesiredName.ToString();
        if (BaseName.empty()) BaseName = "Variable";

        FString Candidate = BaseName;
        int32   Suffix    = 1;
        bool    bUnique   = false;
        while (!bUnique)
        {
            bUnique                                        = true;
            const TArray<FLuaBlueprintVariable>& Variables = Blueprint->GetVariables();
            for (int32 i = 0; i < static_cast<int32>(Variables.size()); ++i)
            {
                if (i == CurrentIndex) continue;
                if (Variables[i].Name.ToString() == Candidate)
                {
                    Candidate = BaseName + std::to_string(Suffix++);
                    bUnique   = false;
                    break;
                }
            }
        }
        return FName(Candidate);
    }

    struct FLuaBlueprintNodeFragmentBounds
    {
        ImVec2 Min    = ImVec2(0.0f, 0.0f);
        ImVec2 Max    = ImVec2(0.0f, 0.0f);
        bool   bValid = false;
    };

    ImVec2 EstimateLuaBlueprintNodeSize(const FLuaBlueprintNode& Node)
    {
        if (Node.Type == ELuaBlueprintNodeType::Comment)
        {
            return ImVec2((std::max)(80.0f, Node.VectorValue.X), (std::max)(40.0f, Node.VectorValue.Y));
        }

        return ImVec2(180.0f, 90.0f);
    }

    FLuaBlueprintNodeFragmentBounds ComputeNodeFragmentBounds(
        const TArray<FLuaBlueprintNode>& Nodes,
        bool                             bUseEditorNodeSizes,
        bool                             bPreferNonCommentNodes = false
    )
    {
        FLuaBlueprintNodeFragmentBounds Bounds;
        if (Nodes.empty()) return Bounds;

        bool bHasNonComment = false;
        if (bPreferNonCommentNodes)
        {
            for (const FLuaBlueprintNode& Node : Nodes)
            {
                if (Node.Type != ELuaBlueprintNodeType::Comment)
                {
                    bHasNonComment = true;
                    break;
                }
            }
        }

        float MinX = FLT_MAX;
        float MinY = FLT_MAX;
        float MaxX = -FLT_MAX;
        float MaxY = -FLT_MAX;

        for (const FLuaBlueprintNode& Node : Nodes)
        {
            if (bHasNonComment && Node.Type == ELuaBlueprintNodeType::Comment)
            {
                continue;
            }

            ImVec2 Pos(Node.PosX, Node.PosY);
            ImVec2 Size = EstimateLuaBlueprintNodeSize(Node);
            if (bUseEditorNodeSizes)
            {
                const ImVec2 EditorSize = ed::GetNodeSize(ToNodeId(Node.NodeId));
                if (EditorSize.x > 1.0f && EditorSize.y > 1.0f)
                {
                    Size = EditorSize;
                }
            }

            MinX          = (std::min)(MinX, Pos.x);
            MinY          = (std::min)(MinY, Pos.y);
            MaxX          = (std::max)(MaxX, Pos.x + Size.x);
            MaxY          = (std::max)(MaxY, Pos.y + Size.y);
            Bounds.bValid = true;
        }

        if (!Bounds.bValid) return Bounds;
        Bounds.Min = ImVec2(MinX, MinY);
        Bounds.Max = ImVec2(MaxX, MaxY);
        return Bounds;
    }

    FLuaBlueprintNodeFragmentBounds ComputeLiveNodeBounds(const FLuaBlueprintNode& Node, bool bUseEditorNode)
    {
        FLuaBlueprintNodeFragmentBounds Bounds;
        ImVec2                          Pos(Node.PosX, Node.PosY);
        ImVec2                          Size = EstimateLuaBlueprintNodeSize(Node);
        if (bUseEditorNode)
        {
            Pos                     = ed::GetNodePosition(ToNodeId(Node.NodeId));
            const ImVec2 EditorSize = ed::GetNodeSize(ToNodeId(Node.NodeId));
            if (EditorSize.x > 1.0f && EditorSize.y > 1.0f)
            {
                Size = EditorSize;
            }
        }
        Bounds.Min    = Pos;
        Bounds.Max    = ImVec2(Pos.x + Size.x, Pos.y + Size.y);
        Bounds.bValid = true;
        return Bounds;
    }

    bool IsBoundsFullyInside(const FLuaBlueprintNodeFragmentBounds& Inner, const FLuaBlueprintNodeFragmentBounds& Outer)
    {
        if (!Inner.bValid || !Outer.bValid) return false;
        constexpr float Tolerance = 1.0f;
        return Inner.Min.x >= Outer.Min.x - Tolerance
                && Inner.Min.y >= Outer.Min.y - Tolerance
                && Inner.Max.x <= Outer.Max.x + Tolerance
                && Inner.Max.y <= Outer.Max.y + Tolerance;
    }

    ImVec2 ComputeNodeFragmentMin(const TArray<FLuaBlueprintNode>& Nodes)
    {
        const FLuaBlueprintNodeFragmentBounds Bounds = ComputeNodeFragmentBounds(Nodes, false);
        return Bounds.bValid ? Bounds.Min : ImVec2(0.0f, 0.0f);
    }

    FLuaBlueprintNodeFragmentBounds ComputeNodeFragmentPasteReferenceBounds(
        const TArray<FLuaBlueprintNode>& Nodes,
        bool                             bUseEditorNodeSizes
    )
    {
        // Duplicate/paste placement should be based on actual source nodes, not on an oversized
        // selected comment/group rectangle. If only comments are selected, comments become the basis.
        return ComputeNodeFragmentBounds(Nodes, bUseEditorNodeSizes, true);
    }

    bool IsValueProducingNode(ELuaBlueprintNodeType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintNodeType::LiteralBool:
        case ELuaBlueprintNodeType::LiteralInt:
        case ELuaBlueprintNodeType::LiteralFloat:
        case ELuaBlueprintNodeType::LiteralString:
        case ELuaBlueprintNodeType::LiteralVector:
        case ELuaBlueprintNodeType::GetVariable:
        case ELuaBlueprintNodeType::GetProperty:
        case ELuaBlueprintNodeType::CallFunction:
        case ELuaBlueprintNodeType::CallFunctionSignature:
        case ELuaBlueprintNodeType::Self:
        case ELuaBlueprintNodeType::FindActorByName:
        case ELuaBlueprintNodeType::FindActorByClass:
        case ELuaBlueprintNodeType::FindActorByTag:
        case ELuaBlueprintNodeType::FindActorsByTag:
        case ELuaBlueprintNodeType::FindActorsByClass:
        case ELuaBlueprintNodeType::GetActorLocation:
        case ELuaBlueprintNodeType::GetActorRotation:
        case ELuaBlueprintNodeType::GetActorScale:
        case ELuaBlueprintNodeType::GetActorForward:
        case ELuaBlueprintNodeType::GetActorRight:
        case ELuaBlueprintNodeType::ActorHasTag:
        case ELuaBlueprintNodeType::GetActorName:
        case ELuaBlueprintNodeType::GetOwnerActor:
        case ELuaBlueprintNodeType::IsValid:
        case ELuaBlueprintNodeType::GetRootComponent:
        case ELuaBlueprintNodeType::GetRootPrimitiveComponent:
        case ELuaBlueprintNodeType::GetComponentByName:
        case ELuaBlueprintNodeType::GetPrimitiveComponent:
        case ELuaBlueprintNodeType::GetLinearVelocity:
        case ELuaBlueprintNodeType::GetMass:
        case ELuaBlueprintNodeType::Lerp:
        case ELuaBlueprintNodeType::Clamp:
        case ELuaBlueprintNodeType::Min:
        case ELuaBlueprintNodeType::Max:
        case ELuaBlueprintNodeType::RandomFloat:
        case ELuaBlueprintNodeType::RandomInt:
        case ELuaBlueprintNodeType::Sin:
        case ELuaBlueprintNodeType::Cos:
        case ELuaBlueprintNodeType::Sqrt:
        case ELuaBlueprintNodeType::AbsFloat:
        case ELuaBlueprintNodeType::Floor:
        case ELuaBlueprintNodeType::Ceil:
        case ELuaBlueprintNodeType::Distance:
        case ELuaBlueprintNodeType::GetGameTime:
        case ELuaBlueprintNodeType::EventInputAxis:
        case ELuaBlueprintNodeType::Reroute:
        case ELuaBlueprintNodeType::ToBool:
        case ELuaBlueprintNodeType::ToInt:
        case ELuaBlueprintNodeType::ToFloat:
        case ELuaBlueprintNodeType::ToString:
        case ELuaBlueprintNodeType::ToVector:
            return true;
        default:
            return false;
        }
    }

    const FLuaBlueprintNode* FindCustomLuaFunctionNodeById(const ULuaBlueprintAsset* Blueprint, uint32 NodeId)
    {
        if (!Blueprint || NodeId == 0) return nullptr;
        const FLuaBlueprintNode* Node = Blueprint->FindNode(NodeId);
        return Node && Node->Type == ELuaBlueprintNodeType::CustomLuaFunction ? Node : nullptr;
    }

    FLuaBlueprintNode* FindCustomLuaFunctionNodeById(ULuaBlueprintAsset* Blueprint, uint32 NodeId)
    {
        if (!Blueprint || NodeId == 0) return nullptr;
        FLuaBlueprintNode* Node = Blueprint->FindNode(NodeId);
        return Node && Node->Type == ELuaBlueprintNodeType::CustomLuaFunction ? Node : nullptr;
    }

    const FLuaBlueprintNode* FindCustomLuaFunctionNodeByName(const ULuaBlueprintAsset* Blueprint, const FName& FunctionName)
    {
        if (!Blueprint || FunctionName == FName::None) return nullptr;
        for (const FLuaBlueprintNode& Node : Blueprint->GetNodes())
        {
            if (Node.Type == ELuaBlueprintNodeType::CustomLuaFunction && Node.NameValue == FunctionName)
            {
                return &Node;
            }
        }
        return nullptr;
    }

    FLuaBlueprintNode* FindCustomLuaFunctionNodeByName(ULuaBlueprintAsset* Blueprint, const FName& FunctionName)
    {
        if (!Blueprint || FunctionName == FName::None) return nullptr;
        for (FLuaBlueprintNode& Node : Blueprint->GetMutableNodes())
        {
            if (Node.Type == ELuaBlueprintNodeType::CustomLuaFunction && Node.NameValue == FunctionName)
            {
                return &Node;
            }
        }
        return nullptr;
    }

    const FLuaBlueprintNode* ResolveCustomLuaFunctionCallTarget(const ULuaBlueprintAsset* Blueprint, const FLuaBlueprintNode& CallNode)
    {
        if (!Blueprint || CallNode.Type != ELuaBlueprintNodeType::CallCustomLuaFunction) return nullptr;

        for (const FLuaBlueprintPin& Pin : CallNode.Pins)
        {
            if (Pin.Kind != ELuaBlueprintPinKind::Input || Pin.DisplayName.ToString() != "Function") continue;
            if (const FLuaBlueprintLink* Link = Blueprint->FindLinkToInput(Pin.PinId))
            {
                if (const FLuaBlueprintPin* SourcePin = Blueprint->FindPin(Link->FromPinId))
                {
                    if (const FLuaBlueprintNode* SourceNode = FindCustomLuaFunctionNodeById(Blueprint, SourcePin->OwningNodeId))
                    {
                        return SourceNode;
                    }
                }
            }
            break;
        }

        if (const FLuaBlueprintNode* ById = FindCustomLuaFunctionNodeById(Blueprint, static_cast<uint32>(CallNode.IntValue)))
        {
            return ById;
        }
        return FindCustomLuaFunctionNodeByName(Blueprint, CallNode.NameValue);
    }

    void SyncCallCustomLuaFunctionNamePin(FLuaBlueprintNode& CallNode)
    {
        if (CallNode.Type != ELuaBlueprintNodeType::CallCustomLuaFunction) return;
        for (FLuaBlueprintPin& Pin : CallNode.Pins)
        {
            if (Pin.Kind == ELuaBlueprintPinKind::Input && Pin.DisplayName.ToString() == "FunctionName")
            {
                Pin.DefaultString = CallNode.NameValue.ToString();
                break;
            }
        }
    }

    void SetCallCustomLuaFunctionTarget(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& CallNode, const FLuaBlueprintNode* FunctionNode)
    {
        if (!Blueprint || CallNode.Type != ELuaBlueprintNodeType::CallCustomLuaFunction || !FunctionNode || FunctionNode->Type != ELuaBlueprintNodeType::CustomLuaFunction) return;
        CallNode.IntValue = static_cast<int32>(FunctionNode->NodeId);
        CallNode.NameValue = FunctionNode->NameValue;
        SyncCallCustomLuaFunctionNamePin(CallNode);
    }
}

FLuaBlueprintEditorWidget::~FLuaBlueprintEditorWidget()
{
    DestroyContext();
}

bool FLuaBlueprintEditorWidget::CanEdit(UObject* Object) const
{
    return Cast<ULuaBlueprintAsset>(Object) != nullptr;
}

void FLuaBlueprintEditorWidget::Open(UObject* Object)
{
    if (!CanEdit(Object)) return;

    FAssetEditorWidget::Open(Object);
    EnsureContext();
    bPositionsPushed              = false;
    bPendingInitialContentFit     = true;
    bPendingNodeGeometryEdit      = false;
    bPendingNavigateToNode        = false;
    PendingNavigateToNodeId       = 0;
    bSuppressInitialGeometryDirty = true;
    bDebugBreakpointsSynced       = false;

    if (ULuaBlueprintAsset* Blueprint = GetBlueprint())
    {
        if (Blueprint->GetNodes().empty())
        {
            Blueprint->InitializeDefault();
        }
        else if (Blueprint->IsCompileDirty())
        {
            Blueprint->Compile();
        }
        CaptureInitialUndoSnapshot(Blueprint);
    }
}

void FLuaBlueprintEditorWidget::Close()
{
    if (bPendingNodeGeometryEdit)
    {
        if (ULuaBlueprintAsset* Blueprint = GetBlueprint())
        {
            CommitBlueprintEdit(Blueprint);
        }
        bPendingNodeGeometryEdit = false;
    }

    DestroyContext();
    bPositionsPushed = false;
    UndoStack.clear();
    RedoStack.clear();
    ClipboardNodes.clear();
    ClipboardLinks.clear();
    bPendingInitialContentFit     = false;
    bPendingNodeGeometryEdit      = false;
    bPendingNavigateToNode        = false;
    PendingNavigateToNodeId       = 0;
    bSuppressInitialGeometryDirty = false;
    bDebugBreakpointsSynced       = false;
    FAssetEditorWidget::Close();
}

void FLuaBlueprintEditorWidget::Render(float /*DeltaTime*/)
{
    ULuaBlueprintAsset* Blueprint = GetBlueprint();
    if (!Blueprint)
    {
        Close();
        return;
    }

    EnsureContext();

    // 표시 라벨은 dirty mark(*) 때문에 바뀔 수 있지만, ### 뒤 ID 는 자산 인스턴스로 고정한다.
    // ## 는 표시 suffix 만 숨길 뿐 앞 라벨까지 ID 에 포함되므로 dirty toggle 시 다른 창으로 취급될 수 있다.
    const FString DisplayLabel    = (Blueprint->GetSourcePath().empty() ? Blueprint->GetName() : Blueprint->GetSourcePath());
    const bool    bShowDirtyState = !bSuppressInitialGeometryDirty && (IsDirty() || Blueprint->IsCompileDirty());
    const FString DirtyMark       = bShowDirtyState ? FString("*") : FString();
    char          WindowTitleBuf[512];
    std::snprintf(
        WindowTitleBuf,
        sizeof(WindowTitleBuf),
        "Lua Blueprint - %s%s###LuaBP_%p",
        DisplayLabel.c_str(),
        DirtyMark.c_str(),
        static_cast<const void*>(Blueprint)
    );

    bool bWindowOpen = IsOpen();
    if (!bRenderingDocument)
    {
        ImGui::SetNextWindowSize(ImVec2(1440.0f, 900.0f), ImGuiCond_Once);
    }
    if (!bRenderingDocument && !ImGui::Begin(WindowTitleBuf, &bWindowOpen))
    {
        ImGui::End();
        if (!bWindowOpen) Close();
        return;
    }

    if (ConsumeFocusRequest())
    {
        ImGui::SetWindowFocus();
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        ImGuiIO&   IO    = ImGui::GetIO();
        const bool bCtrl = IO.KeyCtrl || IO.KeySuper;
        if (bCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
        {
            UndoBlueprintEdit(Blueprint);
        }
        else if (bCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
        {
            RedoBlueprintEdit(Blueprint);
        }
        else if (!IO.WantTextInput && bCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            bQueuedCopySelected = true;
        }
        else if (!IO.WantTextInput && bCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
        {
            bQueuedPasteNodes = true;
        }
        else if (!IO.WantTextInput && bCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
        {
            bQueuedDuplicateSelected = true;
        }
        else if (!IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            bQueuedDeleteSelected = true;
        }
    }

    SyncBreakpointsToDebugManager(Blueprint);
    ProcessLuaDebugAutoFocus(Blueprint);
    RenderToolbar(Blueprint);
    RenderCompileErrorPanel(Blueprint);

    const float BottomHeight = 180.0f;
    ImGui::BeginChild("##LuaBlueprintMainArea", ImVec2(0, -BottomHeight), ImGuiChildFlags_None);
    RenderLeftPanel(Blueprint);
    ImGui::SameLine();
    RenderGraph(Blueprint);
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::BeginTabBar("##LuaBlueprintBottomTabs"))
    {
        if (ImGui::BeginTabItem("Diagnostics"))
        {
            RenderDiagnostics(Blueprint);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Generated Lua"))
        {
            RenderGeneratedLua(Blueprint);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    RenderCustomLuaFunctionEditor(Blueprint);

    if (!bRenderingDocument)
    {
        ImGui::End();
        if (!bWindowOpen) Close();
    }
}

void FLuaBlueprintEditorWidget::RenderDocument(float DeltaTime)
{
    if (ImGui::BeginChild("##LuaBlueprintDocument", ImVec2(0, 0), ImGuiChildFlags_None))
    {
        bRenderingDocument = true;
        Render(DeltaTime);
        bRenderingDocument = false;
    }
    ImGui::EndChild();
}

bool FLuaBlueprintEditorWidget::IsEditingObject(UObject* Object) const
{
    if (FAssetEditorWidget::IsEditingObject(Object))
    {
        return true;
    }

    const ULuaBlueprintAsset* CurrentBlueprint   = Cast<ULuaBlueprintAsset>(EditedObject);
    const ULuaBlueprintAsset* RequestedBlueprint = Cast<ULuaBlueprintAsset>(Object);
    if (!IsOpen() || !CurrentBlueprint || !RequestedBlueprint)
    {
        return false;
    }

    const FString& CurrentPath = CurrentBlueprint->GetSourcePath();
    return !CurrentPath.empty() && CurrentPath == RequestedBlueprint->GetSourcePath();
}

FString FLuaBlueprintEditorWidget::GetDocumentTitle() const
{
    const ULuaBlueprintAsset* Blueprint = Cast<ULuaBlueprintAsset>(EditedObject);
    if (!Blueprint)
    {
        return FString("Lua Blueprint");
    }

    const FString& SourcePath = Blueprint->GetSourcePath();
    return SourcePath.empty() ? FString("Lua Blueprint - " + Blueprint->GetName()) : FString("Lua Blueprint - " + SourcePath);
}

FString FLuaBlueprintEditorWidget::GetDocumentPayloadId() const
{
    const ULuaBlueprintAsset* Blueprint  = Cast<ULuaBlueprintAsset>(EditedObject);
    const FString             SourcePath = Blueprint ? Blueprint->GetSourcePath() : FString();
    return SourcePath.empty() ? FAssetEditorWidget::GetDocumentPayloadId() : SourcePath;
}

void FLuaBlueprintEditorWidget::EnsureContext()
{
    if (NodeEditorContext) return;
    ed::Config Cfg;
    Cfg.SettingsFile  = nullptr;
    NodeEditorContext = ed::CreateEditor(&Cfg);
}

void FLuaBlueprintEditorWidget::DestroyContext()
{
    if (NodeEditorContext)
    {
        ed::DestroyEditor(NodeEditorContext);
        NodeEditorContext = nullptr;
    }
}

ULuaBlueprintAsset* FLuaBlueprintEditorWidget::GetBlueprint() const
{
    return Cast<ULuaBlueprintAsset>(EditedObject);
}

namespace
{
    TArray<uint8> CaptureLuaBlueprintSnapshot(ULuaBlueprintAsset* Blueprint)
    {
        TArray<uint8> Buffer;
        if (!Blueprint) return Buffer;
        FMemoryArchive Saver(true);
        Blueprint->Serialize(Saver);
        Buffer = Saver.GetBuffer();
        return Buffer;
    }
}

void FLuaBlueprintEditorWidget::RenderToolbar(ULuaBlueprintAsset* Blueprint)
{
    // Material editor 와 동일한 색상 버튼 툴바. 메뉴바 대신 본문 상단에 직접 그린다.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));

    const bool bDirtyNow = !bSuppressInitialGeometryDirty && (IsDirty() || Blueprint->IsCompileDirty());

    // Compile: 그래프를 Lua 로 컴파일(저장 X). Save: 컴파일 후 .uasset 영구 저장.
    if (ToolbarAccentButton("Compile", ImVec4(0.46f, 0.36f, 0.16f, 1.0f), ImVec4(0.62f, 0.48f, 0.20f, 1.0f)))
    {
        Blueprint->Compile();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compile the graph to Lua. Does not save the asset.");

    ImGui::SameLine();
    if (ToolbarAccentButton(bDirtyNow ? "Save *" : "Save", ImVec4(0.22f, 0.50f, 0.28f, 1.0f), ImVec4(0.28f, 0.64f, 0.34f, 1.0f)))
    {
        // Compile errors must not block graph persistence; runtime will keep using last-good Lua.
        Blueprint->Compile();
        if (FLuaBlueprintManager::Get().Save(Blueprint))
        {
            ClearDirty();
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compile and save the blueprint graph to its .uasset.");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (ToolbarAccentButton("Undo", ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), UndoStack.size() > 1))
    {
        UndoBlueprintEdit(Blueprint);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo (Ctrl+Z)");

    ImGui::SameLine();
    if (ToolbarAccentButton("Redo", ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), !RedoStack.empty()))
    {
        RedoBlueprintEdit(Blueprint);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo (Ctrl+Y)");

    ImGui::SameLine();
    if (ToolbarButton("Add Node"))
    {
        // ed 컨텍스트 밖에서 호출되므로 graph 좌표로 직접 stagger (Palette 와 동일 규칙).
        PendingNewNodePosition  = ImVec2(PendingNewNodePosition.x + 30.0f, PendingNewNodePosition.y + 30.0f);
        AddNodeSearchBuf[0]     = 0;
        bOpenToolbarAddNodeMenu = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse and add a node. You can also right-click the canvas.");

    ImGui::SameLine();
    if (ToolbarButton("Reset"))
    {
        Blueprint->InitializeDefault();
        bPositionsPushed = false;
        CommitBlueprintEdit(Blueprint);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset the graph to its default event nodes.");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const bool bLuaDebugPaused = FLuaDebugManager::IsPaused();
    if (ToolbarAccentButton("Continue", ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), bLuaDebugPaused))
    {
        FLuaDebugManager::Continue();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resume the paused LuaBlueprint coroutine.");
    ImGui::SameLine();
    if (ToolbarAccentButton("Step In", ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), bLuaDebugPaused))
    {
        FLuaDebugManager::StepInto();
    }
    ImGui::SameLine();
    if (ToolbarAccentButton("Step Over", ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), bLuaDebugPaused))
    {
        FLuaDebugManager::StepOver();
    }
    ImGui::SameLine();
    if (ToolbarAccentButton("Step Out", ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), bLuaDebugPaused))
    {
        FLuaDebugManager::StepOut();
    }
    ImGui::SameLine();
    if (ToolbarButton("Pause Next Node"))
    {
        FLuaDebugManager::PauseNextLine();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause when the next LuaBlueprint node executes.");

    // ── 우측 정렬 상태 배지: 컴파일 에러/경고 요약 ──
    {
        int NumErrors   = 0;
        int NumWarnings = 0;
        for (const FLuaBlueprintDiagnostic& D : Blueprint->GetDiagnostics())
        {
            if (D.Severity == ELuaBlueprintDiagnosticSeverity::Error) ++NumErrors;
            if (D.Severity == ELuaBlueprintDiagnosticSeverity::Warning) ++NumWarnings;
        }

        char   StatusBuf[64];
        ImVec4 StatusCol;
        if (NumErrors > 0)
        {
            std::snprintf(StatusBuf, sizeof(StatusBuf), "%d error%s", NumErrors, NumErrors == 1 ? "" : "s");
            StatusCol = ImVec4(0.95f, 0.40f, 0.32f, 1.0f);
        }
        else if (NumWarnings > 0)
        {
            std::snprintf(StatusBuf, sizeof(StatusBuf), "%d warning%s", NumWarnings, NumWarnings == 1 ? "" : "s");
            StatusCol = ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
        }
        else if (bDirtyNow)
        {
            std::snprintf(StatusBuf, sizeof(StatusBuf), "Unsaved changes");
            StatusCol = ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
        }
        else
        {
            std::snprintf(StatusBuf, sizeof(StatusBuf), "Compiled");
            StatusCol = ImVec4(0.45f, 0.90f, 0.55f, 1.0f);
        }

        const float TextW = ImGui::CalcTextSize(StatusBuf).x;
        ImGui::SameLine((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - TextW - 24.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(StatusCol, "%s", StatusBuf);
    }

    ImGui::PopStyleVar();
    ImGui::Separator();

    // 툴바 Add Node 버튼 → 캔버스 우클릭과 동일한 검색형 노드 메뉴를 팝업으로 노출.
    if (bOpenToolbarAddNodeMenu)
    {
        ImGui::OpenPopup("LuaBlueprintToolbarAddNode");
        bOpenToolbarAddNodeMenu = false;
    }
    if (ImGui::BeginPopup("LuaBlueprintToolbarAddNode"))
    {
        RenderAddNodeMenu(Blueprint);
        ImGui::EndPopup();
    }
}

void FLuaBlueprintEditorWidget::RenderCompileErrorPanel(ULuaBlueprintAsset* Blueprint)
{
    // Material 패턴: 상단에 빨간 에러 패널을 명시적으로 노출. Diagnostics 탭은 보조용.
    bool  bHasError   = false;
    int   NumWarnings = 0;
    int   NumErrors   = 0;
    int   VisibleRows = 0;
    for (const FLuaBlueprintDiagnostic& D : Blueprint->GetDiagnostics())
    {
        if (D.Severity == ELuaBlueprintDiagnosticSeverity::Error)
        {
            bHasError = true;
            ++NumErrors;
            ++VisibleRows;
        }
        if (D.Severity == ELuaBlueprintDiagnosticSeverity::Warning)
        {
            ++NumWarnings;
            if (!bHasError) ++VisibleRows;
        }
    }

    if (!bHasError && NumWarnings == 0) return;

    const ImVec4 Bg = bHasError ? ImVec4(0.25f, 0.10f, 0.10f, 0.6f) : ImVec4(0.25f, 0.20f, 0.10f, 0.6f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Bg);
    const float RowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float Height = (std::min)(180.0f, (std::max)(64.0f, RowHeight * static_cast<float>((std::min)(VisibleRows, 6) + 2)));
    ImGui::BeginChild("##LuaBlueprintCompileBanner", ImVec2(0, Height), ImGuiChildFlags_Borders);

    if (bHasError)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Compile errors: %d", NumErrors);
        for (const FLuaBlueprintDiagnostic& D : Blueprint->GetDiagnostics())
        {
            if (D.Severity != ELuaBlueprintDiagnosticSeverity::Error) continue;
            ImGui::PushID(static_cast<int>(D.NodeId));
            const FString Line = FString("Node ") + std::to_string(D.NodeId) + ": " + D.Message;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", Line.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemClicked())
            {
                QueueNavigateToNode(D.NodeId);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Click to focus the node.");
            }
            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f), "Compile warnings: %d", NumWarnings);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("Open Diagnostics below for the full list. Click a diagnostic row to focus its node.");
        ImGui::PopTextWrapPos();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void FLuaBlueprintEditorWidget::RenderLeftPanel(ULuaBlueprintAsset* Blueprint)
{
    // Material editor 의 좌측 패널과 동일한 구성: 하나의 bordered 컬럼 안에 색상 헤더로 구분된 섹션.
    const float Width = 280.0f;
    ImGui::BeginChild("##LuaBlueprintLeftPanel", ImVec2(Width, 0), ImGuiChildFlags_Borders);

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.24f, 0.26f, 0.32f, 1.0f));
    if (ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderVariables(Blueprint);
    }
    if (ImGui::CollapsingHeader("Palette", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderPalettePanel(Blueprint);
    }
    ImGui::PopStyleColor();

    ImGui::EndChild();
}

void FLuaBlueprintEditorWidget::RenderVariables(ULuaBlueprintAsset* Blueprint)
{
    if (ImGui::SmallButton("+ New Variable"))
    {
        ImGui::OpenPopup("LuaBlueprintAddVariableMenu");
    }

    if (ImGui::BeginPopup("LuaBlueprintAddVariableMenu"))
    {
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Bool, "Bool");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Int, "Int");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Float, "Float");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::String, "String");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Vector, "Vector");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Object, "Object");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Array, "Array");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Actor, "Actor");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Pawn, "Pawn");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::PlayerController, "PlayerController");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::ActorComponent, "ActorComponent");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::SceneComponent, "SceneComponent");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::PrimitiveComponent, "PrimitiveComponent");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::StaticMesh, "StaticMesh");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::StaticMeshComponent, "StaticMeshComponent");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::SkinnedMeshComponent, "SkinnedMeshComponent");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::SkeletalMeshComponent, "SkeletalMeshComponent");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::CameraComponent, "CameraComponent");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::CineCameraComponent, "CineCameraComponent");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Material, "Material");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Texture, "Texture");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::AnimInstance, "AnimInstance");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Rotator, "Rotator");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::LinearColor, "LinearColor");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Vector4, "Vector4");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Class, "Class");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Enum, "Enum");
        AddVariableMenuItem(Blueprint, ELuaBlueprintPinType::Name, "Name");
        ImGui::EndPopup();
    }

    TArray<FLuaBlueprintVariable>& Variables = Blueprint->GetMutableVariables();
    if (Variables.empty())
    {
        ImGui::TextDisabled("No variables. Click \"+ New Variable\".");
    }

    for (int32 Index = 0; Index < static_cast<int32>(Variables.size()); ++Index)
    {
        FLuaBlueprintVariable& Variable = Variables[Index];
        ImGui::PushID(Index);

        const ImVec4 TypeColor = PinTypeColor(Variable.Type);
        // 행 헤더: 타입 색상 + 이름. drag source — 캔버스로 끌어다 놓으면 Get/Set 선택.
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        const FString HeaderLabel = FString(Variable.Name.ToString()) + " : " + PinTypeLabel(Variable.Type);
        const bool    bOpen       = ImGui::TreeNodeEx("##Var", ImGuiTreeNodeFlags_DefaultOpen, " ");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, TypeColor);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", HeaderLabel.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && ImGui::CalcTextSize(HeaderLabel.c_str()).x > ImGui::GetContentRegionAvail().x)
        {
            ImGui::SetTooltip("%s", HeaderLabel.c_str());
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            const FName DragName = Variable.Name;
            ImGui::SetDragDropPayload("LuaBlueprintVariable", &DragName, sizeof(FName));
            ImGui::TextColored(TypeColor, "%s", HeaderLabel.c_str());
            ImGui::EndDragDropSource();
        }

        if (bOpen)
        {
            RenderVariableEditor(Blueprint, Variable, Index);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void FLuaBlueprintEditorWidget::RenderPalettePanel(ULuaBlueprintAsset* Blueprint)
{
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##palettesearch", "Search nodes...", PaletteSearchBuf, sizeof(PaletteSearchBuf));

    struct FPaletteItem
    {
        const char*           Category;
        ELuaBlueprintNodeType Type;
    };
    // 우클릭 Add Node 메뉴와 동일한 카탈로그를 평평하게 펼친 형태. 카테고리별로 색상 불릿 + 클릭 추가.
    static const FPaletteItem Items[] = {
        { "Events", ELuaBlueprintNodeType::EventBeginPlay },
        { "Events", ELuaBlueprintNodeType::EventTick },
        { "Events", ELuaBlueprintNodeType::EventEndPlay },
        { "Events", ELuaBlueprintNodeType::EventOverlap },
        { "Events", ELuaBlueprintNodeType::EventEndOverlap },
        { "Events", ELuaBlueprintNodeType::EventHit },
        { "Events", ELuaBlueprintNodeType::EventEndHit },
        { "Events", ELuaBlueprintNodeType::EventPostBeginPlay },
        { "Events", ELuaBlueprintNodeType::EventPostStartMatch },
        { "Events", ELuaBlueprintNodeType::EventPlayerCameraReady },
        { "Events", ELuaBlueprintNodeType::EventInputAction },
        { "Events", ELuaBlueprintNodeType::EventInputAxis },
        { "Flow", ELuaBlueprintNodeType::Sequence },
        { "Flow", ELuaBlueprintNodeType::Branch },
        { "Flow", ELuaBlueprintNodeType::ForLoop },
        { "Flow", ELuaBlueprintNodeType::WhileLoop },
        { "Flow", ELuaBlueprintNodeType::ForEachActorByClass },
        { "Flow", ELuaBlueprintNodeType::ForEachActorByTag },
        { "Flow", ELuaBlueprintNodeType::ForEachArray },
        { "Flow", ELuaBlueprintNodeType::Delay },
        { "Actor", ELuaBlueprintNodeType::SpawnActor },
        { "Actor", ELuaBlueprintNodeType::SpawnPawn },
        { "Actor", ELuaBlueprintNodeType::SpawnPawnAndPossess },
        { "Actor", ELuaBlueprintNodeType::DestroyActor },
        { "Actor", ELuaBlueprintNodeType::FindActorByName },
        { "Actor", ELuaBlueprintNodeType::FindActorByClass },
        { "Actor", ELuaBlueprintNodeType::FindActorByTag },
        { "Actor", ELuaBlueprintNodeType::GetActorLocation },
        { "Actor", ELuaBlueprintNodeType::SetActorLocation },
        { "Actor", ELuaBlueprintNodeType::GetActorRotation },
        { "Actor", ELuaBlueprintNodeType::SetActorRotation },
        { "Actor", ELuaBlueprintNodeType::GetActorForward },
        { "Actor", ELuaBlueprintNodeType::AddActorWorldOffset },
        { "Game Framework", ELuaBlueprintNodeType::GetPlayerController },
        { "Game Framework", ELuaBlueprintNodeType::GetControlledPawn },
        { "Game Framework", ELuaBlueprintNodeType::Possess },
        { "Game Framework", ELuaBlueprintNodeType::GetInputComponent },
        { "Component", ELuaBlueprintNodeType::GetRootComponent },
        { "Component", ELuaBlueprintNodeType::GetComponentByName },
        { "Component", ELuaBlueprintNodeType::AddForce },
        { "Component", ELuaBlueprintNodeType::AddTorque },
        { "Component", ELuaBlueprintNodeType::GetLinearVelocity },
        { "Component", ELuaBlueprintNodeType::SetLinearVelocity },
        { "Component", ELuaBlueprintNodeType::SetSimulatePhysics },
        { "Variables", ELuaBlueprintNodeType::GetVariable },
        { "Variables", ELuaBlueprintNodeType::SetVariable },
        { "Variables", ELuaBlueprintNodeType::GetProperty },
        { "Variables", ELuaBlueprintNodeType::SetProperty },
        { "Variables", ELuaBlueprintNodeType::Self },
        { "Anim Graph", ELuaBlueprintNodeType::GetSkeletalMeshComponent },
        { "Anim Graph", ELuaBlueprintNodeType::GetAnimInstance },
        { "Anim Graph", ELuaBlueprintNodeType::SetAnimGraphVariableFloat },
        { "Anim Graph", ELuaBlueprintNodeType::SetAnimGraphVariableBool },
        { "Anim Graph", ELuaBlueprintNodeType::SetAnimGraphVariableInt },
        { "Anim Graph", ELuaBlueprintNodeType::GetAnimGraphVariableFloat },
        { "Anim Graph", ELuaBlueprintNodeType::GetAnimGraphVariableBool },
        { "Anim Graph", ELuaBlueprintNodeType::GetAnimGraphVariableInt },
        { "Functions", ELuaBlueprintNodeType::CallFunction },
        { "Functions", ELuaBlueprintNodeType::CustomEvent },
        { "Functions", ELuaBlueprintNodeType::CallCustomEvent },
        { "Functions", ELuaBlueprintNodeType::CustomLuaFunction },
        { "Functions", ELuaBlueprintNodeType::CallCustomLuaFunction },
        { "Delegates", ELuaBlueprintNodeType::BindEvent },
        { "Delegates", ELuaBlueprintNodeType::UnbindEvent },
        { "Utility", ELuaBlueprintNodeType::Lerp },
        { "Utility", ELuaBlueprintNodeType::Clamp },
        { "Utility", ELuaBlueprintNodeType::Min },
        { "Utility", ELuaBlueprintNodeType::Max },
        { "Utility", ELuaBlueprintNodeType::RandomFloat },
        { "Utility", ELuaBlueprintNodeType::Sin },
        { "Utility", ELuaBlueprintNodeType::Cos },
        { "Utility", ELuaBlueprintNodeType::Sqrt },
        { "Utility", ELuaBlueprintNodeType::Distance },
        { "Utility", ELuaBlueprintNodeType::GetGameTime },
        { "Math (Float)", ELuaBlueprintNodeType::AddFloat },
        { "Math (Float)", ELuaBlueprintNodeType::SubtractFloat },
        { "Math (Float)", ELuaBlueprintNodeType::MultiplyFloat },
        { "Math (Float)", ELuaBlueprintNodeType::DivideFloat },
        { "Math (Int)", ELuaBlueprintNodeType::AddInt },
        { "Math (Int)", ELuaBlueprintNodeType::SubtractInt },
        { "Math (Int)", ELuaBlueprintNodeType::MultiplyInt },
        { "Math (Int)", ELuaBlueprintNodeType::DivideInt },
        { "Compare", ELuaBlueprintNodeType::EqualFloat },
        { "Compare", ELuaBlueprintNodeType::LessFloat },
        { "Compare", ELuaBlueprintNodeType::GreaterFloat },
        { "Compare", ELuaBlueprintNodeType::EqualInt },
        { "Logic", ELuaBlueprintNodeType::And },
        { "Logic", ELuaBlueprintNodeType::Or },
        { "Logic", ELuaBlueprintNodeType::Not },
        { "Vector", ELuaBlueprintNodeType::MakeVector },
        { "Vector", ELuaBlueprintNodeType::BreakVector },
        { "Vector", ELuaBlueprintNodeType::AddVector },
        { "Vector", ELuaBlueprintNodeType::ScaleVector },
        { "Vector", ELuaBlueprintNodeType::DotVector },
        { "Vector", ELuaBlueprintNodeType::CrossVector },
        { "Vector", ELuaBlueprintNodeType::NormalizeVector },
        { "String", ELuaBlueprintNodeType::AppendString },
        { "Conversions", ELuaBlueprintNodeType::ToInt },
        { "Conversions", ELuaBlueprintNodeType::ToFloat },
        { "Conversions", ELuaBlueprintNodeType::ToString },
        { "Conversions", ELuaBlueprintNodeType::ToVector },
        { "Literals", ELuaBlueprintNodeType::LiteralBool },
        { "Literals", ELuaBlueprintNodeType::LiteralInt },
        { "Literals", ELuaBlueprintNodeType::LiteralFloat },
        { "Literals", ELuaBlueprintNodeType::LiteralString },
        { "Literals", ELuaBlueprintNodeType::LiteralVector },
        { "Graph Utility", ELuaBlueprintNodeType::Reroute },
        { "Graph Utility", ELuaBlueprintNodeType::Comment },
        { "Debug", ELuaBlueprintNodeType::PrintString },
    };

    const char* CurrentCategory = nullptr;
    const bool  bSearching      = PaletteSearchBuf[0] != 0;
    for (const FPaletteItem& Item : Items)
    {
        const char* Label = NodeTypeLabel(Item.Type);
        if (bSearching && !ContainsCaseInsensitive(Label, PaletteSearchBuf)) continue;

        // 검색 중이 아닐 때만 카테고리 머리글을 노출(검색 모드는 평평한 리스트).
        if (!bSearching && (!CurrentCategory || std::strcmp(CurrentCategory, Item.Category) != 0))
        {
            CurrentCategory = Item.Category;
            ImGui::Spacing();
            ImGui::TextDisabled("%s", Item.Category);
            ImGui::Separator();
        }

        // 이벤트 노드는 그래프에 하나만 존재 가능 — 이미 있으면 비활성.
        const bool bDisabled = IsEventNode(Item.Type) && HasNodeOfType(Blueprint, Item.Type);
        ImGui::BeginDisabled(bDisabled);

        ImGui::PushStyleColor(ImGuiCol_Text, NodeHeaderColor(Item.Type));
        ImGui::Bullet();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushID(static_cast<int32>(Item.Type));
        if (ImGui::Selectable(Label))
        {
            SpawnPaletteNode(Blueprint, Item.Type);
        }
        if (ImGui::IsItemHovered() && ImGui::CalcTextSize(Label).x > ImGui::GetContentRegionAvail().x)
        {
            ImGui::SetTooltip("%s", Label);
        }
        ImGui::PopID();

        ImGui::EndDisabled();
    }

    if (bSearching)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Reflected Functions");
        ImGui::Separator();
        const ImVec2 Spawn(PendingNewNodePosition.x + 30.0f, PendingNewNodePosition.y + 30.0f);
        RenderReflectedFunctionNodeMenu(Blueprint, PaletteSearchBuf, Spawn, true, true);
    }
}

void FLuaBlueprintEditorWidget::SpawnPaletteNode(ULuaBlueprintAsset* Blueprint, ELuaBlueprintNodeType Type)
{
    // Palette/toolbar 는 ed 컨텍스트 밖에서 호출되므로 graph 좌표로 직접 stagger 한다 (Material Palette 와 동일).
    const ImVec2       Spawn(PendingNewNodePosition.x + 30.0f, PendingNewNodePosition.y + 30.0f);
    FLuaBlueprintNode* NewNode = Blueprint->AddNodeOfType(Type, Spawn.x, Spawn.y);
    if (!NewNode) return;

    NewNode->PosX = Spawn.x;
    NewNode->PosY = Spawn.y;
    if (NodeEditorContext)
    {
        FScopedNodeEditorCurrent Scope(NodeEditorContext);
        ed::SetNodePosition(ToNodeId(NewNode->NodeId), Spawn);
    }
    PendingNewNodePosition = Spawn;
    SelectOnlyNodes(TArray<uint32> { NewNode->NodeId });
    CommitBlueprintEdit(Blueprint);
}

void FLuaBlueprintEditorWidget::RenderGraph(ULuaBlueprintAsset* Blueprint)
{
    const float InspectorWidth = 360.0f;
    const float Spacing        = ImGui::GetStyle().ItemSpacing.x;
    const float TotalWidth     = ImGui::GetContentRegionAvail().x;
    const float CanvasWidth    = (TotalWidth > InspectorWidth + Spacing + 120.0f) ? TotalWidth - InspectorWidth - Spacing
            : TotalWidth;

    uint32 SelectedNodeId = 0;

    ImGui::BeginChild("##LuaBlueprintCanvasChild", ImVec2(CanvasWidth, 0), ImGuiChildFlags_Borders);

    const ImVec2 TooltipOwnerMin = ImGui::GetWindowPos();
    const ImVec2 TooltipOwnerMax(
        TooltipOwnerMin.x + ImGui::GetWindowSize().x,
        TooltipOwnerMin.y + ImGui::GetWindowSize().y
    );
    const ImRect         TooltipOwnerRect(TooltipOwnerMin, TooltipOwnerMax);
    const ImGuiViewport* TooltipOwnerViewport   = ImGui::GetWindowViewport();
    const ImGuiID        TooltipOwnerViewportId = TooltipOwnerViewport ? TooltipOwnerViewport->ID : 0;
    InlineAssetPickerOwnerMin = TooltipOwnerMin;
    InlineAssetPickerOwnerMax = TooltipOwnerMax;

    ed::SetCurrentEditor(NodeEditorContext);
    ed::Begin("LuaBlueprintCanvas");

    // 이전 프레임에 변수 drop 이 들어왔다면 이제 ed 컨텍스트가 활성 상태이므로
    // 안전하게 screen→canvas 변환을 끝내고 Get/Set 팝업을 띄울 준비.
    if (bPendingVariableDrop)
    {
        PendingVariableDropPos = ed::ScreenToCanvas(PendingVariableScreenPos);
        bPendingVariableDrop   = false;
        bShowVariableDropMenu  = true;
    }

    if (!bPositionsPushed)
    {
        for (const FLuaBlueprintNode& Node : Blueprint->GetNodes())
        {
            ed::SetNodePosition(ToNodeId(Node.NodeId), ImVec2(Node.PosX, Node.PosY));
        }
        bPositionsPushed = true;
    }

    ProcessQueuedNodeEditorCommands(Blueprint);

    bool                  bAnyNodeGeometryChangedThisFrame = false;
    bool                  bHoveredNodeHelpIcon             = false;
    ELuaBlueprintNodeType HoveredNodeHelpType              = ELuaBlueprintNodeType::Comment;
    FDeferredTooltip      DeferredNodeTooltip;
    bInlineAssetHoverTooltipVisible = false;
    InlineAssetHoverTooltipPath.clear();

    for (FLuaBlueprintNode& Node : Blueprint->GetMutableNodes())
    {
        // Comment 는 ed::Group 으로 노출 — 내부에 겹친 다른 노드들을 같이 드래그함 (UE BP Comment).
        if (Node.Type == ELuaBlueprintNodeType::Comment)
        {
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(110, 95, 30, 80));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(220, 200, 100, 200));

            ed::BeginNode(ToNodeId(Node.NodeId));
            ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.5f, 1.0f), "%s", Node.StringValue.empty() ? "Comment" : Node.StringValue.c_str());
            if (RenderNodeHelpIcon(Node.Type, HoveredNodeHelpType))
            {
                bHoveredNodeHelpIcon = true;
            }
            const ImVec2 GroupSize((std::max)(80.0f, Node.VectorValue.X), (std::max)(40.0f, Node.VectorValue.Y));
            ed::Group(GroupSize);
            ed::EndNode();

            // 사용자가 ed 측 핸들로 리사이즈한 경우 VectorValue 도 동기화.
            const ImVec2 ActualSize = ed::GetNodeSize(ToNodeId(Node.NodeId));
            if (ActualSize.x > 0 && ActualSize.y > 0)
            {
                if (std::fabs(Node.VectorValue.X - ActualSize.x) > 0.5f || std::fabs(Node.VectorValue.Y - ActualSize.y) > 0.5f)
                {
                    Node.VectorValue.X = ActualSize.x;
                    Node.VectorValue.Y = ActualSize.y;
                    Blueprint->BumpEditorVersion();
                    // 생성 직후 ed 가 코멘트 박스 크기를 1~2 프레임에 걸쳐 확정하는 "자동 정착"은
                    // 사용자가 직접 리사이즈한 게 아니므로 별도 undo 스냅샷을 만들면 안 된다.
                    // (안 그러면 그룹 생성 직후 정착 커밋이 끼어들어 Undo 한 번이 헛돈다 → 그룹 실행취소 버그)
                    // 실제 리사이즈 드래그(마우스 다운) 중일 때만 geometry 변경으로 커밋한다.
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        bAnyNodeGeometryChangedThisFrame = true;
                    }
                }
            }

            ed::PopStyleColor(2);
            continue;
        }

        const bool    bCanNodeBreakpoint = IsLuaBlueprintExecutableDebugNode(Node);
        const FString DebugSourcePath    = NormalizeLuaBlueprintDebugPath(Blueprint->GetSourcePath());
        const bool    bPausedHere        = IsPausedOnLuaBlueprintNode(Blueprint, Node);

        bool bRecentlyExecuted = false;
        {
            TArray<FLuaDebugTraceEntry> Trace = FLuaDebugManager::GetRecentTrace();
            for (auto It = Trace.rbegin(); It != Trace.rend(); ++It)
            {
                if (It->Event == "Node" || It->Event == "Pause")
                {
                    bRecentlyExecuted = It->Location.bLuaBlueprint
                            && NormalizeLuaBlueprintDebugPath(It->Location.BlueprintPath) == DebugSourcePath
                            && It->Location.NodeId == Node.NodeId;
                    break;
                }
            }
        }

        int32 DebugStylePushes = 0;
        if (bCanNodeBreakpoint && Node.bBreakpointEnabled)
        {
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(220, 50, 50, 230));
            ++DebugStylePushes;
        }
        if (bRecentlyExecuted && !bPausedHere)
        {
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(80, 170, 255, 230));
            ++DebugStylePushes;
        }
        if (bPausedHere)
        {
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(70, 48, 18, 230));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 190, 60, 255));
            DebugStylePushes += 2;
        }
        ed::BeginNode(ToNodeId(Node.NodeId));

        ImGui::PushID(static_cast<int>(Node.NodeId));
        if (RenderBreakpointGlyphButton(Node.bBreakpointEnabled, bCanNodeBreakpoint, "##NodeBreakpoint", DeferredNodeTooltip))
        {
            Node.bBreakpointEnabled = !Node.bBreakpointEnabled;
            FLuaDebugManager::SetNodeBreakpoint(Blueprint->GetSourcePath(), Node.NodeId, Node.bBreakpointEnabled);
            Blueprint->BumpEditorVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::PopID();
        ImGui::SameLine(0.0f, 6.0f);

        if (bPausedHere)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.22f, 1.0f), "▶");
            ImGui::SameLine(0.0f, 6.0f);
        }
        else if (bRecentlyExecuted)
        {
            ImGui::TextColored(ImVec4(0.35f, 0.70f, 1.0f, 1.0f), "•");
            ImGui::SameLine(0.0f, 6.0f);
        }
        const FString HeaderLabel = NodeHeaderDisplayLabel(Node);
        ImGui::TextColored(NodeHeaderColor(Node.Type), "%s", HeaderLabel.c_str());
        if (RenderNodeHelpIcon(Node.Type, HoveredNodeHelpType))
        {
            bHoveredNodeHelpIcon = true;
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        RenderNodeBody(Blueprint, Node);

        if (bPausedHere)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.22f, 1.0f), "Paused values");
            RenderLuaDebugInlineSnapshot(FLuaDebugManager::GetPausedSnapshot());
        }

        RenderNodePins(Blueprint, Node);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ed::EndNode();
        if (DebugStylePushes > 0)
        {
            ed::PopStyleColor(DebugStylePushes);
        }
    }

    Blueprint->RefreshAllNodePinTypes();
    if (Blueprint->RemoveInvalidLinks())
    {
        CommitBlueprintEdit(Blueprint);
    }

    for (const FLuaBlueprintLink& Link : Blueprint->GetLinks())
    {
        // 링크 색상 = output pin 타입 색상. 데이터 흐름 가독성 강화.
        ImVec4 LinkColor(0.8f, 0.8f, 0.8f, 1.0f);
        if (const FLuaBlueprintPin* From = Blueprint->FindPin(Link.FromPinId))
        {
            LinkColor = PinTypeColor(From->Type);
        }
        ed::Link(ToLinkId(Link.LinkId), ToPinId(Link.FromPinId), ToPinId(Link.ToPinId), LinkColor);
    }

    if (bPendingNavigateToNode && PendingNavigateToNodeId != 0 && Blueprint->FindNode(PendingNavigateToNodeId))
    {
        SelectOnlyNodes(TArray<uint32> { PendingNavigateToNodeId });
        ed::NavigateToSelection(true, 0.18f);
        SelectedNodeId = PendingNavigateToNodeId;
        bPendingInitialContentFit = false;
        bPendingNavigateToNode = false;
        PendingNavigateToNodeId = 0;
    }

    // SetNodePosition 직후가 아니라, 실제 노드들이 이번 frame 에 제출된 뒤 fit 해야
    // node-editor 내부 bounds 가 살아있는 노드 기준으로 계산된다.
    if (bPendingInitialContentFit && !Blueprint->GetNodes().empty())
    {
        ed::NavigateToContent(0.0f);
        bPendingInitialContentFit = false;
    }

    if (ed::BeginCreate())
    {
        ed::PinId StartId, EndId;
        if (ed::QueryNewLink(&StartId, &EndId))
        {
            if (StartId && EndId)
            {
                uint32 FromPinId    = 0;
                uint32 ToPinIdValue = 0;
                if (Blueprint->CanLinkPins(PinIdToU32(StartId), PinIdToU32(EndId), &FromPinId, &ToPinIdValue))
                {
                    if (ed::AcceptNewItem())
                    {
                        Blueprint->AddLink(FromPinId, ToPinIdValue);
                        if (const FLuaBlueprintPin* FromPin = Blueprint->FindPin(FromPinId))
                        {
                            if (FLuaBlueprintPin* ToPin = Blueprint->FindPin(ToPinIdValue))
                            {
                                FLuaBlueprintNode* FromNode = Blueprint->FindNode(FromPin->OwningNodeId);
                                FLuaBlueprintNode* ToNode = Blueprint->FindNode(ToPin->OwningNodeId);
                                if (FromNode && ToNode && FromNode->Type == ELuaBlueprintNodeType::CustomLuaFunction &&
                                    ToNode->Type == ELuaBlueprintNodeType::CallCustomLuaFunction &&
                                    ToPin->DisplayName.ToString() == "Function")
                                {
                                    SetCallCustomLuaFunctionTarget(Blueprint, *ToNode, FromNode);
                                }
                            }
                        }
                        CommitBlueprintEdit(Blueprint);
                    }
                }
                else
                {
                    ed::RejectNewItem(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), 2.0f);
                }
            }
        }

        // 핀을 빈 공간으로 드래그해 놓는 경우는 QueryNewLink(Start, End)만으로는
        // 안정적으로 잡히지 않는다. ax::NodeEditor의 QueryNewNode 경로를 사용해야
        // release 시점에 context-sensitive node popup을 열 수 있다.
        ed::PinId NewNodePinId = 0;
        if (ed::QueryNewNode(&NewNodePinId))
        {
            if (NewNodePinId && ed::AcceptNewItem(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), 1.5f))
            {
                PendingPinSpawnPinId   = PinIdToU32(NewNodePinId);
                PendingPinSpawnPos     = ed::ScreenToCanvas(ImGui::GetMousePos());
                PendingNewNodePosition = PendingPinSpawnPos;
                PinSpawnSearchBuf[0]   = 0;
                bShowPinSpawnMenu      = true;
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
                if (Blueprint->RemoveLink(LinkIdToU32(DeletedLink))) CommitBlueprintEdit(Blueprint);
            }
        }

        TArray<uint32> DeletedNodeIds;
        ed::NodeId     DeletedNode;
        while (ed::QueryDeletedNode(&DeletedNode))
        {
            if (ed::AcceptDeletedItem())
            {
                DeletedNodeIds.push_back(NodeIdToU32(DeletedNode));
            }
        }
        if (!DeletedNodeIds.empty() && DeleteNodesIncludingContainedGroups(Blueprint, DeletedNodeIds))
        {
            CommitBlueprintEdit(Blueprint);
        }
    }
    ed::EndDelete();

    for (FLuaBlueprintNode& Node : Blueprint->GetMutableNodes())
    {
        const ImVec2 Pos = ed::GetNodePosition(ToNodeId(Node.NodeId));
        if (std::fabs(Node.PosX - Pos.x) > 0.01f || std::fabs(Node.PosY - Pos.y) > 0.01f)
        {
            Node.PosX = Pos.x;
            Node.PosY = Pos.y;
            Blueprint->BumpEditorVersion();
            bAnyNodeGeometryChangedThisFrame = true;
        }
    }

    // Node-editor drag/resize updates can arrive every frame. Keep the data model live so hit tests,
    // grouping and saving use current coordinates, but push only one undo snapshot when the drag ends.
    const bool bMouseDownForGeometryDrag = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (bSuppressInitialGeometryDirty)
    {
        // 블루프린트 더블클릭해서 열었을 때 MouseDown => Drag로 판정되어 Dirty로 표시되는 것 방지
        if (bAnyNodeGeometryChangedThisFrame || bPendingNodeGeometryEdit)
        {
            CaptureInitialUndoSnapshot(Blueprint);
            ClearDirty();
            bPendingNodeGeometryEdit = false;
        }
        if (!bMouseDownForGeometryDrag)
        {
            bSuppressInitialGeometryDirty = false;
        }
    }
    else if (bAnyNodeGeometryChangedThisFrame)
    {
        bPendingNodeGeometryEdit = true;
        if (!bMouseDownForGeometryDrag)
        {
            CommitBlueprintEdit(Blueprint);
            bPendingNodeGeometryEdit = false;
        }
    }
    else if (bPendingNodeGeometryEdit && !bMouseDownForGeometryDrag)
    {
        CommitBlueprintEdit(Blueprint);
        bPendingNodeGeometryEdit = false;
    }

    ed::NodeId ContextNodeId = 0;
    ed::PinId  ContextPinId  = 0;
    ed::LinkId ContextLinkId = 0;

    ed::Suspend();
    if (ed::ShowNodeContextMenu(&ContextNodeId))
    {
        ContextMenuNodeId = NodeIdToU32(ContextNodeId);
        ContextMenuPinId  = 0;
        ContextMenuLinkId = 0;
        if (ContextMenuNodeId != 0)
        {
            SelectOnlyNodes(TArray<uint32> { ContextMenuNodeId });
        }
        ImGui::OpenPopup("LuaBlueprintNodeMenu");
    }
    else if (ed::ShowPinContextMenu(&ContextPinId))
    {
        ContextMenuNodeId = 0;
        ContextMenuPinId  = PinIdToU32(ContextPinId);
        ContextMenuLinkId = 0;
        PendingPinSpawnPinId   = ContextMenuPinId;
        PendingPinSpawnPos     = ed::ScreenToCanvas(ImGui::GetMousePos());
        PendingNewNodePosition = PendingPinSpawnPos;
        PinSpawnSearchBuf[0]   = 0;
        ImGui::OpenPopup("LuaBlueprintPinMenu");
    }
    else if (ed::ShowLinkContextMenu(&ContextLinkId))
    {
        ContextMenuNodeId = 0;
        ContextMenuPinId  = 0;
        ContextMenuLinkId = LinkIdToU32(ContextLinkId);
        ImGui::OpenPopup("LuaBlueprintLinkMenu");
    }
    else if (ed::ShowBackgroundContextMenu())
    {
        ContextMenuNodeId = 0;
        ContextMenuPinId  = 0;
        ContextMenuLinkId = 0;
        PendingNewNodePosition = ed::ScreenToCanvas(ImGui::GetMousePos());
        AddNodeSearchBuf[0]    = 0;
        ImGui::OpenPopup("LuaBlueprintBackgroundMenu");
    }

    if (ImGui::BeginPopup("LuaBlueprintNodeMenu"))
    {
        if (FLuaBlueprintNode* ContextNode = Blueprint->FindNode(ContextMenuNodeId))
        {
            if (ContextNode->Type == ELuaBlueprintNodeType::CustomLuaFunction)
            {
                if (ImGui::MenuItem("Edit Code"))
                {
                    OpenCustomLuaFunctionEditor(Blueprint, ContextNode->NodeId);
                }
                ImGui::Separator();
            }
            if (IsLuaBlueprintExecutableDebugNode(*ContextNode))
            {
                const char* BreakpointLabel = ContextNode->bBreakpointEnabled ? "Disable Breakpoint" : "Enable Breakpoint";
                if (ImGui::MenuItem(BreakpointLabel, "F9"))
                {
                    ContextNode->bBreakpointEnabled = !ContextNode->bBreakpointEnabled;
                    FLuaDebugManager::SetNodeBreakpoint(Blueprint->GetSourcePath(), ContextNode->NodeId, ContextNode->bBreakpointEnabled);
                    Blueprint->BumpEditorVersion();
                    CommitBlueprintEdit(Blueprint);
                }
                ImGui::Separator();
            }
        }
        if (ImGui::MenuItem("Copy"))
        {
            bQueuedCopySelected = true;
        }
        if (ImGui::MenuItem("Duplicate"))
        {
            bQueuedDuplicateSelected = true;
        }
        // 다중 선택 확인은 ed::GetSelectedNodes 가 ed 컨텍스트를 요구하므로
        // 정확한 개수 체크 대신 메뉴는 항상 노출하고 핸들러에서 1개 미만이면 no-op.
        if (ImGui::MenuItem("Group Selection as Comment"))
        {
            bQueuedGroupSelected = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete"))
        {
            TArray<uint32> RootNodeIds;
            RootNodeIds.push_back(ContextMenuNodeId);
            if (DeleteNodesIncludingContainedGroups(Blueprint, RootNodeIds)) CommitBlueprintEdit(Blueprint);
            ContextMenuNodeId = 0;
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("LuaBlueprintLinkMenu"))
    {
        if (ImGui::MenuItem("Break Link"))
        {
            if (ContextMenuLinkId != 0 && Blueprint->RemoveLink(ContextMenuLinkId)) CommitBlueprintEdit(Blueprint);
            ContextMenuLinkId = 0;
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("LuaBlueprintPinMenu"))
    {
        if (ContextMenuPinId != 0)
        {
            PendingPinSpawnPinId = ContextMenuPinId;
        }
        RenderPinSpawnMenu(Blueprint);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("LuaBlueprintBackgroundMenu"))
    {
        if (!ClipboardNodes.empty() && ImGui::MenuItem("Paste", "Ctrl+V"))
        {
            PasteCopiedNodes(Blueprint, &PendingNewNodePosition);
        }
        if (ImGui::MenuItem("Group Selection as Comment"))
        {
            bQueuedGroupSelected = true;
        }
        ImGui::Separator();
        RenderAddNodeMenu(Blueprint);
        ImGui::EndPopup();
    }

    if (bShowPinSpawnMenu)
    {
        ImGui::OpenPopup("LuaBlueprintPinSpawnMenu");
        bShowPinSpawnMenu = false;
    }
    if (ImGui::BeginPopup("LuaBlueprintPinSpawnMenu"))
    {
        RenderPinSpawnMenu(Blueprint);
        ImGui::EndPopup();
    }

    // 변수 drop → Get/Set 팝업 (캔버스에서 직접 받는 drop 은 ed context 에 막혀
    // background context menu 와 같은 timing 으로 처리).
    if (bShowVariableDropMenu)
    {
        ImGui::OpenPopup("LuaBlueprintVariableDropMenu");
        bShowVariableDropMenu = false;
    }
    if (ImGui::BeginPopup("LuaBlueprintVariableDropMenu"))
    {
        ImGui::TextDisabled("%s", PendingVariableDropName.ToString().c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Get"))
        {
            SpawnVariableNode(
                Blueprint,
                ELuaBlueprintNodeType::GetVariable,
                PendingVariableDropName,
                PendingVariableDropPos
            );
        }
        if (ImGui::MenuItem("Set"))
        {
            SpawnVariableNode(
                Blueprint,
                ELuaBlueprintNodeType::SetVariable,
                PendingVariableDropName,
                PendingVariableDropPos
            );
        }
        ImGui::EndPopup();
    }

    ed::Resume();

    {
        ed::NodeId SelectedNodes[1];
        const int  SelectedCount = ed::GetSelectedNodes(SelectedNodes, 1);
        if (SelectedCount > 0)
        {
            SelectedNodeId    = NodeIdToU32(SelectedNodes[0]);
            const ImGuiIO& IO = ImGui::GetIO();
            if (!IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F9))
            {
                if (FLuaBlueprintNode* Node = Blueprint->FindNode(SelectedNodeId))
                {
                    if (IsLuaBlueprintExecutableDebugNode(*Node))
                    {
                        Node->bBreakpointEnabled = !Node->bBreakpointEnabled;
                        FLuaDebugManager::SetNodeBreakpoint(Blueprint->GetSourcePath(), Node->NodeId, Node->bBreakpointEnabled);
                        Blueprint->BumpEditorVersion();
                        CommitBlueprintEdit(Blueprint);
                    }
                }
            }
        }
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);

    // NodeEditor canvas transform/clipping is no longer active here.
    // Render hover tooltips and the inline asset picker only from this stable layer.
    RenderDeferredInlineAssetPicker(Blueprint);

    if (bHoveredNodeHelpIcon)
    {
        RenderNodeHelpTooltip(HoveredNodeHelpType, TooltipOwnerRect, TooltipOwnerViewportId);
    }
    else if (bInlineAssetHoverTooltipVisible)
    {
        FDeferredTooltip Tooltip;
        SetDeferredTooltip(Tooltip, FString(), InlineAssetHoverTooltipPath);
        RenderDeferredTooltip(Tooltip);
    }
    else
    {
        RenderDeferredTooltip(DeferredNodeTooltip);
    }

    // 캔버스 child 위의 빈 영역에서 drag-drop 수신. ed 컨텍스트는 자체 hit-test 를 하지만,
    // EndChild 직전 dummy invisible button 으로 받는 게 가장 안정적.
    HandleVariableDropOnCanvas();

    ImGui::EndChild();

    if (CanvasWidth < TotalWidth)
    {
        ImGui::SameLine();
        ImGui::BeginChild("##LuaBlueprintInspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (SelectedNodeId != 0)
        {
            if (FLuaBlueprintNode* Node = Blueprint->FindNode(SelectedNodeId))
            {
                RenderNodeInspector(Blueprint, *Node);
            }
            else
            {
                ImGui::TextDisabled("Stale selection.");
            }
        }
        else
        {
            ImGui::TextDisabled("Select a node to edit details.");
        }
        ImGui::EndChild();
    }
}

void FLuaBlueprintEditorWidget::RenderNodeBody(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node)
{
    // Material editor 가 노드 본문에 색 swatch / 텍스처 썸네일을 그리는 것과 같은 자리.
    // LuaBP literal 노드는 인라인으로 값 편집기를 표시 — 노드 안에서 바로 수정 가능.
    switch (Node.Type)
    {
    case ELuaBlueprintNodeType::LiteralBool:
        ImGui::PushID(static_cast<int>(Node.NodeId));
        if (ImGui::Checkbox("##lb", &Node.BoolValue))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::PopID();
        break;
    case ELuaBlueprintNodeType::LiteralInt:
        ImGui::PushID(static_cast<int>(Node.NodeId));
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##li", &Node.IntValue, 0))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::PopID();
        break;
    case ELuaBlueprintNodeType::LiteralFloat:
        ImGui::PushID(static_cast<int>(Node.NodeId));
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::DragFloat("##lf", &Node.FloatValue, 0.01f, 0.0f, 0.0f, "%.3f"))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::PopID();
        break;
    case ELuaBlueprintNodeType::LiteralString:
    {
        ImGui::PushID(static_cast<int>(Node.NodeId));
        char Buf[256];
        CopyToBuffer(Buf, sizeof(Buf), Node.StringValue);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("##ls", Buf, sizeof(Buf)))
        {
            Node.StringValue = Buf;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::PopID();
        break;
    }
    case ELuaBlueprintNodeType::LiteralVector:
    {
        ImGui::PushID(static_cast<int>(Node.NodeId));
        float V[3] = { Node.VectorValue.X, Node.VectorValue.Y, Node.VectorValue.Z };
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloat3("##lv", V, 0.01f))
        {
            Node.VectorValue = FVector(V[0], V[1], V[2]);
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::PopID();
        break;
    }
    case ELuaBlueprintNodeType::GetVariable:
    case ELuaBlueprintNodeType::SetVariable:
        // 변수명은 노드 제목("Get/Set <Name>")에 표시한다.
        // 본문에 별도 [Name] 라벨을 두면 핀 행과 분리되어 떠 보인다.
        break;
    case ELuaBlueprintNodeType::GetProperty:
    case ELuaBlueprintNodeType::SetProperty:
    case ELuaBlueprintNodeType::CallFunction:
        if (Node.NameValue != FName::None)
        {
            ImGui::TextDisabled(".%s", Node.NameValue.ToString().c_str());
        }
        break;
    case ELuaBlueprintNodeType::CallFunctionSignature:
        if (!Node.StringValue.empty())
        {
            ImGui::TextDisabled("%s", Node.StringValue.c_str());
        }
        break;
    case ELuaBlueprintNodeType::CustomLuaFunction:
    {
        ImGui::TextDisabled("%s", Node.NameValue == FName::None ? "(no function)" : Node.NameValue.ToString().c_str());
        ImGui::TextWrapped("Double-click here or press Edit Code to write Lua.");
        const bool bDoubleClickedBody = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (ImGui::SmallButton("Edit Code"))
        {
            OpenCustomLuaFunctionEditor(Blueprint, Node.NodeId);
        }
        if (bDoubleClickedBody)
        {
            OpenCustomLuaFunctionEditor(Blueprint, Node.NodeId);
        }
        break;
    }
    case ELuaBlueprintNodeType::CallCustomLuaFunction:
    {
        const FLuaBlueprintNode* Target = ResolveCustomLuaFunctionCallTarget(Blueprint, Node);
        const FString TargetLabel = Target ? Target->NameValue.ToString() : Node.NameValue.ToString();
        ImGui::TextDisabled("%s", TargetLabel.empty() ? "(no function)" : TargetLabel.c_str());
        if (Target && Node.IntValue == static_cast<int32>(Target->NodeId))
        {
            ImGui::TextDisabled("linked to node #%u", Target->NodeId);
        }
        else if (Target)
        {
            ImGui::TextDisabled("resolved by name");
        }
        else
        {
            ImGui::TextDisabled("unresolved");
        }
        const bool bOpenDefinition = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (bOpenDefinition && Target)
        {
            OpenCustomLuaFunctionEditor(Blueprint, Target->NodeId);
        }
        break;
    }
    case ELuaBlueprintNodeType::CustomEvent:
    case ELuaBlueprintNodeType::CallCustomEvent:
    case ELuaBlueprintNodeType::SetTimer:
    case ELuaBlueprintNodeType::SetTimerForNextTick:
    case ELuaBlueprintNodeType::BindWidgetClick:
        ImGui::TextDisabled(Node.NameValue == FName::None ? "(no event)" : Node.NameValue.ToString().c_str());
        break;
    case ELuaBlueprintNodeType::EventInputAction:
        ImGui::TextDisabled(
            "%s / %s / %s",
            Node.NameValue == FName::None ? "(no action)" : Node.NameValue.ToString().c_str(),
            Node.StringValue.empty() ? "Pressed" : Node.StringValue.c_str(),
            GetInputKeyName(Node.IntValue).c_str()
        );
        break;
    case ELuaBlueprintNodeType::EventInputAxis:
        ImGui::TextDisabled(
            "%s / %s / %s / x%.3f",
            Node.NameValue == FName::None ? "(no axis)" : Node.NameValue.ToString().c_str(),
            Node.StringValue.empty() ? "Key" : Node.StringValue.c_str(),
            GetInputKeyName(Node.IntValue).c_str(),
            Node.FloatValue
        );
        break;
    case ELuaBlueprintNodeType::Comment:
        ImGui::TextWrapped("%s", Node.StringValue.empty() ? "Comment" : Node.StringValue.c_str());
        break;
    case ELuaBlueprintNodeType::SpawnActor:
    case ELuaBlueprintNodeType::FindActorByClass:
    case ELuaBlueprintNodeType::FindActorsByClass:
    case ELuaBlueprintNodeType::Cast:
    case ELuaBlueprintNodeType::ForEachActorByClass:
        ImGui::TextDisabled(Node.NameValue == FName::None ? "(no class)" : Node.NameValue.ToString().c_str());
        break;
    case ELuaBlueprintNodeType::ForEachActorByTag:
        ImGui::TextDisabled(Node.StringValue.empty() ? "(no tag)" : Node.StringValue.c_str());
        break;
    case ELuaBlueprintNodeType::BindEvent:
        ImGui::TextDisabled(Node.NameValue == FName::None ? "(no event)" : Node.NameValue.ToString().c_str());
        if (!Node.StringValue.empty()) ImGui::TextDisabled("→ %s", Node.StringValue.c_str());
        break;
    case ELuaBlueprintNodeType::UnbindEvent:
    case ELuaBlueprintNodeType::HasEventBinding:
        ImGui::TextDisabled(Node.NameValue == FName::None ? "(no event)" : Node.NameValue.ToString().c_str());
        break;
    default:
        break;
    }
}


void FLuaBlueprintEditorWidget::RenderNodeInputPin(
    ULuaBlueprintAsset* Blueprint,
    FLuaBlueprintNode&  Node,
    FLuaBlueprintPin&   Pin
)
{
    const ImVec4 PinCol     = PinTypeColor(Pin.Type);
    const bool   bExecPin   = Pin.Type == ELuaBlueprintPinType::Exec;
    const bool   bConnected = IsPinConnected(Blueprint, Pin);

    ed::BeginPin(ToPinId(Pin.PinId), ed::PinKind::Input);
    DrawPinIcon(bConnected, bExecPin, PinCol);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextColored(PinCol, "%s", Pin.DisplayName.ToString().c_str());
    ed::EndPin();

    if (Pin.Kind == ELuaBlueprintPinKind::Input)
    {
        ImGui::SameLine(0.0f, 6.0f);
        RenderInputPinConnectionStatus(Blueprint, Pin);
        ImGui::SameLine(0.0f, 6.0f);
        RenderInlinePinLiteral(Blueprint, Node, Pin);
    }
}

void FLuaBlueprintEditorWidget::RenderNodeOutputPin(ULuaBlueprintAsset* Blueprint, FLuaBlueprintPin& Pin)
{
    const ImVec4 PinCol     = PinTypeColor(Pin.Type);
    const bool   bExecPin   = Pin.Type == ELuaBlueprintPinType::Exec;
    const bool   bConnected = IsPinConnected(Blueprint, Pin);

    ed::BeginPin(ToPinId(Pin.PinId), ed::PinKind::Output);
    ImGui::TextColored(PinCol, "%s", Pin.DisplayName.ToString().c_str());
    ImGui::SameLine(0.0f, 6.0f);
    DrawPinIcon(bConnected, bExecPin, PinCol);
    ed::EndPin();
}

void FLuaBlueprintEditorWidget::RenderNodePins(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node)
{
    TArray<FLuaBlueprintPin*> InputPins;
    TArray<FLuaBlueprintPin*> OutputPins;
    InputPins.reserve(Node.Pins.size());
    OutputPins.reserve(Node.Pins.size());

    for (FLuaBlueprintPin& Pin : Node.Pins)
    {
        if (Pin.Kind == ELuaBlueprintPinKind::Input)
        {
            InputPins.push_back(&Pin);
        }
        else
        {
            OutputPins.push_back(&Pin);
        }
    }

    if (InputPins.empty() && OutputPins.empty())
    {
        return;
    }

    auto EstimateInlineLiteralWidth = [&](FLuaBlueprintPin& Pin) -> float
    {
        if (Pin.Kind != ELuaBlueprintPinKind::Input)
        {
            return 0.0f;
        }
        if (Blueprint->FindLinkToInput(Pin.PinId) != nullptr)
        {
            return 0.0f;
        }
        switch (Pin.Type)
        {
        case ELuaBlueprintPinType::Bool:
            return 26.0f;
        case ELuaBlueprintPinType::Int:
        case ELuaBlueprintPinType::Float:
            return 84.0f;
        case ELuaBlueprintPinType::String:
            return LuaBlueprintAssetTypeForPin(Node, Pin) ? 194.0f : 164.0f;
        case ELuaBlueprintPinType::Name:
        case ELuaBlueprintPinType::Class:
        case ELuaBlueprintPinType::Enum:
            return 164.0f;
        case ELuaBlueprintPinType::Vector:
        case ELuaBlueprintPinType::Rotator:
            return 164.0f;
        case ELuaBlueprintPinType::LinearColor:
        case ELuaBlueprintPinType::Vector4:
            return 194.0f;
        default:
            return 0.0f;
        }
    };

    auto EstimateConnectionStatusWidth = [&](FLuaBlueprintPin& Pin) -> float
    {
        if (Pin.Kind != ELuaBlueprintPinKind::Input || Pin.Type == ELuaBlueprintPinType::Exec)
        {
            return 0.0f;
        }

        const FLuaBlueprintLink* Link = Blueprint ? Blueprint->FindLinkToInput(Pin.PinId) : nullptr;
        if (!Link)
        {
            return 0.0f;
        }

        const FLuaBlueprintPin* SourcePin = Blueprint->FindPin(Link->FromPinId);
        if (!SourcePin || SourcePin->Type == ELuaBlueprintPinType::Exec)
        {
            return 0.0f;
        }

        FString Label = "linked";
        if (SourcePin->Type != Pin.Type && ULuaBlueprintAsset::ArePinTypesCompatibleForLink(SourcePin->Type, Pin.Type))
        {
            Label = FString("auto ") + PinTypeLabel(SourcePin->Type) + " -> " + PinTypeLabel(Pin.Type);
        }
        return ImGui::CalcTextSize(Label.c_str()).x;
    };

    auto EstimateInputPinWidth = [&](FLuaBlueprintPin& Pin) -> float
    {
        const FString Label = Pin.DisplayName.ToString();
        float Width = ImGui::GetTextLineHeight();
        Width += 6.0f + ImGui::CalcTextSize(Label.c_str()).x;

        const float StatusWidth = EstimateConnectionStatusWidth(Pin);
        if (StatusWidth > 0.0f)
        {
            Width += 6.0f + StatusWidth;
        }

        const float LiteralWidth = EstimateInlineLiteralWidth(Pin);
        if (LiteralWidth > 0.0f)
        {
            Width += 6.0f + LiteralWidth;
        }

        return Width;
    };

    auto EstimateOutputPinWidth = [&](FLuaBlueprintPin& Pin) -> float
    {
        const FString Label = Pin.DisplayName.ToString();
        return ImGui::CalcTextSize(Label.c_str()).x + 6.0f + ImGui::GetTextLineHeight();
    };

    float LeftColumnWidth  = 0.0f;
    float RightColumnWidth = 0.0f;
    for (FLuaBlueprintPin* Pin : InputPins)
    {
        LeftColumnWidth = (std::max)(LeftColumnWidth, EstimateInputPinWidth(*Pin));
    }
    for (FLuaBlueprintPin* Pin : OutputPins)
    {
        RightColumnWidth = (std::max)(RightColumnWidth, EstimateOutputPinWidth(*Pin));
    }

    if (!InputPins.empty()) LeftColumnWidth = (std::max)(LeftColumnWidth, 120.0f);
    if (!OutputPins.empty()) RightColumnWidth = (std::max)(RightColumnWidth, 90.0f);

    auto EstimateNodeBodyMinWidth = [&]() -> float
    {
        switch (Node.Type)
        {
        case ELuaBlueprintNodeType::LiteralString:
            return 184.0f;
        case ELuaBlueprintNodeType::LiteralVector:
            return 184.0f;
        case ELuaBlueprintNodeType::CustomLuaFunction:
            return 220.0f;
        case ELuaBlueprintNodeType::CallFunctionSignature:
            return Node.StringValue.empty() ? 0.0f : ImGui::CalcTextSize(Node.StringValue.c_str()).x;
        case ELuaBlueprintNodeType::EventInputAction:
        case ELuaBlueprintNodeType::EventInputAxis:
            return 180.0f;
        default:
            break;
        }
        return 0.0f;
    };

    const FString HeaderLabel = NodeHeaderDisplayLabel(Node);
    const float HeaderMinWidth = ImGui::GetTextLineHeight() + 6.0f
            + ImGui::CalcTextSize(HeaderLabel.c_str()).x
            + 22.0f  // help icon + local spacing
            + 32.0f; // node inner padding / style slack

    const float BaseMiddleGap = (InputPins.empty() || OutputPins.empty()) ? 12.0f : 28.0f;
    const float ContentMinWidth = (std::max)(
        (std::max)(HeaderMinWidth, EstimateNodeBodyMinWidth()),
        150.0f
    );

    float MiddleGap = BaseMiddleGap;
    float DesiredTableWidth = LeftColumnWidth + MiddleGap + RightColumnWidth;
    if (DesiredTableWidth < ContentMinWidth)
    {
        const float Extra = ContentMinWidth - DesiredTableWidth;
        if (InputPins.empty() && !OutputPins.empty())
        {
            // 출력만 있는 pure/value 노드는 출력 핀이 노드 오른쪽 끝에 붙어야 한다.
            LeftColumnWidth += Extra;
        }
        else if (!InputPins.empty() && OutputPins.empty())
        {
            // 입력만 있는 sink 노드는 입력 핀이 왼쪽 끝에 붙어야 한다.
            RightColumnWidth += Extra;
        }
        else
        {
            // 양쪽 핀이 모두 있으면 가운데 여백을 늘려 좌/우 edge anchoring 을 유지한다.
            MiddleGap += Extra;
        }
        DesiredTableWidth = LeftColumnWidth + MiddleGap + RightColumnWidth;
    }

    const int32 RowCount = (std::max)(static_cast<int32>(InputPins.size()), static_cast<int32>(OutputPins.size()));

    ImGui::PushID(static_cast<int>(Node.NodeId));
    if (ImGui::BeginTable(
            "##NodePins",
            3,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX,
            ImVec2(DesiredTableWidth, 0.0f)))
    {
        ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthFixed, LeftColumnWidth);
        ImGui::TableSetupColumn("Gap", ImGuiTableColumnFlags_WidthFixed, MiddleGap);
        ImGui::TableSetupColumn("Outputs", ImGuiTableColumnFlags_WidthFixed, RightColumnWidth);

        for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (RowIndex < static_cast<int32>(InputPins.size()))
            {
                RenderNodeInputPin(Blueprint, Node, *InputPins[RowIndex]);
            }
            else
            {
                ImGui::Dummy(ImVec2(LeftColumnWidth, 0.0f));
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::Dummy(ImVec2(MiddleGap, 0.0f));

            ImGui::TableSetColumnIndex(2);
            if (RowIndex < static_cast<int32>(OutputPins.size()))
            {
                const float CellCursorX = ImGui::GetCursorPosX();
                const float CellAvailX  = ImGui::GetContentRegionAvail().x;
                const float ContentWidth = EstimateOutputPinWidth(*OutputPins[RowIndex]);
                if (CellAvailX > ContentWidth)
                {
                    ImGui::SetCursorPosX(CellCursorX + (CellAvailX - ContentWidth));
                }
                RenderNodeOutputPin(Blueprint, *OutputPins[RowIndex]);
            }
            else
            {
                ImGui::Dummy(ImVec2(RightColumnWidth, 0.0f));
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopID();
}

void FLuaBlueprintEditorWidget::RenderNodeInspector(ULuaBlueprintAsset* Blueprint, FLuaBlueprintNode& Node)
{
    const FString HeaderLabel = NodeHeaderDisplayLabel(Node);
    ImGui::TextColored(NodeHeaderColor(Node.Type), "%s", HeaderLabel.c_str());
    ImGui::TextDisabled("Node #%u", Node.NodeId);
    ImGui::Separator();

    char DisplayBuf[128];
    CopyToBuffer(DisplayBuf, sizeof(DisplayBuf), Node.DisplayName.ToString());
    if (ImGui::InputText("Display", DisplayBuf, sizeof(DisplayBuf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        Node.DisplayName = DisplayBuf[0] ? FName(DisplayBuf) : FName(NodeTypeLabel(Node.Type));
        Blueprint->BumpVersion();
        CommitBlueprintEdit(Blueprint);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Runtime Debug");
    const bool bPausedHere = IsPausedOnLuaBlueprintNode(Blueprint, Node);
    if (bPausedHere)
    {
        const FLuaDebugValueSnapshot Snapshot = FLuaDebugManager::GetPausedSnapshot();
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.22f, 1.0f), "Paused at line %d", Snapshot.Location.Line);
        RenderLuaDebugValueList("Node Values", Snapshot.NodeValues, 64);
        RenderLuaDebugValueList("Blueprint Variables", Snapshot.BlueprintVariables, 64);
        RenderLuaDebugValueList("Watch", Snapshot.Watches, 32);
        RenderLuaDebugValueList("Locals", Snapshot.Locals, 64);
        RenderLuaDebugValueList("Upvalues", Snapshot.Upvalues, 32);
    }
    else if (FLuaDebugManager::IsPaused())
    {
        const FLuaDebugLocation& Loc = FLuaDebugManager::GetPausedLocation();
        ImGui::TextDisabled("Paused at %s / Node #%u", Loc.NodeName.c_str(), Loc.NodeId);
    }
    else
    {
        ImGui::TextDisabled("Run PIE and break on this node to inspect locals, Blueprint variables, and watches.");
    }

    ImGui::PushItemWidth(-72.0f);
    ImGui::InputTextWithHint("##WatchExpression", "watch expression, e.g. Health or __vars.Health", DebugWatchExpressionBuf, sizeof(DebugWatchExpressionBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    const bool bSubmitWatch = ImGui::IsItemDeactivatedAfterEdit() && DebugWatchExpressionBuf[0] != 0;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add##WatchExpression") || bSubmitWatch)
    {
        if (DebugWatchExpressionBuf[0] != 0)
        {
            FLuaDebugManager::AddWatchExpression(DebugWatchExpressionBuf);
            DebugWatchExpressionBuf[0] = 0;
        }
    }

    TArray<FString> WatchExpressions = FLuaDebugManager::GetWatchExpressions();
    for (int32 WatchIndex = 0; WatchIndex < static_cast<int32>(WatchExpressions.size()); ++WatchIndex)
    {
        ImGui::PushID(WatchIndex);
        ImGui::TextDisabled("Watch");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", WatchExpressions[WatchIndex].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            FLuaDebugManager::RemoveWatchExpression(WatchIndex);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    ImGui::Separator();

    switch (Node.Type)
    {
    case ELuaBlueprintNodeType::GetVariable:
    case ELuaBlueprintNodeType::SetVariable:
    {
        FString     CurrentName = Node.NameValue.ToString();
        const char* Preview     = CurrentName.empty() ? "(none)" : CurrentName.c_str();
        if (ImGui::BeginCombo("Variable", Preview))
        {
            for (const FLuaBlueprintVariable& Variable : Blueprint->GetVariables())
            {
                const FString VarName   = Variable.Name.ToString();
                const bool    bSelected = (VarName == CurrentName);
                if (ImGui::Selectable(VarName.c_str(), bSelected))
                {
                    Node.NameValue = Variable.Name;
                    Blueprint->RefreshNodePinTypes(Node);
                    Blueprint->BumpVersion();
                    CommitBlueprintEdit(Blueprint);
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        char NameBuf[128];
        CopyToBuffer(NameBuf, sizeof(NameBuf), Node.NameValue.ToString());
        if (ImGui::InputText("Variable Name", NameBuf, sizeof(NameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.NameValue = NameBuf[0] ? FName(NameBuf) : FName::None;
            Blueprint->RefreshNodePinTypes(Node);
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    case ELuaBlueprintNodeType::GetProperty:
    case ELuaBlueprintNodeType::SetProperty:
    case ELuaBlueprintNodeType::CallFunction:
    {
        char NameBuf[160];
        CopyToBuffer(NameBuf, sizeof(NameBuf), Node.NameValue.ToString());
        if (ImGui::InputText(
            Node.Type == ELuaBlueprintNodeType::CallFunction ? "Function" : "Property",
            NameBuf,
            sizeof(NameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue
        ))
        {
            Node.NameValue = NameBuf[0] ? FName(NameBuf) : FName::None;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    case ELuaBlueprintNodeType::CallFunctionSignature:
    {
        char SigBuf[256];
        CopyToBuffer(SigBuf, sizeof(SigBuf), Node.StringValue.empty() ? Node.NameValue.ToString() : Node.StringValue);
        if (ImGui::InputText("Signature", SigBuf, sizeof(SigBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.StringValue = SigBuf;
            Node.NameValue   = SigBuf[0] ? FName(SigBuf) : FName::None;
            Blueprint->RefreshNodePinTypes(Node);
            Blueprint->RemoveInvalidLinks();
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }

        const FString Preview = Node.StringValue.empty() ? FString("(select reflected function)") : Node.StringValue;
        if (ImGui::BeginCombo("Reflected Function", Preview.c_str()))
        {
            TArray<UClass*> Classes = UClass::GetAllClasses();
            std::sort(
                Classes.begin(),
                Classes.end(),
                [](const UClass* A, const UClass* B)
                {
                    const char* AN = A ? A->GetName() : "";
                    const char* BN = B ? B->GetName() : "";
                    return std::strcmp(AN, BN) < 0;
                }
            );

            for (UClass* Class : Classes)
            {
                if (!Class) continue;
                TArray<const FFunction*> Functions;
                Class->GetFunctionRefs(Functions);
                if (Functions.empty()) continue;
                if (!ImGui::BeginMenu(Class->GetName())) continue;
                for (const FFunction* Function : Functions)
                {
                    if (!Function) continue;
                    const FString Signature = Function->GetSignature();
                    const FString Label     = FString(Function->GetDisplayName()) + "  [" + Signature + "]";
                    const bool    bSelected = (Node.StringValue == Signature);
                    if (ImGui::Selectable(Label.c_str(), bSelected))
                    {
                        Node.StringValue = Signature;
                        Node.NameValue   = FName(Function->GetName());
                        Blueprint->RefreshNodePinTypes(Node);
                        Blueprint->RemoveInvalidLinks();
                        Blueprint->BumpVersion();
                        CommitBlueprintEdit(Blueprint);
                    }
                    if (bSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndMenu();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("선택 시 reflection parameter 기준으로 입력/출력 핀이 자동 재구성됩니다.");
        break;
    }
    case ELuaBlueprintNodeType::CustomLuaFunction:
    {
        char FunctionBuf[160];
        CopyToBuffer(FunctionBuf, sizeof(FunctionBuf), Node.NameValue.ToString());
        if (ImGui::InputText("Function Name", FunctionBuf, sizeof(FunctionBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const FName OldName = Node.NameValue;
            const FName NewName = FunctionBuf[0] ? FName(FunctionBuf) : FName::None;
            Node.NameValue = NewName;
            RenameCustomLuaFunctionCascade(Blueprint, OldName, NewName);
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        if (ImGui::Button("Edit Code"))
        {
            OpenCustomLuaFunctionEditor(Blueprint, Node.NodeId);
        }
        ImGui::TextDisabled("Arguments: Arg0, Arg1, Arg2, Arg3. Use return <value> for the Return output.");
        break;
    }
    case ELuaBlueprintNodeType::CallCustomLuaFunction:
    {
        const FLuaBlueprintNode* CurrentTarget = ResolveCustomLuaFunctionCallTarget(Blueprint, Node);
        const FString CurrentName = CurrentTarget ? CurrentTarget->NameValue.ToString() : Node.NameValue.ToString();
        const char* Preview = CurrentName.empty() ? "(none)" : CurrentName.c_str();
        if (ImGui::BeginCombo("Custom Lua Function Node", Preview))
        {
            for (const FLuaBlueprintNode& Other : Blueprint->GetNodes())
            {
                if (Other.Type != ELuaBlueprintNodeType::CustomLuaFunction) continue;
                const FString FunctionName = Other.NameValue.ToString();
                const FString Label = FunctionName.empty()
                    ? FString("<unnamed>  #") + std::to_string(Other.NodeId)
                    : FunctionName + FString("  #") + std::to_string(Other.NodeId);
                const bool bSelected = (Node.IntValue == static_cast<int32>(Other.NodeId));
                if (ImGui::Selectable(Label.c_str(), bSelected))
                {
                    SetCallCustomLuaFunctionTarget(Blueprint, Node, &Other);
                    Blueprint->BumpVersion();
                    CommitBlueprintEdit(Blueprint);
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Node link is stored by stable NodeId. The name below remains the readable/fallback call target.");

        char FunctionBuf[160];
        CopyToBuffer(FunctionBuf, sizeof(FunctionBuf), Node.NameValue.ToString());
        if (ImGui::InputText("Function Name Fallback", FunctionBuf, sizeof(FunctionBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.NameValue = FunctionBuf[0] ? FName(FunctionBuf) : FName::None;
            Node.IntValue = 0;
            SyncCallCustomLuaFunctionNamePin(Node);
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        if (ImGui::Button("Open Definition"))
        {
            if (const FLuaBlueprintNode* Target = ResolveCustomLuaFunctionCallTarget(Blueprint, Node))
            {
                OpenCustomLuaFunctionEditor(Blueprint, Target->NodeId);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Node Link"))
        {
            Node.IntValue = 0;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    case ELuaBlueprintNodeType::CustomEvent:
    case ELuaBlueprintNodeType::CallCustomEvent:
    case ELuaBlueprintNodeType::SetTimer:
    case ELuaBlueprintNodeType::SetTimerForNextTick:
    case ELuaBlueprintNodeType::BindWidgetClick:
    {
        char EventBuf[160];
        CopyToBuffer(EventBuf, sizeof(EventBuf), Node.NameValue.ToString());
        if (ImGui::InputText("Event Name", EventBuf, sizeof(EventBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.NameValue = EventBuf[0] ? FName(EventBuf) : FName::None;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    case ELuaBlueprintNodeType::BindEvent:
    case ELuaBlueprintNodeType::UnbindEvent:
    case ELuaBlueprintNodeType::HasEventBinding:
    {
        // target 의 함수/시그니처 이름 — Reflection.BindEvent 2번째 인자.
        char EventBuf[160];
        CopyToBuffer(EventBuf, sizeof(EventBuf), Node.NameValue.ToString());
        if (ImGui::InputText("Target Event", EventBuf, sizeof(EventBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.NameValue = EventBuf[0] ? FName(EventBuf) : FName::None;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        if (Node.Type == ELuaBlueprintNodeType::BindEvent)
        {
            // 우리 그래프의 CustomEvent 이름 — 콜백으로 매핑됨. dropdown + 자유 입력.
            char CallbackBuf[160];
            CopyToBuffer(CallbackBuf, sizeof(CallbackBuf), Node.StringValue);
            const FString Preview = Node.StringValue.empty() ? FString("(none)") : Node.StringValue;
            if (ImGui::BeginCombo("Callback", Preview.c_str()))
            {
                for (const FLuaBlueprintNode& Other : Blueprint->GetNodes())
                {
                    if (Other.Type != ELuaBlueprintNodeType::CustomEvent) continue;
                    const FString EvName = Other.NameValue.ToString();
                    if (EvName.empty()) continue;
                    const bool bSel = (Node.StringValue == EvName);
                    if (ImGui::Selectable(EvName.c_str(), bSel))
                    {
                        Node.StringValue = EvName;
                        Blueprint->BumpVersion();
                        CommitBlueprintEdit(Blueprint);
                    }
                    if (bSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::InputText("Callback Name", CallbackBuf, sizeof(CallbackBuf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                Node.StringValue = CallbackBuf;
                Blueprint->BumpVersion();
                CommitBlueprintEdit(Blueprint);
            }
            ImGui::TextDisabled("(목록의 Custom Event 이름이나 직접 입력)");
        }
        break;
    }
    case ELuaBlueprintNodeType::EventInputAction:
    {
        char NameBuf[128];
        CopyToBuffer(NameBuf, sizeof(NameBuf), Node.NameValue.ToString());
        if (ImGui::InputText("Action Name", NameBuf, sizeof(NameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.NameValue = NameBuf[0] ? FName(NameBuf) : FName::None;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }

        const char* CurrentEvent = Node.StringValue == "Released" ? "Released" : "Pressed";
        if (ImGui::BeginCombo("Event", CurrentEvent))
        {
            const char* EventNames[] = { "Pressed", "Released" };
            for (const char* EventName : EventNames)
            {
                const bool bSelected = std::strcmp(CurrentEvent, EventName) == 0;
                if (ImGui::Selectable(EventName, bSelected))
                {
                    Node.StringValue = EventName;
                    Blueprint->BumpVersion();
                    CommitBlueprintEdit(Blueprint);
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        const FString CurrentKey = GetInputKeyName(Node.IntValue);
        if (ImGui::BeginCombo("Key", CurrentKey.c_str()))
        {
            const bool bNoneSelected = Node.IntValue == 0;
            if (ImGui::Selectable("None / existing mapping only", bNoneSelected))
            {
                Node.IntValue = 0;
                Blueprint->BumpVersion();
                CommitBlueprintEdit(Blueprint);
            }
            if (bNoneSelected) ImGui::SetItemDefaultFocus();

            for (const FString& KeyName : GetKnownInputKeyNames())
            {
                const int32 KeyCode   = ResolveInputKeyCode(KeyName);
                const bool  bSelected = KeyCode == Node.IntValue;
                if (ImGui::Selectable(KeyName.c_str(), bSelected))
                {
                    Node.IntValue = KeyCode;
                    Blueprint->BumpVersion();
                    CommitBlueprintEdit(Blueprint);
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        char KeyBuf[96];
        CopyToBuffer(KeyBuf, sizeof(KeyBuf), CurrentKey);
        if (ImGui::InputTextWithHint("Key Name", "Space, W, LeftMouseButton...", KeyBuf, sizeof(KeyBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.IntValue = ResolveInputKeyCode(KeyBuf);
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::TextDisabled("None이면 같은 이름의 기존 ActionMapping에만 바인딩합니다.");
        break;
    }
    case ELuaBlueprintNodeType::EventInputAxis:
    {
        char NameBuf[128];
        CopyToBuffer(NameBuf, sizeof(NameBuf), Node.NameValue.ToString());
        if (ImGui::InputText("Axis Name", NameBuf, sizeof(NameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.NameValue = NameBuf[0] ? FName(NameBuf) : FName::None;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }

        const char* Sources[]     = { "Key", "MouseX", "MouseY", "MouseWheel" };
        FString     CurrentSource = Node.StringValue.empty() ? "Key" : Node.StringValue;
        if (ImGui::BeginCombo("Source", CurrentSource.c_str()))
        {
            for (const char* SourceName : Sources)
            {
                const bool bSelected = CurrentSource == SourceName;
                if (ImGui::Selectable(SourceName, bSelected))
                {
                    Node.StringValue = SourceName;
                    Blueprint->BumpVersion();
                    CommitBlueprintEdit(Blueprint);
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (CurrentSource == "Key")
        {
            const FString CurrentKey = GetInputKeyName(Node.IntValue);
            if (ImGui::BeginCombo("Key", CurrentKey.c_str()))
            {
                const bool bNoneSelected = Node.IntValue == 0;
                if (ImGui::Selectable("None / existing mapping only", bNoneSelected))
                {
                    Node.IntValue = 0;
                    Blueprint->BumpVersion();
                    CommitBlueprintEdit(Blueprint);
                }
                if (bNoneSelected) ImGui::SetItemDefaultFocus();

                for (const FString& KeyName : GetKnownInputKeyNames())
                {
                    const int32 KeyCode   = ResolveInputKeyCode(KeyName);
                    const bool  bSelected = KeyCode == Node.IntValue;
                    if (ImGui::Selectable(KeyName.c_str(), bSelected))
                    {
                        Node.IntValue = KeyCode;
                        Blueprint->BumpVersion();
                        CommitBlueprintEdit(Blueprint);
                    }
                    if (bSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            char KeyBuf[96];
            CopyToBuffer(KeyBuf, sizeof(KeyBuf), CurrentKey);
            if (ImGui::InputTextWithHint("Key Name", "W, S, Space, LeftMouseButton...", KeyBuf, sizeof(KeyBuf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                Node.IntValue = ResolveInputKeyCode(KeyBuf);
                Blueprint->BumpVersion();
                CommitBlueprintEdit(Blueprint);
            }
            ImGui::TextDisabled("None이면 같은 이름의 기존 AxisMapping에만 바인딩합니다.");
        }
        if (ImGui::DragFloat("Scale", &Node.FloatValue, 0.01f, -100.0f, 100.0f, "%.3f"))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::TextDisabled("Mouse source는 Key 값을 사용하지 않습니다.");
        break;
    }
    case ELuaBlueprintNodeType::Comment:
    {
        char TextBuf[512];
        CopyToBuffer(TextBuf, sizeof(TextBuf), Node.StringValue);
        if (ImGui::InputTextMultiline("Comment", TextBuf, sizeof(TextBuf), ImVec2(-1, 100)))
        {
            Node.StringValue = TextBuf;
            Blueprint->BumpEditorVersion();
            CommitBlueprintEdit(Blueprint);
        }
        float Size[2] = { Node.VectorValue.X, Node.VectorValue.Y };
        if (ImGui::DragFloat2("Group Size", Size, 1.0f, 80.0f, 4000.0f, "%.0f"))
        {
            Node.VectorValue = FVector(Size[0], Size[1], Node.VectorValue.Z);
            Blueprint->BumpEditorVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::TextDisabled("그룹 박스를 끌면 안에 있는 노드도 같이 이동합니다.");
        break;
    }
    case ELuaBlueprintNodeType::PrintString:
    case ELuaBlueprintNodeType::LiteralString:
    {
        char TextBuf[512];
        CopyToBuffer(TextBuf, sizeof(TextBuf), Node.StringValue);
        if (ImGui::InputTextMultiline("Text", TextBuf, sizeof(TextBuf), ImVec2(-1, 90)))
        {
            Node.StringValue = TextBuf;
            // PrintString 의 경우 Text 입력 핀의 DefaultString 도 동기화 — 핀 inline editor 와의 일관성.
            if (Node.Type == ELuaBlueprintNodeType::PrintString)
            {
                for (FLuaBlueprintPin& Pin : Node.Pins)
                {
                    if (Pin.Kind == ELuaBlueprintPinKind::Input && Pin.Type == ELuaBlueprintPinType::String && Pin.
                                                                                                               DisplayName.ToString() == "Text")
                    {
                        Pin.DefaultString = Node.StringValue;
                        break;
                    }
                }
            }
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    case ELuaBlueprintNodeType::LiteralBool:
        if (ImGui::Checkbox("Value", &Node.BoolValue))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    case ELuaBlueprintNodeType::LiteralInt:
        if (ImGui::InputInt("Value", &Node.IntValue))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    case ELuaBlueprintNodeType::LiteralFloat:
        if (ImGui::InputFloat("Value", &Node.FloatValue))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    case ELuaBlueprintNodeType::LiteralVector:
    {
        float V[3] = { Node.VectorValue.X, Node.VectorValue.Y, Node.VectorValue.Z };
        if (ImGui::InputFloat3("Value", V))
        {
            Node.VectorValue = FVector(V[0], V[1], V[2]);
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    // 클래스 이름이 컴파일 타임에 묶이는 노드들 — 드롭다운 + 직접 입력.
    case ELuaBlueprintNodeType::SpawnActor:
    case ELuaBlueprintNodeType::FindActorByClass:
    case ELuaBlueprintNodeType::FindActorsByClass:
    case ELuaBlueprintNodeType::Cast:
    case ELuaBlueprintNodeType::ForEachActorByClass:
    {
        FString     Current = Node.NameValue.ToString();
        const char* Preview = Current.empty() ? "(none)" : Current.c_str();
        if (ImGui::BeginCombo("Class", Preview))
        {
            for (UClass* C : UClass::GetAllClasses())
            {
                if (!C) continue;
                const bool bSelected = (Current == C->GetName());
                if (ImGui::Selectable(C->GetName(), bSelected))
                {
                    Node.NameValue = FName(C->GetName());
                    Blueprint->BumpVersion();
                    CommitBlueprintEdit(Blueprint);
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        char Buf[160];
        CopyToBuffer(Buf, sizeof(Buf), Node.NameValue.ToString());
        if (ImGui::InputText("Class Name", Buf, sizeof(Buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.NameValue = Buf[0] ? FName(Buf) : FName::None;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    // Tag 가 컴파일 타임에 묶이는 노드 (ForEachActorByTag). FindActorByTag 류는 input pin 으로도 가능.
    case ELuaBlueprintNodeType::ForEachActorByTag:
    {
        char Buf[128];
        CopyToBuffer(Buf, sizeof(Buf), Node.StringValue);
        if (ImGui::InputText("Tag", Buf, sizeof(Buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            Node.StringValue = Buf;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    default:
        break;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Pins");
    for (const FLuaBlueprintPin& Pin : Node.Pins)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, PinTypeColor(Pin.Type));
        ImGui::BulletText(
            "%s %s %s #%u",
            Pin.Kind == ELuaBlueprintPinKind::Input ? "In" : "Out",
            PinTypeLabel(Pin.Type),
            Pin.DisplayName.ToString().c_str(),
            Pin.PinId
        );
        ImGui::PopStyleColor();
    }
}

void FLuaBlueprintEditorWidget::RenderVariableEditor(
    ULuaBlueprintAsset*    Blueprint,
    FLuaBlueprintVariable& Variable,
    int32                  Index
)
{
    char NameBuf[128];
    CopyToBuffer(NameBuf, sizeof(NameBuf), Variable.Name.ToString());
    if (ImGui::InputText("Name", NameBuf, sizeof(NameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const FName OldName = Variable.Name;
        const FName NewName = MakeUniqueVariableNameForEditor(Blueprint, NameBuf[0] ? FName(NameBuf) : FName::None, Index);
        Variable.Name       = NewName;
        CopyToBuffer(NameBuf, sizeof(NameBuf), Variable.Name.ToString());
        // 이 변수를 참조하는 Get/Set 노드의 NameValue 도 함께 갱신 — AnimGraph state rename 패턴.
        RenameVariableCascade(Blueprint, OldName, NewName);
        Blueprint->RefreshAllNodePinTypes();
        Blueprint->RemoveInvalidLinks();
        Blueprint->BumpVersion();
        CommitBlueprintEdit(Blueprint);
    }

    Variable.Type            = NormalizeVariablePinTypeForEditor(Variable.Type);
    int         TypeIndex    = VariablePinTypeToComboIndex(Variable.Type);
    const char* TypeLabels[] = { "Bool", "Int", "Float", "String", "Vector", "Object", "Array", "Actor", "Pawn", "PlayerController", "ActorComponent", "SceneComponent", "PrimitiveComponent", "Rotator", "LinearColor", "Vector4", "Class", "Enum", "Name", "StaticMesh", "StaticMeshComponent", "SkinnedMeshComponent", "SkeletalMeshComponent", "CameraComponent", "CineCameraComponent", "Material", "Texture", "AnimInstance", "LuaBlueprintComponent", "LuaScriptComponent" };
    if (ImGui::Combo("Type", &TypeIndex, TypeLabels, IM_ARRAYSIZE(TypeLabels)))
    {
        Variable.Type = ComboIndexToVariablePinType(TypeIndex);
        Blueprint->RefreshAllNodePinTypes();
        Blueprint->RemoveInvalidLinks();
        Blueprint->BumpVersion();
        CommitBlueprintEdit(Blueprint);
    }

    switch (Variable.Type)
    {
    case ELuaBlueprintPinType::Bool:
        if (ImGui::Checkbox("Default", &Variable.BoolValue))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    case ELuaBlueprintPinType::Int:
        if (ImGui::InputInt("Default", &Variable.IntValue))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    case ELuaBlueprintPinType::Float:
        if (ImGui::InputFloat("Default", &Variable.FloatValue))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    case ELuaBlueprintPinType::String:
    case ELuaBlueprintPinType::Name:
    case ELuaBlueprintPinType::Class:
    case ELuaBlueprintPinType::Enum:
    {
        char ValueBuf[512];
        CopyToBuffer(ValueBuf, sizeof(ValueBuf), Variable.StringValue);
        if (ImGui::InputText("Default", ValueBuf, sizeof(ValueBuf)))
        {
            Variable.StringValue = ValueBuf;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    case ELuaBlueprintPinType::Vector:
    case ELuaBlueprintPinType::Rotator:
    {
        float V[3] = { Variable.VectorValue.X, Variable.VectorValue.Y, Variable.VectorValue.Z };
        if (ImGui::InputFloat3("Default", V))
        {
            Variable.VectorValue = FVector(V[0], V[1], V[2]);
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    case ELuaBlueprintPinType::LinearColor:
    case ELuaBlueprintPinType::Vector4:
    {
        float V[4] = { Variable.VectorValue.X, Variable.VectorValue.Y, Variable.VectorValue.Z, Variable.FloatValue };
        if (ImGui::InputFloat4("Default", V))
        {
            Variable.VectorValue = FVector(V[0], V[1], V[2]);
            Variable.FloatValue  = V[3];
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    }
    case ELuaBlueprintPinType::Object:
        if (ImGui::Checkbox("Strong Object Ref", &Variable.bStrongObject))
        {
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        break;
    default:
        break;
    }

    // Get/Set 빠른 추가 버튼 — 실제 BP 의 ctrl/alt+drag 대안. PendingNewNodePosition 가 비어있으면
    // 캔버스 우상단 근처 기본 위치로.
    const ImVec2 SpawnPos = (PendingNewNodePosition.x != 0.0f || PendingNewNodePosition.y != 0.0f)
            ? PendingNewNodePosition : ImVec2(100.0f + Index * 30.0f, 100.0f + Index * 30.0f);

    if (ImGui::SmallButton("Get"))
    {
        SpawnVariableNode(Blueprint, ELuaBlueprintNodeType::GetVariable, Variable.Name, SpawnPos);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Set"))
    {
        SpawnVariableNode(Blueprint, ELuaBlueprintNodeType::SetVariable, Variable.Name, SpawnPos);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove"))
    {
        TArray<FLuaBlueprintVariable>& Vars = Blueprint->GetMutableVariables();
        if (Index >= 0 && Index < static_cast<int32>(Vars.size()))
        {
            const FName RemovedName = Vars[Index].Name;
            Vars.erase(Vars.begin() + Index);
            RemoveVariableCascade(Blueprint, RemovedName);
            Blueprint->RefreshAllNodePinTypes();
            Blueprint->RemoveInvalidLinks();
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
    }
}

void FLuaBlueprintEditorWidget::RenderDiagnostics(ULuaBlueprintAsset* Blueprint)
{
    if (Blueprint->GetDiagnostics().empty())
    {
        ImGui::TextDisabled("No diagnostics.");
        return;
    }

    for (const FLuaBlueprintDiagnostic& Diagnostic : Blueprint->GetDiagnostics())
    {
        ImVec4 Color(0.8f, 0.8f, 0.8f, 1.0f);
        switch (Diagnostic.Severity)
        {
        case ELuaBlueprintDiagnosticSeverity::Error:
            Color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
            break;
        case ELuaBlueprintDiagnosticSeverity::Warning:
            Color = ImVec4(0.95f, 0.85f, 0.35f, 1.0f);
            break;
        default:
            break;
        }
        const FString Line = FString("[") + SeverityLabel(Diagnostic.Severity) + "] Node " + std::to_string(Diagnostic.NodeId) + ": " + Diagnostic.Message;
        ImGui::PushID(static_cast<int>(Diagnostic.NodeId));
        ImGui::PushStyleColor(ImGuiCol_Text, Color);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", Line.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked())
        {
            QueueNavigateToNode(Diagnostic.NodeId);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Click to focus the node.");
        }
        ImGui::PopID();
    }
}

void FLuaBlueprintEditorWidget::SyncBreakpointsToDebugManager(ULuaBlueprintAsset* Blueprint)
{
    if (!Blueprint || bDebugBreakpointsSynced)
    {
        return;
    }

    for (const FLuaBlueprintNode& Node : Blueprint->GetNodes())
    {
        if (Node.bBreakpointEnabled)
        {
            FLuaDebugManager::SetNodeBreakpoint(Blueprint->GetSourcePath(), Node.NodeId, true);
        }
    }
    bDebugBreakpointsSynced = true;
}

void FLuaBlueprintEditorWidget::ProcessLuaDebugAutoFocus(ULuaBlueprintAsset* Blueprint)
{
    if (!Blueprint || !FLuaDebugManager::IsPaused())
    {
        return;
    }

    FLuaDebugEditorFocusRequest Request;
    if (!FLuaDebugManager::PeekEditorFocusRequest(Request))
    {
        return;
    }

    if (Request.Serial == LastHandledLuaDebugFocusSerial)
    {
        return;
    }

    const FString CurrentPath = NormalizeLuaBlueprintDebugPath(Blueprint->GetSourcePath());
    const FString RequestPath = NormalizeLuaBlueprintDebugPath(Request.Location.BlueprintPath);
    if (CurrentPath.empty() || CurrentPath != RequestPath)
    {
        return;
    }

    LastHandledLuaDebugFocusSerial = Request.Serial;
    QueueNavigateToNode(Request.Location.NodeId);
    RequestFocus();
}

void FLuaBlueprintEditorWidget::QueueNavigateToNode(uint32 NodeId)
{
    if (NodeId == 0)
    {
        return;
    }
    bPendingNavigateToNode = true;
    PendingNavigateToNodeId = NodeId;
}


void FLuaBlueprintEditorWidget::RenderGeneratedLua(ULuaBlueprintAsset* Blueprint)
{
    const FString& Source = Blueprint->GetGeneratedLuaSource();
    if (Source.empty())
    {
        ImGui::TextDisabled("No generated source. Compile the blueprint first.");
        return;
    }

    ImGui::InputTextMultiline(
        "##GeneratedLua",
        const_cast<char*>(Source.c_str()),
        Source.size() + 1,
        ImVec2(-1.0f, -1.0f),
        ImGuiInputTextFlags_ReadOnly
    );
}


void FLuaBlueprintEditorWidget::OpenCustomLuaFunctionEditor(ULuaBlueprintAsset* Blueprint, uint32 NodeId)
{
    if (!Blueprint) return;
    FLuaBlueprintNode* Node = Blueprint->FindNode(NodeId);
    if (!Node || Node->Type != ELuaBlueprintNodeType::CustomLuaFunction) return;

    CustomLuaFunctionEditorNodeId = NodeId;
    CopyToBuffer(CustomLuaFunctionNameBuf, sizeof(CustomLuaFunctionNameBuf), Node->NameValue.ToString());
    CopyToBuffer(CustomLuaFunctionCodeBuf, sizeof(CustomLuaFunctionCodeBuf), Node->StringValue);
    bCustomLuaFunctionEditorOpen = true;
    bCustomLuaFunctionEditorRequestFocus = true;
    ImGui::OpenPopup("Custom Lua Function Editor");
}

void FLuaBlueprintEditorWidget::RenderCustomLuaFunctionEditor(ULuaBlueprintAsset* Blueprint)
{
    if (!bCustomLuaFunctionEditorOpen || !Blueprint)
    {
        return;
    }

    if (bCustomLuaFunctionEditorRequestFocus)
    {
        ImGui::OpenPopup("Custom Lua Function Editor");
        ImGui::SetNextWindowSize(ImVec2(820.0f, 620.0f), ImGuiCond_Always);
        bCustomLuaFunctionEditorRequestFocus = false;
    }

    bool bOpen = bCustomLuaFunctionEditorOpen;
    if (ImGui::BeginPopupModal("Custom Lua Function Editor", &bOpen, ImGuiWindowFlags_NoSavedSettings))
    {
        FLuaBlueprintNode* Node = Blueprint->FindNode(CustomLuaFunctionEditorNodeId);
        if (!Node || Node->Type != ELuaBlueprintNodeType::CustomLuaFunction)
        {
            ImGui::TextDisabled("The edited function node no longer exists.");
            if (ImGui::Button("Close"))
            {
                bCustomLuaFunctionEditorOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        ImGui::TextDisabled("Arguments available in the function body: Arg0, Arg1, Arg2, Arg3");
        ImGui::TextDisabled("Use return <value> to feed the Return pin of Call Custom Lua Function.");
        ImGui::Separator();

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("Function Name", CustomLuaFunctionNameBuf, sizeof(CustomLuaFunctionNameBuf));

        ImGui::TextUnformatted("Lua Body");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline(
            "##CustomLuaFunctionBody",
            CustomLuaFunctionCodeBuf,
            sizeof(CustomLuaFunctionCodeBuf),
            ImVec2(-1.0f, -88.0f),
            ImGuiInputTextFlags_AllowTabInput
        );

        ImGui::Separator();
        if (ImGui::Button("Apply"))
        {
            const FName OldName = Node->NameValue;
            const FName NewName = CustomLuaFunctionNameBuf[0] ? FName(CustomLuaFunctionNameBuf) : FName::None;
            Node->NameValue = NewName;
            RenameCustomLuaFunctionCascade(Blueprint, OldName, NewName);
            Node->StringValue = CustomLuaFunctionCodeBuf;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply && Close"))
        {
            const FName OldName = Node->NameValue;
            const FName NewName = CustomLuaFunctionNameBuf[0] ? FName(CustomLuaFunctionNameBuf) : FName::None;
            Node->NameValue = NewName;
            RenameCustomLuaFunctionCascade(Blueprint, OldName, NewName);
            Node->StringValue = CustomLuaFunctionCodeBuf;
            Blueprint->BumpVersion();
            CommitBlueprintEdit(Blueprint);
            bCustomLuaFunctionEditorOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Compile"))
        {
            const FName OldName = Node->NameValue;
            const FName NewName = CustomLuaFunctionNameBuf[0] ? FName(CustomLuaFunctionNameBuf) : FName::None;
            Node->NameValue = NewName;
            RenameCustomLuaFunctionCascade(Blueprint, OldName, NewName);
            Node->StringValue = CustomLuaFunctionCodeBuf;
            Blueprint->BumpVersion();
            Blueprint->Compile();
            CommitBlueprintEdit(Blueprint);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            bCustomLuaFunctionEditorOpen = false;
            ImGui::CloseCurrentPopup();
        }

        if (!bOpen)
        {
            bCustomLuaFunctionEditorOpen = false;
        }
        ImGui::EndPopup();
    }
    else if (!bOpen)
    {
        bCustomLuaFunctionEditorOpen = false;
    }
}

void FLuaBlueprintEditorWidget::OpenInlineAssetPicker(
    const FLuaBlueprintNode& Node,
    const FLuaBlueprintPin&  Pin,
    const char*              AssetType
)
{
    if (!AssetType || !AssetType[0])
    {
        return;
    }

    InlineAssetPickerNodeId = Node.NodeId;
    InlineAssetPickerPinId  = Pin.PinId;
    InlineAssetPickerType   = AssetType;
    InlineAssetPickerPreviewPath = Pin.DefaultString;

    // NodeEditor 내부에서는 ImGui popup/combo 를 직접 열지 않는다.
    // 클릭된 버튼의 실제 screen-space rect 를 캡쳐하고, ed::End() 이후의 안정적인
    // overlay 레이어에서 이 rect 바로 아래에 asset picker window 를 띄운다.
    const ImVec2 ItemMin = ImGui::GetItemRectMin();
    const ImVec2 ItemMax = ImGui::GetItemRectMax();
    InlineAssetPickerAnchorPos = ImVec2(ItemMin.x, ItemMax.y + 4.0f);

    // 일부 NodeEditor zoom/transform 상태에서 item rect 가 비정상적으로 들어오는 경우를
    // 대비해 mouse position fallback 도 같이 보관한다.
    InlineAssetPickerFallbackPos = ImGui::GetMousePos();
    InlineAssetPickerFallbackPos.x += 12.0f;
    InlineAssetPickerFallbackPos.y += 12.0f;

    InlineAssetPickerSearchBuf[0] = 0;
    bOpenInlineAssetPicker = true;
}

bool FLuaBlueprintEditorWidget::ApplyInlineAssetPickerSelection(
    ULuaBlueprintAsset* Blueprint,
    const FString&      SelectedPath
)
{
    if (!Blueprint || InlineAssetPickerNodeId == 0 || InlineAssetPickerPinId == 0)
    {
        return false;
    }

    FLuaBlueprintNode* Node = Blueprint->FindNode(InlineAssetPickerNodeId);
    if (!Node)
    {
        return false;
    }

    FLuaBlueprintPin* Pin = nullptr;
    for (FLuaBlueprintPin& Candidate : Node->Pins)
    {
        if (Candidate.PinId == InlineAssetPickerPinId)
        {
            Pin = &Candidate;
            break;
        }
    }
    if (!Pin || Pin->Kind != ELuaBlueprintPinKind::Input || Pin->Type != ELuaBlueprintPinType::String)
    {
        return false;
    }

    if (Pin->DefaultString == SelectedPath)
    {
        return false;
    }

    Pin->DefaultString = SelectedPath;
    Blueprint->BumpVersion();
    CommitBlueprintEdit(Blueprint);
    return true;
}

void FLuaBlueprintEditorWidget::RenderDeferredInlineAssetPicker(ULuaBlueprintAsset* Blueprint)
{
    bool bJustOpenedThisFrame = false;
    if (bOpenInlineAssetPicker)
    {
        bOpenInlineAssetPicker = false;
        bInlineAssetPickerVisible = true;
        bJustOpenedThisFrame = true;
    }

    if (!bInlineAssetPickerVisible)
    {
        return;
    }

    ImVec2 PickerPos = InlineAssetPickerAnchorPos;
    const bool bAnchorLooksInvalid =
        !std::isfinite(PickerPos.x) || !std::isfinite(PickerPos.y)
        || (InlineAssetPickerOwnerMax.x > InlineAssetPickerOwnerMin.x
            && (PickerPos.x < InlineAssetPickerOwnerMin.x - 256.0f || PickerPos.x > InlineAssetPickerOwnerMax.x + 256.0f))
        || (InlineAssetPickerOwnerMax.y > InlineAssetPickerOwnerMin.y
            && (PickerPos.y < InlineAssetPickerOwnerMin.y - 256.0f || PickerPos.y > InlineAssetPickerOwnerMax.y + 256.0f));
    if (bAnchorLooksInvalid)
    {
        PickerPos = InlineAssetPickerFallbackPos;
    }

    const ImVec2 EstimatedSize(380.0f, 390.0f);
    if (InlineAssetPickerOwnerMax.x > InlineAssetPickerOwnerMin.x && InlineAssetPickerOwnerMax.y > InlineAssetPickerOwnerMin.y)
    {
        const float MinX = InlineAssetPickerOwnerMin.x + 6.0f;
        const float MinY = InlineAssetPickerOwnerMin.y + 6.0f;
        const float MaxX = (std::max)(MinX, InlineAssetPickerOwnerMax.x - EstimatedSize.x - 6.0f);
        const float MaxY = (std::max)(MinY, InlineAssetPickerOwnerMax.y - EstimatedSize.y - 6.0f);
        PickerPos.x = (std::min)((std::max)(PickerPos.x, MinX), MaxX);
        PickerPos.y = (std::min)((std::max)(PickerPos.y, MinY), MaxY);
    }

    // 위치는 새 open 요청이 들어온 프레임에만 강제로 지정한다.
    // 그 이후에는 ImGui 가 보관한 위치를 사용해야 사용자가 창을 드래그해서 옮길 수 있다.
    const ImGuiCond PickerPlacementCond = bJustOpenedThisFrame ? ImGuiCond_Always : ImGuiCond_Appearing;
    ImGui::SetNextWindowPos(PickerPos, PickerPlacementCond);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), PickerPlacementCond);
    if (bJustOpenedThisFrame)
    {
        ImGui::SetNextWindowFocus();
    }

    bool bWindowOpen = true;
    const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    if (!ImGui::Begin("Asset Picker##LuaBlueprintInlineAssetPicker", &bWindowOpen, Flags))
    {
        ImGui::End();
        ImGui::PopStyleVar();
        if (!bWindowOpen)
        {
            bInlineAssetPickerVisible = false;
        }
        return;
    }

    if (!Blueprint || InlineAssetPickerType.empty())
    {
        ImGui::TextDisabled("No asset type.");
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    const FString CurrentPath = InlineAssetPickerPreviewPath;
    ImGui::TextDisabled("%s", InlineAssetPickerType.c_str());
    if (!CurrentPath.empty() && CurrentPath != "None")
    {
        ImGui::SameLine();
        ImGui::TextDisabled("/ %s", LuaBlueprintAssetPreviewText(CurrentPath).c_str());
    }
    ImGui::Separator();

    ImGui::SetNextItemWidth(350.0f);
    if (bJustOpenedThisFrame)
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputTextWithHint("##AssetSearch", "search...", InlineAssetPickerSearchBuf, sizeof(InlineAssetPickerSearchBuf));

    auto MatchesSearch = [&](const FAssetListItem& Item)
    {
        if (InlineAssetPickerSearchBuf[0] == 0)
        {
            return true;
        }
        return ContainsCaseInsensitive(Item.DisplayName.c_str(), InlineAssetPickerSearchBuf)
            || ContainsCaseInsensitive(Item.FullPath.c_str(), InlineAssetPickerSearchBuf);
    };

    bool bClosePicker = false;

    const bool bSelectedNone = CurrentPath.empty() || CurrentPath == "None";
    if (ImGui::Selectable("None", bSelectedNone))
    {
        ApplyInlineAssetPickerSelection(Blueprint, "None");
        bClosePicker = true;
    }
    if (bSelectedNone)
    {
        ImGui::SetItemDefaultFocus();
    }

    ImGui::Separator();
    const TArray<FAssetListItem>& Items = FAssetRegistry::ListByTypeName(InlineAssetPickerType.c_str());
    int32 VisibleCount = 0;
    ImGui::BeginChild("##AssetList", ImVec2(360.0f, 280.0f), true);
    for (const FAssetListItem& Item : Items)
    {
        if (!MatchesSearch(Item))
        {
            continue;
        }
        ++VisibleCount;
        const bool bSelected = Item.FullPath == CurrentPath;
        const char* Label = Item.DisplayName.empty() ? Item.FullPath.c_str() : Item.DisplayName.c_str();
        ImGui::PushID(Item.FullPath.c_str());
        if (ImGui::Selectable(Label, bSelected))
        {
            ApplyInlineAssetPickerSelection(Blueprint, Item.FullPath);
            bClosePicker = true;
        }
        if (ImGui::IsItemHovered() && !Item.FullPath.empty())
        {
            ImGui::SetTooltip("%s", Item.FullPath.c_str());
        }
        if (bSelected)
        {
            ImGui::SetItemDefaultFocus();
        }
        ImGui::PopID();
    }
    if (VisibleCount == 0)
    {
        ImGui::TextDisabled(Items.empty() ? "No assets found." : "No matching assets.");
    }
    ImGui::EndChild();

    // IsWindowHovered() 만으로 닫힘 판정을 하면 InputText/Child/드래그 상태에서
    // hovered window 가 예상과 다르게 잡혀 창 내부 클릭도 외부 클릭처럼 처리될 수 있다.
    // 따라서 현재 picker window 의 실제 screen rect 를 저장하고, 외부 클릭은 이 rect 기준으로 판정한다.
    const ImVec2 PickerWindowMin = ImGui::GetWindowPos();
    const ImVec2 PickerWindowMax(
        PickerWindowMin.x + ImGui::GetWindowSize().x,
        PickerWindowMin.y + ImGui::GetWindowSize().y
    );
    const ImVec2 MousePos = ImGui::GetMousePos();
    const bool bMouseInsidePicker =
        MousePos.x >= PickerWindowMin.x && MousePos.x <= PickerWindowMax.x
        && MousePos.y >= PickerWindowMin.y && MousePos.y <= PickerWindowMax.y;

    ImGui::End();
    ImGui::PopStyleVar();

    if (!bWindowOpen || bClosePicker || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        bInlineAssetPickerVisible = false;
        return;
    }

    // Popup API 대신 floating window 를 사용하므로 외부 클릭 닫기도 직접 처리한다.
    // 창 내부 검색/선택/스크롤/드래그는 모두 유지하고, picker rect 밖에서 새 left-click 이 들어왔을 때만 닫는다.
    if (!bJustOpenedThisFrame && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bMouseInsidePicker)
    {
        bInlineAssetPickerVisible = false;
    }
}


bool FLuaBlueprintEditorWidget::RenderInlinePinLiteral(
    ULuaBlueprintAsset* Blueprint,
    FLuaBlueprintNode& Node,
    FLuaBlueprintPin& Pin
)
{
    // 데이터 input pin 만 inline editor 노출. Exec/Object/Any/Array 는 literal 편집 대상이 아니다.
    if (Pin.Kind != ELuaBlueprintPinKind::Input) return false;
    if (Pin.Type == ELuaBlueprintPinType::Exec || Pin.Type == ELuaBlueprintPinType::Object || Pin.Type ==
        ELuaBlueprintPinType::Any || Pin.Type == ELuaBlueprintPinType::Array || Pin.Type == ELuaBlueprintPinType::Matrix)
        return false;

    // 이미 link 가 있으면 inline literal 숨김 (실제 Blueprint 와 동일).
    if (Blueprint->FindLinkToInput(Pin.PinId) != nullptr) return false;

    bool bChanged = false;
    ImGui::PushID(static_cast<int>(Pin.PinId));
    switch (Pin.Type)
    {
    case ELuaBlueprintPinType::Bool:
        if (ImGui::Checkbox("##def", &Pin.DefaultBool)) bChanged = true;
        break;
    case ELuaBlueprintPinType::Int:
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##def", &Pin.DefaultInt, 0)) bChanged = true;
        break;
    case ELuaBlueprintPinType::Float:
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::DragFloat("##def", &Pin.DefaultFloat, 0.01f, 0.0f, 0.0f, "%.3f")) bChanged = true;
        break;
    case ELuaBlueprintPinType::String:
    {
        if (const char* AssetType = LuaBlueprintAssetTypeForPin(Node, Pin))
        {
            const FString Preview = LuaBlueprintAssetPreviewText(Pin.DefaultString);
            ImGui::SetNextItemWidth(190.0f);
            if (ImGui::Button(Preview.c_str(), ImVec2(190.0f, 0.0f)))
            {
                OpenInlineAssetPicker(Node, Pin, AssetType);
            }
            if (ImGui::IsItemHovered() && !Pin.DefaultString.empty() && Pin.DefaultString != "None")
            {
                bInlineAssetHoverTooltipVisible = true;
                InlineAssetHoverTooltipPath = Pin.DefaultString;
            }
        }
        else
        {
            char Buf[256];
            CopyToBuffer(Buf, sizeof(Buf), Pin.DefaultString);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputText("##def", Buf, sizeof(Buf)))
            {
                Pin.DefaultString = Buf;
                bChanged          = true;
            }
        }
        break;
    }
    case ELuaBlueprintPinType::Name:
    case ELuaBlueprintPinType::Class:
    case ELuaBlueprintPinType::Enum:
    {
        char Buf[256];
        CopyToBuffer(Buf, sizeof(Buf), Pin.DefaultString);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("##def", Buf, sizeof(Buf)))
        {
            Pin.DefaultString = Buf;
            bChanged          = true;
        }
        break;
    }
    case ELuaBlueprintPinType::Vector:
    case ELuaBlueprintPinType::Rotator:
    {
        float V[3] = { Pin.DefaultVector.X, Pin.DefaultVector.Y, Pin.DefaultVector.Z };
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::DragFloat3("##def", V, 0.01f))
        {
            Pin.DefaultVector = FVector(V[0], V[1], V[2]);
            bChanged          = true;
        }
        break;
    }
    case ELuaBlueprintPinType::LinearColor:
    case ELuaBlueprintPinType::Vector4:
    {
        float V[4] = { Pin.DefaultVector.X, Pin.DefaultVector.Y, Pin.DefaultVector.Z, Pin.DefaultFloat };
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::DragFloat4("##def", V, 0.01f))
        {
            Pin.DefaultVector = FVector(V[0], V[1], V[2]);
            Pin.DefaultFloat  = V[3];
            bChanged          = true;
        }
        break;
    }
    default:
        break;
    }
    ImGui::PopID();

    if (bChanged)
    {
        Blueprint->BumpVersion();
        CommitBlueprintEdit(Blueprint);
    }
    return bChanged;
}

bool FLuaBlueprintEditorWidget::AddNodeMenuItem(ULuaBlueprintAsset* Blueprint, ELuaBlueprintNodeType Type)
{
    const bool bDisabled = IsEventNode(Type) && HasNodeOfType(Blueprint, Type);
    if (bDisabled) ImGui::BeginDisabled();
    ImGui::PushID(static_cast<int32>(Type));
    ImGui::PushStyleColor(ImGuiCol_Text, NodeHeaderColor(Type));
    const bool bClicked = ImGui::MenuItem(NodeTypeLabel(Type));
    ImGui::PopStyleColor();
    ImGui::PopID();
    if (bClicked)
    {
        FLuaBlueprintNode* NewNode = Blueprint->AddNodeOfType(Type, PendingNewNodePosition.x, PendingNewNodePosition.y);
        if (NewNode && NodeEditorContext)
        {
            FScopedNodeEditorCurrent Scope(NodeEditorContext);
            ed::SetNodePosition(ToNodeId(NewNode->NodeId), PendingNewNodePosition);
        }
        CommitBlueprintEdit(Blueprint);
    }
    if (bDisabled) ImGui::EndDisabled();
    return bClicked;
}

FLuaBlueprintNode* FLuaBlueprintEditorWidget::SpawnReflectedFunctionNode(
    ULuaBlueprintAsset* Blueprint,
    const FFunction*    Function,
    const ImVec2&       Position,
    bool                bSelectNode
)
{
    if (!Blueprint || !IsLuaBlueprintCallableFunction(Function))
    {
        return nullptr;
    }

    const FString Signature = Function->GetSignature();
    if (Signature.empty())
    {
        return nullptr;
    }

    FLuaBlueprintNode* NewNode = Blueprint->AddNodeOfType(
        ELuaBlueprintNodeType::CallFunctionSignature,
        Position.x,
        Position.y
    );
    if (!NewNode)
    {
        return nullptr;
    }

    NewNode->NameValue   = FName(Function->GetName());
    NewNode->StringValue = Signature;
    NewNode->DisplayName = FName(Function->GetDisplayName());
    NewNode->PosX        = Position.x;
    NewNode->PosY        = Position.y;
    Blueprint->RefreshNodePinTypes(*NewNode);

    if (NodeEditorContext)
    {
        FScopedNodeEditorCurrent Scope(NodeEditorContext);
        ed::SetNodePosition(ToNodeId(NewNode->NodeId), Position);
    }

    if (bSelectNode)
    {
        SelectOnlyNodes(TArray<uint32> { NewNode->NodeId });
    }

    CommitBlueprintEdit(Blueprint);
    return NewNode;
}

bool FLuaBlueprintEditorWidget::AddReflectedFunctionNodeMenuItem(
    ULuaBlueprintAsset* Blueprint,
    const UClass*       Class,
    const FFunction*    Function,
    const char*         SearchQuery,
    const ImVec2&       Position,
    bool                bSelectNode
)
{
    if (!Blueprint || !IsLuaBlueprintCallableFunction(Function))
    {
        return false;
    }

    const FString SearchText = MakeReflectedFunctionSearchText(Class, Function);
    if (!ContainsCaseInsensitive(SearchText.c_str(), SearchQuery))
    {
        return false;
    }

    FString Label;
    if (Class)
    {
        Label += Class->GetName();
        Label += "::";
    }
    Label += Function->GetDisplayName();
    const FString Signature = Function->GetSignature();
    if (!Signature.empty())
    {
        Label += "  [";
        Label += Signature;
        Label += "]";
    }

    ImGui::PushID(Class ? Class->GetName() : "ReflectedFunction");
    ImGui::PushID(Function->GetSignature());
    ImGui::PushStyleColor(ImGuiCol_Text, NodeHeaderColor(ELuaBlueprintNodeType::CallFunctionSignature));
    const bool bClicked = ImGui::MenuItem(Label.c_str());
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::PopID();

    if (!bClicked)
    {
        return false;
    }

    SpawnReflectedFunctionNode(Blueprint, Function, Position, bSelectNode);
    return true;
}

void FLuaBlueprintEditorWidget::RenderReflectedFunctionNodeMenu(
    ULuaBlueprintAsset* Blueprint,
    const char*         SearchQuery,
    const ImVec2&       Position,
    bool                bFlatList,
    bool                bSelectNode
)
{
    if (!Blueprint)
    {
        return;
    }

    TArray<UClass*> Classes = UClass::GetAllClasses();
    std::sort(
        Classes.begin(),
        Classes.end(),
        [](const UClass* A, const UClass* B)
        {
            const char* AN = A ? A->GetName() : "";
            const char* BN = B ? B->GetName() : "";
            return std::strcmp(AN, BN) < 0;
        }
    );

    int32 NumShown = 0;
    for (UClass* Class : Classes)
    {
        if (!Class)
        {
            continue;
        }

        TArray<const FFunction*> Functions;
        Class->GetFunctionRefs(Functions, false);
        if (Functions.empty())
        {
            continue;
        }

        std::sort(
            Functions.begin(),
            Functions.end(),
            [](const FFunction* A, const FFunction* B)
            {
                const char* AN = A ? A->GetDisplayName() : "";
                const char* BN = B ? B->GetDisplayName() : "";
                const int Cmp = std::strcmp(AN, BN);
                if (Cmp != 0)
                {
                    return Cmp < 0;
                }
                const char* AS = A ? A->GetSignature() : "";
                const char* BS = B ? B->GetSignature() : "";
                return std::strcmp(AS, BS) < 0;
            }
        );

        bool bClassHasVisibleFunction = false;
        for (const FFunction* Function : Functions)
        {
            if (!IsLuaBlueprintCallableFunction(Function))
            {
                continue;
            }
            const FString SearchText = MakeReflectedFunctionSearchText(Class, Function);
            if (!ContainsCaseInsensitive(SearchText.c_str(), SearchQuery))
            {
                continue;
            }
            bClassHasVisibleFunction = true;
            break;
        }
        if (!bClassHasVisibleFunction)
        {
            continue;
        }

        if (bFlatList)
        {
            for (const FFunction* Function : Functions)
            {
                if (AddReflectedFunctionNodeMenuItem(Blueprint, Class, Function, SearchQuery, Position, bSelectNode))
                {
                    return;
                }
                const FString SearchText = MakeReflectedFunctionSearchText(Class, Function);
                if (IsLuaBlueprintCallableFunction(Function) && ContainsCaseInsensitive(SearchText.c_str(), SearchQuery))
                {
                    ++NumShown;
                }
            }
            continue;
        }

        if (!ImGui::BeginMenu(Class->GetName()))
        {
            continue;
        }
        for (const FFunction* Function : Functions)
        {
            if (AddReflectedFunctionNodeMenuItem(Blueprint, nullptr, Function, SearchQuery, Position, bSelectNode))
            {
                ImGui::EndMenu();
                return;
            }
            const FString SearchText = MakeReflectedFunctionSearchText(Class, Function);
            if (IsLuaBlueprintCallableFunction(Function) && ContainsCaseInsensitive(SearchText.c_str(), SearchQuery))
            {
                ++NumShown;
            }
        }
        ImGui::EndMenu();
    }

    if (NumShown == 0)
    {
        ImGui::TextDisabled("No reflected functions found. If headers changed, regenerate reflection output first.");
    }
}

bool FLuaBlueprintEditorWidget::AddVariableMenuItem(
    ULuaBlueprintAsset*  Blueprint,
    ELuaBlueprintPinType Type,
    const char*          Label
)
{
    if (!ImGui::MenuItem(Label)) return false;

    char NameBuf[48];
    std::snprintf(NameBuf, sizeof(NameBuf), "Var%u", static_cast<uint32>(Blueprint->GetVariables().size()));
    Blueprint->AddVariable(FName(NameBuf), Type);
    CommitBlueprintEdit(Blueprint);
    return true;
}

void FLuaBlueprintEditorWidget::RenderAddNodeMenu(ULuaBlueprintAsset* Blueprint)
{
    ImGui::TextDisabled("Add Node");
    ImGui::Separator();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##Search", "search...", AddNodeSearchBuf, sizeof(AddNodeSearchBuf));
    const char* Query = AddNodeSearchBuf;

    auto AddItem = [&](ELuaBlueprintNodeType Type)
    {
        if (!ContainsCaseInsensitive(NodeTypeLabel(Type), Query)) return;
        AddNodeMenuItem(Blueprint, Type);
    };

    const bool bHasQuery = Query[0] != 0;
    if (bHasQuery)
    {
        // 검색 모드: 고정 enum 노드와 reflection 기반 함수 노드를 함께 평평하게 보여준다.
        for (int32 i = 0; i < static_cast<int32>(ELuaBlueprintNodeType::Count); ++i)
        {
            AddItem(static_cast<ELuaBlueprintNodeType>(i));
        }
        RenderReflectedFunctionNodeMenu(Blueprint, Query, PendingNewNodePosition, true, false);
    }
    else
    {
        if (ImGui::BeginMenu("Events"))
        {
            AddItem(ELuaBlueprintNodeType::EventBeginPlay);
            AddItem(ELuaBlueprintNodeType::EventTick);
            AddItem(ELuaBlueprintNodeType::EventEndPlay);
            AddItem(ELuaBlueprintNodeType::EventOverlap);
            AddItem(ELuaBlueprintNodeType::EventEndOverlap);
            AddItem(ELuaBlueprintNodeType::EventHit);
            AddItem(ELuaBlueprintNodeType::EventEndHit);
            AddItem(ELuaBlueprintNodeType::EventPostBeginPlay);
            AddItem(ELuaBlueprintNodeType::EventPostStartMatch);
            AddItem(ELuaBlueprintNodeType::EventPlayerCameraReady);
            AddItem(ELuaBlueprintNodeType::EventInputAction);
            AddItem(ELuaBlueprintNodeType::EventInputAxis);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Flow"))
        {
            AddItem(ELuaBlueprintNodeType::Sequence);
            AddItem(ELuaBlueprintNodeType::Branch);
            AddItem(ELuaBlueprintNodeType::ForLoop);
            AddItem(ELuaBlueprintNodeType::WhileLoop);
            AddItem(ELuaBlueprintNodeType::ForEachActorByClass);
            AddItem(ELuaBlueprintNodeType::ForEachActorByTag);
            AddItem(ELuaBlueprintNodeType::ForEachArray);
            AddItem(ELuaBlueprintNodeType::Delay);
            AddItem(ELuaBlueprintNodeType::SetTimer);
            AddItem(ELuaBlueprintNodeType::ClearTimer);
            AddItem(ELuaBlueprintNodeType::IsTimerActive);
            AddItem(ELuaBlueprintNodeType::SetTimerForNextTick);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Actor"))
        {
            AddItem(ELuaBlueprintNodeType::SpawnActor);
            AddItem(ELuaBlueprintNodeType::SpawnPawn);
            AddItem(ELuaBlueprintNodeType::SpawnPawnAndPossess);
            AddItem(ELuaBlueprintNodeType::DestroyActor);
            AddItem(ELuaBlueprintNodeType::FindActorByName);
            AddItem(ELuaBlueprintNodeType::FindActorByClass);
            AddItem(ELuaBlueprintNodeType::FindActorByTag);
            AddItem(ELuaBlueprintNodeType::FindActorsByTag);
            AddItem(ELuaBlueprintNodeType::FindActorsByClass);
            AddItem(ELuaBlueprintNodeType::LineTrace);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::GetActorLocation);
            AddItem(ELuaBlueprintNodeType::SetActorLocation);
            AddItem(ELuaBlueprintNodeType::GetActorRotation);
            AddItem(ELuaBlueprintNodeType::SetActorRotation);
            AddItem(ELuaBlueprintNodeType::GetActorScale);
            AddItem(ELuaBlueprintNodeType::SetActorScale);
            AddItem(ELuaBlueprintNodeType::GetActorForward);
            AddItem(ELuaBlueprintNodeType::GetActorRight);
            AddItem(ELuaBlueprintNodeType::AddActorWorldOffset);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::AddForceToRoot);
            AddItem(ELuaBlueprintNodeType::AddTorqueToRoot);
            AddItem(ELuaBlueprintNodeType::AddImpulseToRoot);
            AddItem(ELuaBlueprintNodeType::GetRootLinearVelocity);
            AddItem(ELuaBlueprintNodeType::SetRootLinearVelocity);
            AddItem(ELuaBlueprintNodeType::SetRootSimulatePhysics);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::ActorHasTag);
            AddItem(ELuaBlueprintNodeType::ActorAddTag);
            AddItem(ELuaBlueprintNodeType::ActorRemoveTag);
            AddItem(ELuaBlueprintNodeType::GetActorName);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Game Framework"))
        {
            AddItem(ELuaBlueprintNodeType::GetPlayerController);
            AddItem(ELuaBlueprintNodeType::GetController);
            AddItem(ELuaBlueprintNodeType::GetControlledPawn);
            AddItem(ELuaBlueprintNodeType::Possess);
            AddItem(ELuaBlueprintNodeType::UnPossess);
            AddItem(ELuaBlueprintNodeType::IsPawnPossessed);
            AddItem(ELuaBlueprintNodeType::GetInputComponent);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Vehicle"))
        {
            AddItem(ELuaBlueprintNodeType::VehicleSetThrottle);
            AddItem(ELuaBlueprintNodeType::VehicleSetBrake);
            AddItem(ELuaBlueprintNodeType::VehicleSetSteering);
            AddItem(ELuaBlueprintNodeType::VehicleSetHandbrake);
            AddItem(ELuaBlueprintNodeType::VehicleReset);
            AddItem(ELuaBlueprintNodeType::VehicleGetForwardSpeed);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Particle"))
        {
            AddItem(ELuaBlueprintNodeType::ParticleSetTemplateByPath);
            AddItem(ELuaBlueprintNodeType::ParticleActivate);
            AddItem(ELuaBlueprintNodeType::ParticleDeactivate);
            AddItem(ELuaBlueprintNodeType::ParticleReset);
            AddItem(ELuaBlueprintNodeType::ParticleRebuild);
            AddItem(ELuaBlueprintNodeType::ParticleSetLOD);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Object"))
        {
            AddItem(ELuaBlueprintNodeType::IsValid);
            AddItem(ELuaBlueprintNodeType::Cast);
            AddItem(ELuaBlueprintNodeType::GetOwnerActor);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Component"))
        {
            AddItem(ELuaBlueprintNodeType::GetRootComponent);
            AddItem(ELuaBlueprintNodeType::GetRootPrimitiveComponent);
            AddItem(ELuaBlueprintNodeType::GetComponentByName);
            AddItem(ELuaBlueprintNodeType::GetPrimitiveComponent);
            AddItem(ELuaBlueprintNodeType::ActivateComponent);
            AddItem(ELuaBlueprintNodeType::DeactivateComponent);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::AttachToComponent);
            AddItem(ELuaBlueprintNodeType::GetAttachSocketName);
            AddItem(ELuaBlueprintNodeType::HasSocket);
            AddItem(ELuaBlueprintNodeType::GetSocketWorldLocation);
            AddItem(ELuaBlueprintNodeType::GetSocketWorldRotation);
            AddItem(ELuaBlueprintNodeType::GetSocketWorldScale);
            AddItem(ELuaBlueprintNodeType::GetSocketForwardVector);
            AddItem(ELuaBlueprintNodeType::GetSocketRightVector);
            AddItem(ELuaBlueprintNodeType::GetSocketUpVector);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::AddForce);
            AddItem(ELuaBlueprintNodeType::AddTorque);
            AddItem(ELuaBlueprintNodeType::AddImpulse);
            AddItem(ELuaBlueprintNodeType::GetLinearVelocity);
            AddItem(ELuaBlueprintNodeType::SetLinearVelocity);
            AddItem(ELuaBlueprintNodeType::GetMass);
            AddItem(ELuaBlueprintNodeType::SetSimulatePhysics);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Lua / Blueprint Calls"))
        {
            AddItem(ELuaBlueprintNodeType::GetLuaBlueprintComponent);
            AddItem(ELuaBlueprintNodeType::GetLuaScriptComponent);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::CallLuaBlueprintFunction);
            AddItem(ELuaBlueprintNodeType::CallLuaScriptFunction);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::CallLuaBlueprintFileFunction);
            AddItem(ELuaBlueprintNodeType::CallLuaScriptFileFunction);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Static Mesh"))
        {
            AddItem(ELuaBlueprintNodeType::LoadStaticMesh);
            AddItem(ELuaBlueprintNodeType::GetStaticMeshComponent);
            AddItem(ELuaBlueprintNodeType::GetStaticMesh);
            AddItem(ELuaBlueprintNodeType::SetStaticMesh);
            AddItem(ELuaBlueprintNodeType::SetStaticMeshByPath);
            AddItem(ELuaBlueprintNodeType::ClearStaticMesh);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Skeletal Mesh / Animation"))
        {
            AddItem(ELuaBlueprintNodeType::GetSkeletalMeshComponent);
            AddItem(ELuaBlueprintNodeType::SetSkeletalMeshByPath);
            AddItem(ELuaBlueprintNodeType::ClearSkeletalMesh);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::PlayAnimationByPath);
            AddItem(ELuaBlueprintNodeType::StopAnimation);
            AddItem(ELuaBlueprintNodeType::SetAnimationByPath);
            AddItem(ELuaBlueprintNodeType::SetAnimationPlayRate);
            AddItem(ELuaBlueprintNodeType::SetAnimationLooping);
            AddItem(ELuaBlueprintNodeType::SetAnimationPlaying);
            AddItem(ELuaBlueprintNodeType::GetAnimInstance);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::SetAnimGraphVariableFloat);
            AddItem(ELuaBlueprintNodeType::SetAnimGraphVariableBool);
            AddItem(ELuaBlueprintNodeType::SetAnimGraphVariableInt);
            AddItem(ELuaBlueprintNodeType::GetAnimGraphVariableFloat);
            AddItem(ELuaBlueprintNodeType::GetAnimGraphVariableBool);
            AddItem(ELuaBlueprintNodeType::GetAnimGraphVariableInt);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Material"))
        {
            AddItem(ELuaBlueprintNodeType::LoadMaterial);
            AddItem(ELuaBlueprintNodeType::LoadTexture);
            AddItem(ELuaBlueprintNodeType::GetMaterial);
            AddItem(ELuaBlueprintNodeType::SetMaterial);
            AddItem(ELuaBlueprintNodeType::SetMaterialByPath);
            AddItem(ELuaBlueprintNodeType::CreateDynamicMaterialInstance);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::SetMaterialScalarParameter);
            AddItem(ELuaBlueprintNodeType::SetMaterialVectorParameter);
            AddItem(ELuaBlueprintNodeType::SetMaterialColorParameter);
            AddItem(ELuaBlueprintNodeType::SetMaterialTextureParameter);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Camera / Cinematic"))
        {
            AddItem(ELuaBlueprintNodeType::GetCameraComponent);
            AddItem(ELuaBlueprintNodeType::GetActiveCamera);
            AddItem(ELuaBlueprintNodeType::PossessCamera);
            AddItem(ELuaBlueprintNodeType::SetActiveCameraWithBlend);
            AddItem(ELuaBlueprintNodeType::SetViewTargetWithBlend);
            AddItem(ELuaBlueprintNodeType::SetCameraFOV);
            AddItem(ELuaBlueprintNodeType::CameraLookAt);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::FadeIn);
            AddItem(ELuaBlueprintNodeType::FadeOut);
            AddItem(ELuaBlueprintNodeType::SetVignette);
            AddItem(ELuaBlueprintNodeType::ClearVignette);
            AddItem(ELuaBlueprintNodeType::StartCameraShakeAsset);
            ImGui::Separator();
            AddItem(ELuaBlueprintNodeType::SetDepthOfField);
            AddItem(ELuaBlueprintNodeType::SetBokeh);
            AddItem(ELuaBlueprintNodeType::ClearDepthOfField);
            AddItem(ELuaBlueprintNodeType::SetLetterbox);
            AddItem(ELuaBlueprintNodeType::ClearLetterbox);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI"))
        {
            AddItem(ELuaBlueprintNodeType::CreateWidget);
            AddItem(ELuaBlueprintNodeType::AddWidgetToViewport);
            AddItem(ELuaBlueprintNodeType::RemoveWidgetFromParent);
            AddItem(ELuaBlueprintNodeType::SetWidgetText);
            AddItem(ELuaBlueprintNodeType::BindWidgetClick);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Audio"))
        {
            AddItem(ELuaBlueprintNodeType::LoadAudio);
            AddItem(ELuaBlueprintNodeType::AudioPlaySound);
            AddItem(ELuaBlueprintNodeType::PlayBGM);
            AddItem(ELuaBlueprintNodeType::StopBGM);
            AddItem(ELuaBlueprintNodeType::PlayAudioLoop);
            AddItem(ELuaBlueprintNodeType::StopAudioLoop);
            AddItem(ELuaBlueprintNodeType::SetAudioMasterVolume);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Utility"))
        {
            AddItem(ELuaBlueprintNodeType::Lerp);
            AddItem(ELuaBlueprintNodeType::Clamp);
            AddItem(ELuaBlueprintNodeType::Min);
            AddItem(ELuaBlueprintNodeType::Max);
            AddItem(ELuaBlueprintNodeType::RandomFloat);
            AddItem(ELuaBlueprintNodeType::RandomInt);
            AddItem(ELuaBlueprintNodeType::Sin);
            AddItem(ELuaBlueprintNodeType::Cos);
            AddItem(ELuaBlueprintNodeType::Sqrt);
            AddItem(ELuaBlueprintNodeType::AbsFloat);
            AddItem(ELuaBlueprintNodeType::Floor);
            AddItem(ELuaBlueprintNodeType::Ceil);
            AddItem(ELuaBlueprintNodeType::Distance);
            AddItem(ELuaBlueprintNodeType::GetGameTime);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Variables / Properties"))
        {
            AddItem(ELuaBlueprintNodeType::GetVariable);
            AddItem(ELuaBlueprintNodeType::SetVariable);
            AddItem(ELuaBlueprintNodeType::GetProperty);
            AddItem(ELuaBlueprintNodeType::SetProperty);
            AddItem(ELuaBlueprintNodeType::Self);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Functions"))
        {
            AddItem(ELuaBlueprintNodeType::CallFunction);
            AddItem(ELuaBlueprintNodeType::CallFunctionSignature);
            if (ImGui::BeginMenu("Reflected Functions"))
            {
                RenderReflectedFunctionNodeMenu(Blueprint, Query, PendingNewNodePosition, false, false);
                ImGui::EndMenu();
            }
            AddItem(ELuaBlueprintNodeType::CustomEvent);
            AddItem(ELuaBlueprintNodeType::CallCustomEvent);
            AddItem(ELuaBlueprintNodeType::CustomLuaFunction);
            AddItem(ELuaBlueprintNodeType::CallCustomLuaFunction);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Delegates"))
        {
            AddItem(ELuaBlueprintNodeType::BindEvent);
            AddItem(ELuaBlueprintNodeType::UnbindEvent);
            AddItem(ELuaBlueprintNodeType::HasEventBinding);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Conversions"))
        {
            AddItem(ELuaBlueprintNodeType::ToBool);
            AddItem(ELuaBlueprintNodeType::ToInt);
            AddItem(ELuaBlueprintNodeType::ToFloat);
            AddItem(ELuaBlueprintNodeType::ToString);
            AddItem(ELuaBlueprintNodeType::ToVector);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Graph Utility"))
        {
            AddItem(ELuaBlueprintNodeType::Reroute);
            AddItem(ELuaBlueprintNodeType::Comment);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Literals"))
        {
            AddItem(ELuaBlueprintNodeType::LiteralBool);
            AddItem(ELuaBlueprintNodeType::LiteralInt);
            AddItem(ELuaBlueprintNodeType::LiteralFloat);
            AddItem(ELuaBlueprintNodeType::LiteralString);
            AddItem(ELuaBlueprintNodeType::LiteralVector);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Math (Float)"))
        {
            AddItem(ELuaBlueprintNodeType::AddFloat);
            AddItem(ELuaBlueprintNodeType::SubtractFloat);
            AddItem(ELuaBlueprintNodeType::MultiplyFloat);
            AddItem(ELuaBlueprintNodeType::DivideFloat);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Math (Int)"))
        {
            AddItem(ELuaBlueprintNodeType::AddInt);
            AddItem(ELuaBlueprintNodeType::SubtractInt);
            AddItem(ELuaBlueprintNodeType::MultiplyInt);
            AddItem(ELuaBlueprintNodeType::DivideInt);
            AddItem(ELuaBlueprintNodeType::ModInt);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Compare"))
        {
            AddItem(ELuaBlueprintNodeType::EqualFloat);
            AddItem(ELuaBlueprintNodeType::NotEqualFloat);
            AddItem(ELuaBlueprintNodeType::LessFloat);
            AddItem(ELuaBlueprintNodeType::GreaterFloat);
            AddItem(ELuaBlueprintNodeType::LessEqualFloat);
            AddItem(ELuaBlueprintNodeType::GreaterEqualFloat);
            AddItem(ELuaBlueprintNodeType::EqualInt);
            AddItem(ELuaBlueprintNodeType::NotEqualInt);
            AddItem(ELuaBlueprintNodeType::LessInt);
            AddItem(ELuaBlueprintNodeType::GreaterInt);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Logic"))
        {
            AddItem(ELuaBlueprintNodeType::And);
            AddItem(ELuaBlueprintNodeType::Or);
            AddItem(ELuaBlueprintNodeType::Not);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("String"))
        {
            AddItem(ELuaBlueprintNodeType::AppendString);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Vector"))
        {
            AddItem(ELuaBlueprintNodeType::MakeVector);
            AddItem(ELuaBlueprintNodeType::BreakVector);
            AddItem(ELuaBlueprintNodeType::AddVector);
            AddItem(ELuaBlueprintNodeType::SubtractVector);
            AddItem(ELuaBlueprintNodeType::ScaleVector);
            AddItem(ELuaBlueprintNodeType::DotVector);
            AddItem(ELuaBlueprintNodeType::CrossVector);
            AddItem(ELuaBlueprintNodeType::VectorLength);
            AddItem(ELuaBlueprintNodeType::NormalizeVector);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        AddItem(ELuaBlueprintNodeType::PrintString);
    }
}

void FLuaBlueprintEditorWidget::RenderPinSpawnMenu(ULuaBlueprintAsset* Blueprint)
{
    const FLuaBlueprintPin* DragPin = Blueprint ? Blueprint->FindPin(PendingPinSpawnPinId) : nullptr;
    if (!DragPin)
    {
        ImGui::TextDisabled("No pin context.");
        return;
    }

    ImGui::TextDisabled(
        DragPin->Kind == ELuaBlueprintPinKind::Output
        ? "Nodes accepting %s input"
        : "Nodes producing %s output",
        PinTypeLabel(DragPin->Type)
    );
    ImGui::Separator();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##PinSpawnSearch", "search compatible nodes...", PinSpawnSearchBuf, sizeof(PinSpawnSearchBuf));
    ImGui::Separator();

    int32 NumShown = 0;
    for (int32 i = 0; i < static_cast<int32>(ELuaBlueprintNodeType::Count); ++i)
    {
        const ELuaBlueprintNodeType Type = static_cast<ELuaBlueprintNodeType>(i);
        if (!ContainsCaseInsensitive(NodeTypeLabel(Type), PinSpawnSearchBuf)) continue;

        const size_t BeforeNodes = Blueprint ? Blueprint->GetNodes().size() : 0;
        if (AddContextNodeMenuItem(Blueprint, Type))
        {
            PendingPinSpawnPinId = 0;
            ImGui::CloseCurrentPopup();
            return;
        }
        const size_t AfterNodes = Blueprint ? Blueprint->GetNodes().size() : 0;
        if (AfterNodes == BeforeNodes && NodeTypeCanConnectToPendingPin(Type))
        {
            ++NumShown;
        }
    }

    if (NumShown == 0)
    {
        ImGui::TextDisabled("No compatible nodes.");
    }
}

bool FLuaBlueprintEditorWidget::AddContextNodeMenuItem(ULuaBlueprintAsset* Blueprint, ELuaBlueprintNodeType Type)
{
    if (!Blueprint || PendingPinSpawnPinId == 0) return false;
    const FLuaBlueprintPin* DragPin = Blueprint->FindPin(PendingPinSpawnPinId);
    if (!DragPin) return false;
    if (IsEventNode(Type) && HasNodeOfType(Blueprint, Type)) return false;
    const FLuaBlueprintPin DragPinCopy = *DragPin;
    if (!NodeTypeCanConnectToPendingPin(Type)) return false;

    ImGui::PushID(static_cast<int32>(Type));
    ImGui::PushStyleColor(ImGuiCol_Text, NodeHeaderColor(Type));
    const bool bClicked = ImGui::MenuItem(NodeTypeLabel(Type));
    ImGui::PopStyleColor();
    ImGui::PopID();
    if (!bClicked) return false;

    FLuaBlueprintNode* NewNode = Blueprint->AddNodeOfType(Type, PendingPinSpawnPos.x, PendingPinSpawnPos.y);
    if (!NewNode) return false;
    Blueprint->RefreshNodePinTypes(*NewNode);

    if (NodeEditorContext)
    {
        FScopedNodeEditorCurrent Scope(NodeEditorContext);
        ed::SetNodePosition(ToNodeId(NewNode->NodeId), PendingPinSpawnPos);
    }

    FLuaBlueprintPin* OtherPin = FindFirstCompatiblePinOnNode(Blueprint, *NewNode, DragPinCopy);
    if (OtherPin)
    {
        uint32 FromPinId    = 0;
        uint32 ToPinIdValue = 0;
        if (Blueprint->CanLinkPins(DragPinCopy.PinId, OtherPin->PinId, &FromPinId, &ToPinIdValue))
        {
            Blueprint->AddLink(FromPinId, ToPinIdValue);
        }
    }
    CommitBlueprintEdit(Blueprint);
    return true;
}

bool FLuaBlueprintEditorWidget::NodeTypeCanConnectToPendingPin(ELuaBlueprintNodeType Type) const
{
    if (PendingPinSpawnPinId == 0) return true;
    const ULuaBlueprintAsset* CurrentBlueprint = GetBlueprint();
    if (!CurrentBlueprint) return false;
    if (IsEventNode(Type) && HasNodeOfType(CurrentBlueprint, Type)) return false;
    const FLuaBlueprintPin* DragPin = CurrentBlueprint->FindPin(PendingPinSpawnPinId);
    if (!DragPin) return false;

    ULuaBlueprintAsset Scratch;
    FLuaBlueprintNode* Probe = Scratch.AddNodeOfType(Type, 0.0f, 0.0f);
    if (!Probe) return false;

    // 양방향 모두 동일한 "추천 정밀 매칭"을 적용한다(정확/숫자/wildcard/Exec).
    // 이전 코드는 출력 드래그엔 광범위 변환을, 입력 드래그엔 정확 타입만 적용해 비대칭/부정확했다.
    for (const FLuaBlueprintPin& Candidate : Probe->Pins)
    {
        if (Candidate.Kind == DragPin->Kind) continue;

        // Output 드래그 → 그 출력을 받을 수 있는 입력을 가진 노드.
        if (DragPin->Kind == ELuaBlueprintPinKind::Output && Candidate.Kind == ELuaBlueprintPinKind::Input)
        {
            if (ArePinTypesRecommendable(DragPin->Type, Candidate.Type)) return true;
        }
        // Input 드래그 → 그 입력에 줄 출력을 가진 노드.
        else if (DragPin->Kind == ELuaBlueprintPinKind::Input && Candidate.Kind == ELuaBlueprintPinKind::Output)
        {
            if (ArePinTypesRecommendable(Candidate.Type, DragPin->Type)) return true;
        }
    }
    return false;
}

FLuaBlueprintPin* FLuaBlueprintEditorWidget::FindFirstCompatiblePinOnNode(
    ULuaBlueprintAsset*     Blueprint,
    FLuaBlueprintNode&      Node,
    const FLuaBlueprintPin& DragPin
) const
{
    if (!Blueprint) return nullptr;

    // 1순위: 실제 링크 가능 + "추천 정밀 매칭"(정확/숫자/wildcard/Exec) 핀.
    //   → Float 출력을 String 입력 같은 엉뚱한 핀이 아니라 Float/Int 입력에 정확히 연결한다.
    // 2순위(fallback): 정밀 매칭은 아니지만 링크 자체는 가능한 첫 핀.
    FLuaBlueprintPin* Fallback = nullptr;
    for (FLuaBlueprintPin& Candidate : Node.Pins)
    {
        if (Candidate.Kind == DragPin.Kind) continue;
        uint32 FromPinId = 0;
        uint32 ToPinId   = 0;
        if (!Blueprint->CanLinkPins(DragPin.PinId, Candidate.PinId, &FromPinId, &ToPinId)) continue;

        const ELuaBlueprintPinType OutType = (DragPin.Kind == ELuaBlueprintPinKind::Output) ? DragPin.Type : Candidate.Type;
        const ELuaBlueprintPinType InType  = (DragPin.Kind == ELuaBlueprintPinKind::Output) ? Candidate.Type : DragPin.Type;
        if (ArePinTypesRecommendable(OutType, InType))
        {
            return &Candidate;
        }
        if (!Fallback) Fallback = &Candidate;
    }
    return Fallback;
}

void FLuaBlueprintEditorWidget::HandleVariableDropOnCanvas()
{
    // 캔버스 child window 의 전체 영역을 custom drop target 으로 사용.
    // ScreenToCanvas 변환은 다음 프레임의 ed::Begin 안에서 안전하게 처리 (bPendingVariableDrop 플래그).
    const ImGuiID ChildId = ImGui::GetCurrentWindow()->ID;
    const ImRect  ChildRect(
        ImGui::GetWindowPos(),
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y)
    );
    if (ImGui::BeginDragDropTargetCustom(ChildRect, ChildId))
    {
        if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("LuaBlueprintVariable"))
        {
            if (Payload->Data && Payload->DataSize == static_cast<int>(sizeof(FName)))
            {
                PendingVariableDropName  = *reinterpret_cast<const FName*>(Payload->Data);
                PendingVariableScreenPos = ImGui::GetMousePos();
                bPendingVariableDrop     = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void FLuaBlueprintEditorWidget::RenameVariableCascade(
    ULuaBlueprintAsset* Blueprint,
    const FName&        OldName,
    const FName&        NewName
)
{
    if (OldName == NewName) return;
    for (FLuaBlueprintNode& Node : Blueprint->GetMutableNodes())
    {
        if (Node.Type != ELuaBlueprintNodeType::GetVariable && Node.Type != ELuaBlueprintNodeType::SetVariable)
        {
            continue
                    ;
        }
        if (Node.NameValue == OldName) Node.NameValue = NewName;
    }
}

void FLuaBlueprintEditorWidget::RenameCustomLuaFunctionCascade(
    ULuaBlueprintAsset* Blueprint,
    const FName&        OldName,
    const FName&        NewName
)
{
    if (!Blueprint || OldName == NewName) return;
    uint32 RenamedFunctionNodeId = 0;
    for (const FLuaBlueprintNode& Node : Blueprint->GetNodes())
    {
        if (Node.Type == ELuaBlueprintNodeType::CustomLuaFunction && Node.NameValue == NewName)
        {
            RenamedFunctionNodeId = Node.NodeId;
            break;
        }
    }

    for (FLuaBlueprintNode& Node : Blueprint->GetMutableNodes())
    {
        if (Node.Type != ELuaBlueprintNodeType::CallCustomLuaFunction)
        {
            continue;
        }

        if (Node.IntValue != 0 && RenamedFunctionNodeId != 0 && Node.IntValue == static_cast<int32>(RenamedFunctionNodeId))
        {
            Node.NameValue = NewName;
            SyncCallCustomLuaFunctionNamePin(Node);
            continue;
        }

        if (Node.NameValue == OldName)
        {
            Node.NameValue = NewName;
            SyncCallCustomLuaFunctionNamePin(Node);
        }
    }
}

void FLuaBlueprintEditorWidget::SpawnVariableNode(
    ULuaBlueprintAsset*   Blueprint,
    ELuaBlueprintNodeType Type,
    const FName&          VariableName,
    const ImVec2&         Position
)
{
    if (Type != ELuaBlueprintNodeType::GetVariable && Type != ELuaBlueprintNodeType::SetVariable) return;

    FLuaBlueprintNode* NewNode = Blueprint->AddNodeOfType(Type, Position.x, Position.y);
    if (!NewNode) return;

    NewNode->NameValue = VariableName;
    Blueprint->RefreshNodePinTypes(*NewNode);
    if (NodeEditorContext)
    {
        FScopedNodeEditorCurrent Scope(NodeEditorContext);
        ed::SetNodePosition(ToNodeId(NewNode->NodeId), Position);
    }
    Blueprint->BumpVersion();
    CommitBlueprintEdit(Blueprint);
}

void FLuaBlueprintEditorWidget::CaptureInitialUndoSnapshot(ULuaBlueprintAsset* Blueprint)
{
    UndoStack.clear();
    RedoStack.clear();
    ClipboardNodes.clear();
    ClipboardLinks.clear();
    const TArray<uint8> Snapshot = CaptureLuaBlueprintSnapshot(Blueprint);
    if (!Snapshot.empty())
    {
        UndoStack.push_back(Snapshot);
    }
}

void FLuaBlueprintEditorWidget::CommitBlueprintEdit(ULuaBlueprintAsset* Blueprint)
{
    if (!Blueprint || bRestoringSnapshot) return;
    const TArray<uint8> Snapshot = CaptureLuaBlueprintSnapshot(Blueprint);
    if (!Snapshot.empty() && (UndoStack.empty() || UndoStack.back() != Snapshot))
    {
        UndoStack.push_back(Snapshot);
        if (UndoStack.size() > 128)
        {
            UndoStack.erase(UndoStack.begin());
        }
    }
    RedoStack.clear();
    MarkDirty();
}

void FLuaBlueprintEditorWidget::UndoBlueprintEdit(ULuaBlueprintAsset* Blueprint)
{
    if (!Blueprint || UndoStack.size() <= 1) return;
    RedoStack.push_back(UndoStack.back());
    UndoStack.pop_back();
    RestoreBlueprintSnapshot(Blueprint, UndoStack.back());
}

void FLuaBlueprintEditorWidget::RedoBlueprintEdit(ULuaBlueprintAsset* Blueprint)
{
    if (!Blueprint || RedoStack.empty()) return;
    const TArray<uint8> Snapshot = RedoStack.back();
    RedoStack.pop_back();
    UndoStack.push_back(Snapshot);
    RestoreBlueprintSnapshot(Blueprint, Snapshot);
}

bool FLuaBlueprintEditorWidget::RestoreBlueprintSnapshot(ULuaBlueprintAsset* Blueprint, const TArray<uint8>& Snapshot)
{
    if (!Blueprint || Snapshot.empty()) return false;
    bRestoringSnapshot = true;
    FMemoryArchive Loader(Snapshot, false);
    Blueprint->Serialize(Loader);
    Blueprint->Compile();
    bRestoringSnapshot = false;
    bPositionsPushed   = false;
    MarkDirty();
    return true;
}

bool FLuaBlueprintEditorWidget::GatherSelectedNodes(
    ULuaBlueprintAsset*        Blueprint,
    TArray<FLuaBlueprintNode>& OutNodes,
    TArray<FLuaBlueprintLink>& OutLinks
) const
{
    OutNodes.clear();
    OutLinks.clear();
    if (!Blueprint || !NodeEditorContext) return false;

    FScopedNodeEditorCurrent Scope(NodeEditorContext);

    ed::NodeId Selected[512];
    const int  Count = ed::GetSelectedNodes(Selected, 512);
    if (Count <= 0) return false;

    std::unordered_set<uint32> SelectedIds;
    SelectedIds.reserve(static_cast<size_t>(Count));
    for (int i = 0; i < Count; ++i)
    {
        SelectedIds.insert(NodeIdToU32(Selected[i]));
    }

    for (const FLuaBlueprintNode& Node : Blueprint->GetNodes())
    {
        if (SelectedIds.count(Node.NodeId))
        {
            FLuaBlueprintNode SnapshotNode = Node;
            const ImVec2      EditorPos    = ed::GetNodePosition(ToNodeId(Node.NodeId));
            SnapshotNode.PosX              = EditorPos.x;
            SnapshotNode.PosY              = EditorPos.y;
            OutNodes.push_back(SnapshotNode);
        }
    }

    for (const FLuaBlueprintLink& Link : Blueprint->GetLinks())
    {
        const FLuaBlueprintPin* From = Blueprint->FindPin(Link.FromPinId);
        const FLuaBlueprintPin* To   = Blueprint->FindPin(Link.ToPinId);
        if (From && To && SelectedIds.count(From->OwningNodeId) && SelectedIds.count(To->OwningNodeId))
        {
            OutLinks.push_back(Link);
        }
    }

    return !OutNodes.empty();
}

bool FLuaBlueprintEditorWidget::CloneNodeFragment(
    ULuaBlueprintAsset*              Blueprint,
    const TArray<FLuaBlueprintNode>& SourceNodes,
    const TArray<FLuaBlueprintLink>& SourceLinks,
    const ImVec2&                    TargetAnchor,
    TArray<uint32>*                  OutNewNodeIds,
    const ImVec2*                    SourceAnchorOverride
)
{
    if (!Blueprint || SourceNodes.empty()) return false;

    const ImVec2 SourceAnchor = SourceAnchorOverride ? *SourceAnchorOverride : ComputeNodeFragmentMin(SourceNodes);
    const ImVec2 Delta(TargetAnchor.x - SourceAnchor.x, TargetAnchor.y - SourceAnchor.y);

    std::unordered_map<uint32, uint32> PinIdMap;
    TArray<uint32>                     NewNodeIds;
    bool                               bChanged = false;

    // ed::SetNodePosition 호출이 필요하므로 ed 컨텍스트를 활성화. CloneNodeFragment 가
    // RenderGraph 의 ed::Begin/End 안에서 호출되는 경우(ProcessQueuedNodeEditorCommands)는
    // 이미 current 이라 no-op. 바깥(외부 API)에서 호출돼도 안전하게 동작.
    FScopedNodeEditorCurrent EdScope(NodeEditorContext);

    for (const FLuaBlueprintNode& SrcNode : SourceNodes)
    {
        if (IsEventNode(SrcNode.Type) && HasNodeOfType(Blueprint, SrcNode.Type))
        {
            continue;
        }

        const float        NewX    = SrcNode.PosX + Delta.x;
        const float        NewY    = SrcNode.PosY + Delta.y;
        FLuaBlueprintNode* NewNode = Blueprint->AddNodeOfType(SrcNode.Type, NewX, NewY);
        if (!NewNode) continue;

        NewNode->DisplayName = SrcNode.DisplayName;
        NewNode->NameValue   = SrcNode.NameValue;
        NewNode->StringValue = SrcNode.StringValue;
        NewNode->BoolValue   = SrcNode.BoolValue;
        NewNode->IntValue    = SrcNode.IntValue;
        NewNode->FloatValue  = SrcNode.FloatValue;
        NewNode->VectorValue = SrcNode.VectorValue;

        const size_t PinCount = (std::min)(NewNode->Pins.size(), SrcNode.Pins.size());
        for (size_t PinIndex = 0; PinIndex < PinCount; ++PinIndex)
        {
            const FLuaBlueprintPin& SrcPin = SrcNode.Pins[PinIndex];
            FLuaBlueprintPin&       DstPin = NewNode->Pins[PinIndex];
            DstPin.Type                    = SrcPin.Type;
            DstPin.DisplayName             = SrcPin.DisplayName;
            DstPin.DefaultBool             = SrcPin.DefaultBool;
            DstPin.DefaultInt              = SrcPin.DefaultInt;
            DstPin.DefaultFloat            = SrcPin.DefaultFloat;
            DstPin.DefaultString           = SrcPin.DefaultString;
            DstPin.DefaultVector           = SrcPin.DefaultVector;
            PinIdMap[SrcPin.PinId]         = DstPin.PinId;
        }
        // 데이터모델 PosX/PosY 만 세팅하면 RenderGraph 끝의 sync loop 가 ed 의 (0,0) 으로
        // 덮어쓴다 (새 노드는 아직 ed 에 push 안 된 상태). 즉시 ed::SetNodePosition 으로 push.
        if (NodeEditorContext)
        {
            ed::SetNodePosition(ToNodeId(NewNode->NodeId), ImVec2(NewX, NewY));
        }
        NewNodeIds.push_back(NewNode->NodeId);
        bChanged = true;
    }

    for (const FLuaBlueprintLink& SrcLink : SourceLinks)
    {
        auto FromIt = PinIdMap.find(SrcLink.FromPinId);
        auto ToIt   = PinIdMap.find(SrcLink.ToPinId);
        if (FromIt != PinIdMap.end() && ToIt != PinIdMap.end())
        {
            uint32 FromPinId = 0;
            uint32 ToPinId   = 0;
            if (Blueprint->CanLinkPins(FromIt->second, ToIt->second, &FromPinId, &ToPinId))
            {
                Blueprint->AddLink(FromPinId, ToPinId);
            }
        }
    }

    Blueprint->RefreshAllNodePinTypes();
    if (OutNewNodeIds)
    {
        *OutNewNodeIds = NewNodeIds;
    }
    return bChanged;
}

void FLuaBlueprintEditorWidget::SelectOnlyNodes(const TArray<uint32>& NodeIds)
{
    if (!NodeEditorContext) return;

    FScopedNodeEditorCurrent Scope(NodeEditorContext);
    ed::ClearSelection();

    bool bAppend = false;
    for (uint32 NodeId : NodeIds)
    {
        if (NodeId == 0) continue;
        ed::SelectNode(ToNodeId(NodeId), bAppend);
        bAppend = true;
    }
}

void FLuaBlueprintEditorWidget::CopySelectedNodes(ULuaBlueprintAsset* Blueprint)
{
    GatherSelectedNodes(Blueprint, ClipboardNodes, ClipboardLinks);
}

void FLuaBlueprintEditorWidget::PasteCopiedNodes(ULuaBlueprintAsset* Blueprint, const ImVec2* OverrideAnchor)
{
    if (!Blueprint || ClipboardNodes.empty()) return;

    FScopedNodeEditorCurrent Scope(NodeEditorContext);

    FLuaBlueprintNodeFragmentBounds ReferenceBounds;
    ImVec2                          Anchor               = OverrideAnchor ? *OverrideAnchor : ImVec2(0.0f, 0.0f);
    const ImVec2*                   SourceAnchorOverride = nullptr;

    if (!OverrideAnchor)
    {
        ReferenceBounds = ComputeNodeFragmentPasteReferenceBounds(ClipboardNodes, NodeEditorContext != nullptr);
        if (ReferenceBounds.bValid)
        {
            constexpr float PasteGap = 10.0f;
            Anchor                   = ImVec2(ReferenceBounds.Max.x + PasteGap, ReferenceBounds.Min.y + PasteGap);
            SourceAnchorOverride     = &ReferenceBounds.Min;
        }
    }

    if (Anchor.x == 0.0f && Anchor.y == 0.0f)
    {
        const ImVec2 SourceAnchor = ComputeNodeFragmentMin(ClipboardNodes);
        Anchor                    = ImVec2(SourceAnchor.x + 10.0f, SourceAnchor.y + 10.0f);
    }

    TArray<uint32> NewNodeIds;
    if (CloneNodeFragment(Blueprint, ClipboardNodes, ClipboardLinks, Anchor, &NewNodeIds, SourceAnchorOverride))
    {
        SelectOnlyNodes(NewNodeIds);
        bPositionsPushed = false;
        CommitBlueprintEdit(Blueprint);
    }
}

void FLuaBlueprintEditorWidget::DeleteSelectedNodes(ULuaBlueprintAsset* Blueprint)
{
    if (!Blueprint || !NodeEditorContext) return;
    FScopedNodeEditorCurrent Scope(NodeEditorContext);
    ed::NodeId               Selected[512];
    const int                Count = ed::GetSelectedNodes(Selected, 512);
    if (Count <= 0) return;

    TArray<uint32> RootNodeIds;
    RootNodeIds.reserve(static_cast<size_t>(Count));
    for (int i = 0; i < Count; ++i)
    {
        RootNodeIds.push_back(NodeIdToU32(Selected[i]));
    }

    if (DeleteNodesIncludingContainedGroups(Blueprint, RootNodeIds))
    {
        CommitBlueprintEdit(Blueprint);
    }
}

bool FLuaBlueprintEditorWidget::DeleteNodesIncludingContainedGroups(
    ULuaBlueprintAsset*   Blueprint,
    const TArray<uint32>& RootNodeIds
)
{
    if (!Blueprint || RootNodeIds.empty()) return false;

    std::unordered_set<uint32> NodeIdsToDelete;
    for (uint32 NodeId : RootNodeIds)
    {
        if (NodeId != 0)
        {
            NodeIdsToDelete.insert(NodeId);
        }
    }

    // Comment/group membership is geometric in imgui-node-editor. When a comment is removed,
    // recursively collect every node fully contained by that comment rectangle, including nested comments.
    bool bExpanded = true;
    while (bExpanded)
    {
        bExpanded = false;
        TArray<uint32> SnapshotIds;
        SnapshotIds.reserve(NodeIdsToDelete.size());
        for (uint32 NodeId : NodeIdsToDelete)
        {
            SnapshotIds.push_back(NodeId);
        }

        for (uint32 NodeId : SnapshotIds)
        {
            const FLuaBlueprintNode* CommentNode = Blueprint->FindNode(NodeId);
            if (!CommentNode || CommentNode->Type != ELuaBlueprintNodeType::Comment) continue;

            const FLuaBlueprintNodeFragmentBounds CommentBounds = ComputeLiveNodeBounds(*CommentNode, NodeEditorContext != nullptr);
            if (!CommentBounds.bValid) continue;

            for (const FLuaBlueprintNode& Candidate : Blueprint->GetNodes())
            {
                if (Candidate.NodeId == CommentNode->NodeId) continue;
                if (NodeIdsToDelete.count(Candidate.NodeId)) continue;

                const FLuaBlueprintNodeFragmentBounds CandidateBounds = ComputeLiveNodeBounds(Candidate, NodeEditorContext != nullptr);
                if (IsBoundsFullyInside(CandidateBounds, CommentBounds))
                {
                    NodeIdsToDelete.insert(Candidate.NodeId);
                    bExpanded = true;
                }
            }
        }
    }

    bool bChanged = false;
    for (uint32 NodeId : NodeIdsToDelete)
    {
        bChanged = Blueprint->RemoveNode(NodeId) || bChanged;
    }

    if (bChanged)
    {
        Blueprint->RefreshAllNodePinTypes();
        bPositionsPushed = false;
    }
    return bChanged;
}

void FLuaBlueprintEditorWidget::DuplicateSelectedNodes(ULuaBlueprintAsset* Blueprint)
{
    FScopedNodeEditorCurrent Scope(NodeEditorContext);

    TArray<FLuaBlueprintNode> SelectedNodes;
    TArray<FLuaBlueprintLink> SelectedLinks;
    if (!GatherSelectedNodes(Blueprint, SelectedNodes, SelectedLinks)) return;

    const FLuaBlueprintNodeFragmentBounds ReferenceBounds = ComputeNodeFragmentPasteReferenceBounds(SelectedNodes, NodeEditorContext != nullptr);
    const ImVec2                          SourceAnchor    = ReferenceBounds.bValid ? ReferenceBounds.Min : ComputeNodeFragmentMin(SelectedNodes);
    constexpr float                       PasteGap        = 10.0f;
    const ImVec2                          TargetAnchor    = ReferenceBounds.bValid
            ? ImVec2(ReferenceBounds.Max.x + PasteGap, ReferenceBounds.Min.y + PasteGap)
            : ImVec2(SourceAnchor.x + PasteGap, SourceAnchor.y + PasteGap);

    TArray<uint32> NewNodeIds;
    if (CloneNodeFragment(Blueprint, SelectedNodes, SelectedLinks, TargetAnchor, &NewNodeIds, &SourceAnchor))
    {
        SelectOnlyNodes(NewNodeIds);
        bPositionsPushed = false;
        CommitBlueprintEdit(Blueprint);
    }
}

void FLuaBlueprintEditorWidget::GroupSelectedNodesAsComment(ULuaBlueprintAsset* Blueprint)
{
    if (!Blueprint || !NodeEditorContext) return;
    FScopedNodeEditorCurrent Scope(NodeEditorContext);

    ed::NodeId Selected[512];
    const int  Count = ed::GetSelectedNodes(Selected, 512);
    if (Count <= 0) return;

    // ed 가 가진 위치/크기로 bounding box 계산 (데이터모델 PosX/PosY 는 한 프레임 뒤처질 수 있음).
    float MinX = FLT_MAX, MinY = FLT_MAX, MaxX = -FLT_MAX, MaxY = -FLT_MAX;
    bool  bAny = false;
    for (int i = 0; i < Count; ++i)
    {
        const uint32             NodeIdU = NodeIdToU32(Selected[i]);
        const FLuaBlueprintNode* SrcNode = Blueprint->FindNode(NodeIdU);
        if (!SrcNode) continue;

        // Existing comments/groups are now included in the bounds. This lets a new group wrap
        // nested groups instead of ignoring them and creating a too-small comment box.
        const FLuaBlueprintNodeFragmentBounds NodeBounds = ComputeLiveNodeBounds(*SrcNode, true);
        if (!NodeBounds.bValid) continue;

        bAny = true;
        MinX = (std::min)(MinX, NodeBounds.Min.x);
        MinY = (std::min)(MinY, NodeBounds.Min.y);
        MaxX = (std::max)(MaxX, NodeBounds.Max.x);
        MaxY = (std::max)(MaxY, NodeBounds.Max.y);
    }
    if (!bAny) return;

    const float  Pad    = 24.0f;
    const float  Header = 28.0f; // 위쪽에 코멘트 타이틀이 들어갈 여유
    const ImVec2 GroupPos(MinX - Pad, MinY - Pad - Header);
    const ImVec2 GroupSize((MaxX - MinX) + Pad * 2.0f, (MaxY - MinY) + Pad * 2.0f + Header);

    FLuaBlueprintNode* CommentNode = Blueprint->AddNodeOfType(
        ELuaBlueprintNodeType::Comment,
        GroupPos.x,
        GroupPos.y
    );
    if (!CommentNode) return;

    CommentNode->StringValue = "Group";
    CommentNode->VectorValue = FVector(GroupSize.x, GroupSize.y, 0.0f);

    // ed 에 즉시 push — 안 그러면 다음 sync 루프가 (0,0) 로 덮어쓴다 (CloneNodeFragment 와 같은 이유).
    ed::SetNodePosition(ToNodeId(CommentNode->NodeId), GroupPos);

    CommitBlueprintEdit(Blueprint);
}

void FLuaBlueprintEditorWidget::ProcessQueuedNodeEditorCommands(ULuaBlueprintAsset* Blueprint)
{
    if (!Blueprint || !NodeEditorContext)
    {
        bQueuedCopySelected      = false;
        bQueuedPasteNodes        = false;
        bQueuedDuplicateSelected = false;
        bQueuedDeleteSelected    = false;
        bQueuedGroupSelected     = false;
        return;
    }

    // 이 함수는 RenderGraph() 내부, ed::Begin() 이후에 호출된다.
    // 그래도 메뉴/단축키 경로가 나중에 바뀌어도 안전하도록 current editor를 다시 보장한다.
    FScopedNodeEditorCurrent Scope(NodeEditorContext);

    if (bQueuedCopySelected)
    {
        CopySelectedNodes(Blueprint);
    }
    if (bQueuedPasteNodes)
    {
        PasteCopiedNodes(Blueprint);
    }
    if (bQueuedDuplicateSelected)
    {
        DuplicateSelectedNodes(Blueprint);
    }
    if (bQueuedDeleteSelected)
    {
        DeleteSelectedNodes(Blueprint);
    }
    if (bQueuedGroupSelected)
    {
        GroupSelectedNodesAsComment(Blueprint);
    }

    bQueuedCopySelected      = false;
    bQueuedPasteNodes        = false;
    bQueuedDuplicateSelected = false;
    bQueuedDeleteSelected    = false;
    bQueuedGroupSelected     = false;
}

void FLuaBlueprintEditorWidget::RemoveVariableCascade(ULuaBlueprintAsset* Blueprint, const FName& VariableName)
{
    if (!Blueprint || VariableName == FName::None) return;

    TArray<uint32> NodesToRemove;
    for (const FLuaBlueprintNode& Node : Blueprint->GetNodes())
    {
        if ((Node.Type == ELuaBlueprintNodeType::GetVariable || Node.Type == ELuaBlueprintNodeType::SetVariable) &&
            Node.NameValue == VariableName)
        {
            NodesToRemove.push_back(Node.NodeId);
        }
    }

    for (uint32 NodeId : NodesToRemove)
    {
        Blueprint->RemoveNode(NodeId);
    }
}

void FLuaBlueprintEditorWidget::RenderInputPinConnectionStatus(ULuaBlueprintAsset* Blueprint, const FLuaBlueprintPin& Pin)
{
    if (!Blueprint || Pin.Kind != ELuaBlueprintPinKind::Input) return;
    const FLuaBlueprintLink* Link = Blueprint->FindLinkToInput(Pin.PinId);
    if (!Link) return;

    const FLuaBlueprintPin* SourcePin = Blueprint->FindPin(Link->FromPinId);
    if (!SourcePin) return;
    if (SourcePin->Type == ELuaBlueprintPinType::Exec || Pin.Type == ELuaBlueprintPinType::Exec) return;

    if (SourcePin->Type != Pin.Type && ULuaBlueprintAsset::ArePinTypesCompatibleForLink(SourcePin->Type, Pin.Type))
    {
        ImGui::TextDisabled("auto %s -> %s", PinTypeLabel(SourcePin->Type), PinTypeLabel(Pin.Type));
    }
    else
    {
        ImGui::TextDisabled("linked");
    }
}