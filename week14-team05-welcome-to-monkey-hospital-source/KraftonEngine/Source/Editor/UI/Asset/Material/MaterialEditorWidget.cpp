#include "Editor/UI/Asset/Material/MaterialEditorWidget.h"

#include "Editor/UI/Util/EditorTextureManager.h"
#include "Editor/UI/Util/EditorFileUtils.h"

#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/Object.h"
#include "Platform/Paths.h"
#include "Serialization/MemoryArchive.h"
#include "Texture/Texture2D.h"

#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "Runtime/Engine.h"
#include "Slate/SlateApplication.h"
#include "Viewport/Viewport.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_node_editor.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace ed = ax::NodeEditor;

namespace
{
    constexpr const char* DefaultSurfaceShaderPath = "Shaders/Geometry/UberLit.hlsl";

    EMaterialGraphTarget ResolveEditorGraphTarget(const UMaterial* Material)
    {
        if (!Material)
        {
            return EMaterialGraphTarget::Surface;
        }

        switch (Material->GetDomain())
        {
        case EMaterialDomain::Decal:
            return EMaterialGraphTarget::Decal;
        case EMaterialDomain::PostProcess:
            return EMaterialGraphTarget::PostProcess;
        case EMaterialDomain::UI:
        case EMaterialDomain::Surface:
        default:
            return Material->GetGraphDocument().Target;
        }
    }

    uint32 FindPinId(const FMaterialGraphNode* Node, EMaterialGraphPinKind Kind, const char* Name)
    {
        if (!Node) return 0;

        for (const FMaterialGraphPin& Pin : Node->Pins)
        {
            if (Pin.Kind == Kind && Pin.DisplayName.ToString() == Name)
            {
                return Pin.PinId;
            }
        }
        return 0;
    }

    uint32 FindOutputInputPinId(const FMaterialGraph& Graph, const char* Name)
    {
        return FindPinId(Graph.FindFirstNodeOfType(EMaterialGraphNodeType::Output), EMaterialGraphPinKind::Input, Name);
    }

    uint32 AddConstantNode(FMaterialGraph& Graph, EMaterialGraphNodeType Type, const FVector4& Value, float X, float Y, EMaterialGraphTarget Target)
    {
        FMaterialGraphNode* Node = Graph.AddNodeOfType(Type, X, Y, Target);
        if (!Node) return 0;

        Node->Value = Value;
        return FindPinId(Node, EMaterialGraphPinKind::Output, "Value");
    }

    uint32 AddTextureSampleNode(FMaterialGraph& Graph, const FString& TexturePath, EMaterialTextureSlot Slot, const char* ParameterName, float Y, EMaterialGraphTarget Target)
    {
        FMaterialGraphNode* TextureObject = Graph.AddNodeOfType(EMaterialGraphNodeType::TextureObject, -720.0f, Y, Target);
        const uint32 TextureOut = FindPinId(TextureObject, EMaterialGraphPinKind::Output, "Texture");
        if (TextureObject)
        {
            TextureObject->ParameterName = ParameterName;
            TextureObject->TextureSlot   = Slot;
            TextureObject->TexturePath   = TexturePath;
        }

        FMaterialGraphNode* Sample = Graph.AddNodeOfType(EMaterialGraphNodeType::TextureSample, -480.0f, Y, Target);
        const uint32 SampleTextureIn = FindPinId(Sample, EMaterialGraphPinKind::Input, "Texture");
        const uint32 SampleRgbOut    = FindPinId(Sample, EMaterialGraphPinKind::Output, "RGB");
        if (TextureOut && SampleTextureIn)
        {
            Graph.AddLink(TextureOut, SampleTextureIn);
        }
        return SampleRgbOut;
    }

    bool BuildGraphFromRuntimeSurfaceMaterial(UMaterial* Material, EMaterialGraphTarget Target)
    {
        if (!Material || Material->IsGraphMaterial())
        {
            return false;
        }
        if (Material->GetDomain() != EMaterialDomain::Surface ||
            Material->GetShaderPathForSerialize() != DefaultSurfaceShaderPath)
        {
            return false;
        }

        FMaterialGraph Graph;

        FVector4 SectionColor(1.0f, 1.0f, 1.0f, 1.0f);
        Material->GetVector4Parameter("SectionColor", SectionColor);

        float Opacity = 1.0f;
        Material->GetScalarParameter("Opacity", Opacity);

        UTexture2D* DiffuseTexture = nullptr;
        UTexture2D* NormalTexture  = nullptr;
        Material->GetTextureParameter("DiffuseTexture", DiffuseTexture);
        Material->GetTextureParameter("NormalTexture", NormalTexture);

        uint32 BaseColorOut = 0;
        if (DiffuseTexture && !DiffuseTexture->GetSourcePath().empty())
        {
            const uint32 DiffuseOut = AddTextureSampleNode(Graph, DiffuseTexture->GetSourcePath(), EMaterialTextureSlot::Diffuse, "DiffuseTexture", -160.0f, Target);
            const uint32 ColorOut = AddConstantNode(Graph, EMaterialGraphNodeType::ConstantFloat3,
                FVector4(SectionColor.X, SectionColor.Y, SectionColor.Z, 0.0f), -480.0f, 160.0f, Target);

            FMaterialGraphNode* Multiply = Graph.AddNodeOfType(EMaterialGraphNodeType::Multiply, -200.0f, -80.0f, Target);
            const uint32 MulAIn = FindPinId(Multiply, EMaterialGraphPinKind::Input, "A");
            const uint32 MulBIn = FindPinId(Multiply, EMaterialGraphPinKind::Input, "B");
            BaseColorOut = FindPinId(Multiply, EMaterialGraphPinKind::Output, "Result");
            if (DiffuseOut && MulAIn) Graph.AddLink(DiffuseOut, MulAIn);
            if (ColorOut && MulBIn) Graph.AddLink(ColorOut, MulBIn);
        }
        else
        {
            BaseColorOut = AddConstantNode(Graph, EMaterialGraphNodeType::ConstantFloat3,
                FVector4(SectionColor.X, SectionColor.Y, SectionColor.Z, 0.0f), -480.0f, -160.0f, Target);
        }

        uint32 NormalOut = 0;
        if (NormalTexture && !NormalTexture->GetSourcePath().empty())
        {
            NormalOut = AddTextureSampleNode(Graph, NormalTexture->GetSourcePath(), EMaterialTextureSlot::Normal, "NormalTexture", 20.0f, Target);
        }

        const uint32 OpacityOut = AddConstantNode(Graph, EMaterialGraphNodeType::ConstantFloat,
            FVector4(Opacity, 0.0f, 0.0f, 0.0f), -240.0f, 220.0f, Target);

        Graph.AddNodeOfType(EMaterialGraphNodeType::Output, 80.0f, 40.0f, Target);

        const uint32 BaseColorIn = FindOutputInputPinId(Graph, "BaseColor");
        const uint32 NormalIn    = FindOutputInputPinId(Graph, "Normal");
        const uint32 OpacityIn   = FindOutputInputPinId(Graph, "Opacity");

        if (BaseColorOut && BaseColorIn) Graph.AddLink(BaseColorOut, BaseColorIn);
        if (NormalOut && NormalIn) Graph.AddLink(NormalOut, NormalIn);
        if (OpacityOut && OpacityIn) Graph.AddLink(OpacityOut, OpacityIn);

        Graph.RepairPinsForDomain(Target);

        Material->EnableGraphMaterial();
        Material->GetGraphDocument().Target = Target;
        Material->GetGraphDocument().Graph  = Graph;
        Material->MarkGraphSaved();
        return true;
    }
}

namespace
{
    constexpr uint32 MaterialEditorSnapshotMagic   = 0x4D455331; // MES1
    constexpr uint32 MaterialEditorSnapshotVersion = 1;

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

    struct FScopedNodeEditorCurrent
    {
        ed::EditorContext* Previous = nullptr;
        ed::EditorContext* Desired  = nullptr;

        explicit FScopedNodeEditorCurrent(ed::EditorContext* InDesired) : Desired(InDesired)
        {
            Previous = ed::GetCurrentEditor();
            if (Desired && Previous != Desired) ed::SetCurrentEditor(Desired);
        }

        ~FScopedNodeEditorCurrent()
        {
            if (Desired && Previous != Desired) ed::SetCurrentEditor(Previous);
        }
    };

    FString JsonEscape(const FString& In)
    {
        FString Out;
        Out.reserve(In.size() + 8);
        for (char C : In)
        {
            switch (C)
            {
            case '\\': Out += "\\\\"; break;
            case '"':  Out += "\\\""; break;
            case '\n': Out += "\\n"; break;
            case '\r': Out += "\\r"; break;
            case '\t': Out += "\\t"; break;
            default:   Out.push_back(C); break;
            }
        }
        return Out;
    }

    FString BytesToHex(const TArray<uint8>& Bytes)
    {
        static constexpr char Hex[] = "0123456789ABCDEF";
        FString Out;
        Out.resize(Bytes.size() * 2);
        for (size_t i = 0; i < Bytes.size(); ++i)
        {
            Out[i * 2 + 0] = Hex[(Bytes[i] >> 4) & 0xF];
            Out[i * 2 + 1] = Hex[Bytes[i] & 0xF];
        }
        return Out;
    }

    int HexValue(char C)
    {
        if (C >= '0' && C <= '9') return C - '0';
        if (C >= 'a' && C <= 'f') return 10 + (C - 'a');
        if (C >= 'A' && C <= 'F') return 10 + (C - 'A');
        return -1;
    }

    bool HexToBytes(const FString& HexText, TArray<uint8>& OutBytes)
    {
        if ((HexText.size() % 2) != 0) return false;
        OutBytes.clear();
        OutBytes.reserve(HexText.size() / 2);
        for (size_t i = 0; i < HexText.size(); i += 2)
        {
            const int Hi = HexValue(HexText[i]);
            const int Lo = HexValue(HexText[i + 1]);
            if (Hi < 0 || Lo < 0) return false;
            OutBytes.push_back(static_cast<uint8>((Hi << 4) | Lo));
        }
        return true;
    }

    bool ExtractJsonStringField(const FString& Text, const char* FieldName, FString& OutValue)
    {
        const FString Key = FString("\"") + FieldName + "\"";
        size_t Pos = Text.find(Key);
        if (Pos == FString::npos) return false;
        Pos = Text.find(':', Pos + Key.size());
        if (Pos == FString::npos) return false;
        Pos = Text.find('"', Pos + 1);
        if (Pos == FString::npos) return false;
        ++Pos;

        OutValue.clear();
        bool bEscape = false;
        for (; Pos < Text.size(); ++Pos)
        {
            const char C = Text[Pos];
            if (bEscape)
            {
                switch (C)
                {
                case 'n': OutValue.push_back('\n'); break;
                case 'r': OutValue.push_back('\r'); break;
                case 't': OutValue.push_back('\t'); break;
                default:  OutValue.push_back(C); break;
                }
                bEscape = false;
                continue;
            }
            if (C == '\\')
            {
                bEscape = true;
                continue;
            }
            if (C == '"') return true;
            OutValue.push_back(C);
        }
        return false;
    }

    ImVec2 ToolbarButtonSize()
    {
        return ImVec2(92.0f, 0.0f);
    }

    bool ToolbarButton(const char* Label)
    {
        return ImGui::Button(Label, ToolbarButtonSize());
    }

    ImVec4 PinTypeColor(EMaterialGraphPinType Type)
    {
        switch (Type)
        {
        case EMaterialGraphPinType::Float:
            return ImVec4(0.90f, 0.90f, 0.30f, 1.0f);
        case EMaterialGraphPinType::Float2:
        case EMaterialGraphPinType::UV:
            return ImVec4(0.35f, 0.85f, 0.95f, 1.0f);
        case EMaterialGraphPinType::Float3:
            return ImVec4(0.35f, 0.95f, 0.45f, 1.0f);
        case EMaterialGraphPinType::Float4:
        case EMaterialGraphPinType::Color:
            return ImVec4(0.95f, 0.55f, 0.30f, 1.0f);
        case EMaterialGraphPinType::Texture2D:
            return ImVec4(0.80f, 0.55f, 1.0f, 1.0f);
        case EMaterialGraphPinType::Bool:
            return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
        default:
            return ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
        }
    }

    const char* NodeLabel(EMaterialGraphNodeType Type)
    {
        return ToString(Type);
    }

    // UE Material editor 의 카테고리별 헤더 컬러 컨벤션. 노드 분류를 한눈에 구분한다.
    ImVec4 NodeHeaderColor(EMaterialGraphNodeType Type)
    {
        switch (Type)
        {
        case EMaterialGraphNodeType::Output:
            return ImVec4(0.92f, 0.46f, 0.40f, 1.0f);

        case EMaterialGraphNodeType::ScalarParameter:
        case EMaterialGraphNodeType::VectorParameter:
        case EMaterialGraphNodeType::ColorParameter:
            return ImVec4(0.52f, 0.92f, 0.62f, 1.0f);

        case EMaterialGraphNodeType::ConstantFloat:
        case EMaterialGraphNodeType::ConstantFloat2:
        case EMaterialGraphNodeType::ConstantFloat3:
        case EMaterialGraphNodeType::ConstantFloat4:
            return ImVec4(0.45f, 0.83f, 0.95f, 1.0f);

        case EMaterialGraphNodeType::TextureObject:
        case EMaterialGraphNodeType::TextureSample:
            return ImVec4(0.78f, 0.56f, 1.00f, 1.0f);

        case EMaterialGraphNodeType::Add:
        case EMaterialGraphNodeType::Subtract:
        case EMaterialGraphNodeType::Multiply:
        case EMaterialGraphNodeType::Divide:
        case EMaterialGraphNodeType::OneMinus:
        case EMaterialGraphNodeType::Saturate:
        case EMaterialGraphNodeType::Clamp:
        case EMaterialGraphNodeType::Power:
        case EMaterialGraphNodeType::Lerp:
        case EMaterialGraphNodeType::Append:
        case EMaterialGraphNodeType::ComponentMask:
        case EMaterialGraphNodeType::ConstantBiasScale:
        case EMaterialGraphNodeType::Distance:
        case EMaterialGraphNodeType::Normalize:
        case EMaterialGraphNodeType::Dot:
        case EMaterialGraphNodeType::Cross:
            return ImVec4(0.76f, 0.66f, 1.00f, 1.0f);

        case EMaterialGraphNodeType::MakeFloat2:
        case EMaterialGraphNodeType::MakeFloat3:
        case EMaterialGraphNodeType::MakeFloat4:
        case EMaterialGraphNodeType::BreakFloat2:
        case EMaterialGraphNodeType::BreakFloat3:
        case EMaterialGraphNodeType::BreakFloat4:
            return ImVec4(0.40f, 0.85f, 0.95f, 1.0f);

        case EMaterialGraphNodeType::TexCoord:
        case EMaterialGraphNodeType::Panner:
        case EMaterialGraphNodeType::Time:
            return ImVec4(0.95f, 0.85f, 0.40f, 1.0f);

        case EMaterialGraphNodeType::VertexColor:
        case EMaterialGraphNodeType::ParticleColor:
        case EMaterialGraphNodeType::ParticleSubUV:
        case EMaterialGraphNodeType::DynamicParameter:
            return ImVec4(0.96f, 0.64f, 0.34f, 1.0f);

        case EMaterialGraphNodeType::Reroute:
        case EMaterialGraphNodeType::Comment:
        default:
            return ImVec4(0.82f, 0.82f, 0.82f, 1.0f);
        }
    }

    const char* NodeCategoryLabel(EMaterialGraphNodeType Type)
    {
        switch (Type)
        {
        case EMaterialGraphNodeType::Output:
            return "Output";
        case EMaterialGraphNodeType::ScalarParameter:
        case EMaterialGraphNodeType::VectorParameter:
        case EMaterialGraphNodeType::ColorParameter:
            return "Parameter";
        case EMaterialGraphNodeType::ConstantFloat:
        case EMaterialGraphNodeType::ConstantFloat2:
        case EMaterialGraphNodeType::ConstantFloat3:
        case EMaterialGraphNodeType::ConstantFloat4:
            return "Constant";
        case EMaterialGraphNodeType::TextureObject:
        case EMaterialGraphNodeType::TextureSample:
            return "Texture";
        case EMaterialGraphNodeType::TexCoord:
        case EMaterialGraphNodeType::Panner:
        case EMaterialGraphNodeType::Time:
            return "Coordinates";
        case EMaterialGraphNodeType::VertexColor:
        case EMaterialGraphNodeType::ParticleColor:
        case EMaterialGraphNodeType::ParticleSubUV:
        case EMaterialGraphNodeType::DynamicParameter:
            return "Particle";
        case EMaterialGraphNodeType::MakeFloat2:
        case EMaterialGraphNodeType::MakeFloat3:
        case EMaterialGraphNodeType::MakeFloat4:
        case EMaterialGraphNodeType::BreakFloat2:
        case EMaterialGraphNodeType::BreakFloat3:
        case EMaterialGraphNodeType::BreakFloat4:
            return "Conversion";
        case EMaterialGraphNodeType::Reroute:
        case EMaterialGraphNodeType::Comment:
            return "Utility";
        default:
            return "Math";
        }
    }

    // 한 프레임 동안 연결되어 있는 pin 집합 — 핀 아이콘을 채워진 원/빈 원으로 구분하기 위함.
    bool IsPinLinked(const FMaterialGraph& Graph, uint32 PinId)
    {
        for (const FMaterialGraphLink& Link : Graph.Links)
        {
            if (Link.FromPinId == PinId || Link.ToPinId == PinId) return true;
        }
        return false;
    }

    const FMaterialGraphNode* FindConnectedTextureObjectNode(const FMaterialGraph& Graph, const FMaterialGraphNode& SampleNode)
    {
        if (SampleNode.Type != EMaterialGraphNodeType::TextureSample) return nullptr;

        const FMaterialGraphPin* TextureInput = nullptr;
        for (const FMaterialGraphPin& Pin : SampleNode.Pins)
        {
            if (Pin.Kind == EMaterialGraphPinKind::Input && Pin.DisplayName.ToString() == "Texture")
            {
                TextureInput = &Pin;
                break;
            }
        }
        if (!TextureInput) return nullptr;

        for (const FMaterialGraphLink& Link : Graph.Links)
        {
            if (Link.ToPinId != TextureInput->PinId) continue;
            const FMaterialGraphPin* FromPin = Graph.FindPin(Link.FromPinId);
            if (!FromPin) return nullptr;
            const FMaterialGraphNode* FromNode = Graph.FindNode(FromPin->OwningNodeId);
            if (FromNode && FromNode->Type == EMaterialGraphNodeType::TextureObject)
            {
                return FromNode;
            }
            return nullptr;
        }
        return nullptr;
    }

    // UE 스타일 핀 아이콘: 연결되면 채워진 원, 비연결이면 테두리만. "●" 글리프 대신 draw-list 사용.
    void DrawPinIcon(const ImVec4& Color, bool bConnected)
    {
        const float  Diameter = ImGui::GetTextLineHeight() * 0.85f;
        const ImVec2 Size(Diameter, Diameter);
        const ImVec2 P        = ImGui::GetCursorScreenPos();
        ImDrawList*  DrawList = ImGui::GetWindowDrawList();
        const ImVec2 Center(P.x + Size.x * 0.5f, P.y + Size.y * 0.5f);
        const float  Radius = Size.x * 0.40f;
        const ImU32  Col    = ImGui::ColorConvertFloat4ToU32(Color);
        if (bConnected)
        {
            DrawList->AddCircleFilled(Center, Radius, Col, 12);
        }
        else
        {
            DrawList->AddCircle(Center, Radius, Col, 12, 1.6f);
            DrawList->AddCircleFilled(Center, Radius * 0.45f, ImGui::ColorConvertFloat4ToU32(ImVec4(Color.x, Color.y, Color.z, 0.30f)), 8);
        }
        ImGui::Dummy(Size);
    }

    // 비대화형 컬러 스와치 — 노드 안에서는 ColorEdit 의 picker 팝업이 캔버스 변환 때문에
    // 엉뚱한 위치에 뜨므로, 노드 본문은 읽기 전용 스와치만 그리고 편집은 인스펙터에서 한다.
    void DrawColorSwatch(const FVector4& Color, float Size = 0.0f)
    {
        const float  S        = Size > 0.0f ? Size : ImGui::GetFrameHeight();
        const ImVec2 P        = ImGui::GetCursorScreenPos();
        ImDrawList*  DrawList = ImGui::GetWindowDrawList();
        const ImU32  Fill     = ImGui::ColorConvertFloat4ToU32(ImVec4(Color.X, Color.Y, Color.Z, 1.0f));
        DrawList->AddRectFilled(P, ImVec2(P.x + S * 2.4f, P.y + S), Fill, 3.0f);
        DrawList->AddRect(P, ImVec2(P.x + S * 2.4f, P.y + S), IM_COL32(0, 0, 0, 160), 3.0f);
        ImGui::Dummy(ImVec2(S * 2.4f, S));
    }

    bool StringContainsInsensitive(const char* Haystack, const char* Needle)
    {
        if (!Needle || !Needle[0]) return true;
        if (!Haystack) return false;
        FString H = Haystack;
        FString N = Needle;
        std::transform(
            H.begin(),
            H.end(),
            H.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            }
        );
        std::transform(
            N.begin(),
            N.end(),
            N.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            }
        );
        return H.find(N) != FString::npos;
    }

    ImVec2 ComputeNodeFragmentMin(const TArray<FMaterialGraphNode>& Nodes)
    {
        ImVec2 Min(FLT_MAX, FLT_MAX);
        for (const FMaterialGraphNode& Node : Nodes)
        {
            Min.x = min(Min.x, Node.PosX);
            Min.y = min(Min.y, Node.PosY);
        }
        if (Min.x == FLT_MAX) return ImVec2(0, 0);
        return Min;
    }

    bool InputFString(const char* Label, FString& Value, size_t BufferSize = 256)
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

    FString SelectTextureFileWithDialog(const FString& CurrentPath)
    {
        std::wstring InitialDir = FPaths::AssetDir();
        if (!CurrentPath.empty())
        {
            std::filesystem::path Existing = std::filesystem::path(FPaths::RootDir()) / FPaths::ToWide(CurrentPath);
            if (std::filesystem::exists(Existing.parent_path()))
            {
                InitialDir = Existing.parent_path().wstring();
            }
        }

        FEditorFileDialogOptions Options;
        Options.Title = L"Select Texture";
        Options.Filter = L"Texture Files (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.dds)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.dds\0All Files (*.*)\0*.*\0";
        Options.InitialDirectory = InitialDir.c_str();
        Options.bFileMustExist = true;
        Options.bPathMustExist = true;
        Options.bReturnRelativeToProjectRoot = true;

        FString Selected = FEditorFileUtils::OpenFileDialog(Options);
        if (!Selected.empty())
        {
            std::replace(Selected.begin(), Selected.end(), '\\', '/');
            Selected = FPaths::MakeProjectRelative(Selected);
        }
        return Selected;
    }

    struct FMaterialNodeBounds
    {
        ImVec2 Min = ImVec2(0.0f, 0.0f);
        ImVec2 Max = ImVec2(0.0f, 0.0f);
        bool   bValid = false;
    };

    ImVec2 EstimateMaterialNodeSize(const FMaterialGraphNode& Node)
    {
        if (Node.Type == EMaterialGraphNodeType::Comment)
        {
            return ImVec2(max(120.0f, Node.Value.X), max(60.0f, Node.Value.Y));
        }
        if (Node.Type == EMaterialGraphNodeType::TextureObject || Node.Type == EMaterialGraphNodeType::TextureSample)
        {
            return ImVec2(210.0f, 175.0f);
        }
        if (Node.Type == EMaterialGraphNodeType::Output)
        {
            return ImVec2(220.0f, 240.0f);
        }
        return ImVec2(180.0f, 96.0f);
    }

    FMaterialNodeBounds ComputeLiveMaterialNodeBounds(const FMaterialGraphNode& Node, bool bUseEditorNodeSize)
    {
        FMaterialNodeBounds Bounds;
        ImVec2 Pos(Node.PosX, Node.PosY);
        ImVec2 Size = EstimateMaterialNodeSize(Node);
        if (bUseEditorNodeSize)
        {
            Pos = ed::GetNodePosition(ToNodeId(Node.NodeId));
            const ImVec2 EditorSize = ed::GetNodeSize(ToNodeId(Node.NodeId));
            if (EditorSize.x > 1.0f && EditorSize.y > 1.0f)
            {
                Size = EditorSize;
            }
        }
        Bounds.Min = Pos;
        Bounds.Max = ImVec2(Pos.x + Size.x, Pos.y + Size.y);
        Bounds.bValid = true;
        return Bounds;
    }

    bool IsOutputNode(const FMaterialGraph& Graph, uint32 NodeId)
    {
        const FMaterialGraphNode* Node = Graph.FindNode(NodeId);
        return Node && Node->Type == EMaterialGraphNodeType::Output;
    }
}

FMaterialEditorWidget::~FMaterialEditorWidget()
{
    DestroyContext();
}

bool FMaterialEditorWidget::CanEdit(UObject* Object) const
{
    return Cast<UMaterial>(Object) != nullptr;
}

void FMaterialEditorWidget::Open(UObject* Object)
{
    FAssetEditorWidget::Open(Object);
    EnsureContext();

    if (UMaterial* Material = GetMaterial())
    {
        if (!Material->GetGraphDocument().bEnabled)
        {
            const EMaterialGraphTarget GraphTarget = ResolveEditorGraphTarget(Material);
            if (!BuildGraphFromRuntimeSurfaceMaterial(Material, GraphTarget))
            {
                Material->EnableGraphMaterial();
            }
        }
        FMaterialGraphDocument& Doc = Material->GetGraphDocument();
        const EMaterialGraphTarget GraphTarget = ResolveEditorGraphTarget(Material);
        if (Doc.Target != GraphTarget)
        {
            Doc.Target = GraphTarget;
        }
        WorkingGraph = Material->GetGraphDocument().Graph;
        if (!WorkingGraph.HasOutputNode())
        {
            WorkingGraph.InitializeDefault(GraphTarget);
        }
        WorkingGraph.RepairPinsForDomain(GraphTarget);
        // ax::NodeEditor 는 settings 재로딩 public API 가 없으므로, 머티리얼이 바뀔 때마다 컨텍스트를
        // 새로 만들면서 LoadSettings 콜백이 새 Material 의 EditorSettings 를 읽게 한다.
        DestroyContext();
        EnsureContext();

        bPositionsPushed = false;
        // 저장된 JSON 이 있으면 거기에 zoom/pan/positions 가 들어있으므로 fit-content 를 건너뛴다.
        bPendingInitialContentFit = Material->GetGraphDocument().EditorSettings.empty();
        CaptureInitialUndoSnapshot();

        SetupPreviewScene();
    }
}

void FMaterialEditorWidget::Close()
{
    TeardownPreviewScene();
    PreviewMaterial = nullptr;
    DestroyContext();
    FAssetEditorWidget::Close();
}

void FMaterialEditorWidget::Tick(float DeltaTime)
{
    if (!bPreviewSceneReady || !PreviewViewportClient.IsRenderable())
    {
        return;
    }

    // 매 프레임 orbit 보정 — 사용자 회전/줌(거리)은 그대로 유지하고, LookAt 타겟·재배치만 메시 위치로 강제.
    // ResetCameraToPreviewBounds 는 Open 시 한 번뿐. 매 프레임 reset 은 사용자 입력을 덮어쓰니 안 쓴다.
    if (PreviewMeshActor)
    {
        PreviewViewportClient.OrbitCameraAroundTarget(PreviewMeshActor->GetActorLocation());
    }
    PreviewViewportClient.Tick(DeltaTime);
}

void FMaterialEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
    if (IsOpen() && bPreviewSceneReady)
    {
        OutClients.push_back(const_cast<FStaticMeshEditorViewportClient*>(&PreviewViewportClient));
    }
}

void FMaterialEditorWidget::SetupPreviewScene()
{
    if (bPreviewSceneReady) return;

    static int32 GNextInstance = 0;
    InstanceId = GNextInstance++;
    PreviewWorldHandle = FName("MaterialEditorPreview_" + std::to_string(InstanceId));

    FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
    if (!WorldContext.World) return;
    WorldContext.World->SetWorldType(EWorldType::EditorPreview);
    WorldContext.World->InitWorld();

    // Sphere with the preview material applied — same recipe as StaticMesh editor.
    PreviewMeshActor = WorldContext.World->SpawnActor<AActor>();
    if (PreviewMeshActor)
    {
        PreviewMeshComp = PreviewMeshActor->AddComponent<UStaticMeshComponent>();
        if (PreviewMeshComp)
        {
            PreviewMeshComp->SetStaticMeshByPath("Content/Data/BasicShape/Sphere.OBJ");
            PreviewMeshActor->SetRootComponent(PreviewMeshComp);
        }
        PreviewMeshActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
    }

    if (ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>())
    {
        LightActor->InitDefaultComponents();
        LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
        if (UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>())
        {
            LightComp->SetShadowBias(0.0f);
            LightComp->PushToScene();
        }
    }

    PreviewViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), 320, 240);
    PreviewViewportClient.SetPreviewWorld(WorldContext.World);
    PreviewViewportClient.SetPreviewActor(PreviewMeshActor);
    PreviewViewportClient.SetPreviewMeshComponent(PreviewMeshComp);
    PreviewViewportClient.ResetCameraToPreviewBounds();

    WorldContext.World->SetEditorPOVProvider(&PreviewViewportClient);
    FSlateApplication::Get().RegisterViewport(&PreviewViewportClient);

    bPreviewSceneReady = true;
}

void FMaterialEditorWidget::TeardownPreviewScene()
{
    if (!bPreviewSceneReady) return;

    if (UWorld* PreviewWorld = PreviewViewportClient.GetPreviewWorld())
    {
        FScene& PreviewScene = PreviewWorld->GetScene();
        GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);
        PreviewWorld->SetEditorPOVProvider(nullptr);
        if (PreviewWorldHandle.IsValid())
        {
            GEngine->DestroyWorldContext(PreviewWorldHandle);
        }
    }

    FSlateApplication::Get().UnregisterViewport(&PreviewViewportClient);
    PreviewViewportClient.Release();

    PreviewMeshActor      = nullptr;
    PreviewMeshComp       = nullptr;
    AppliedPreviewMaterial = nullptr;
    bPreviewSceneReady    = false;
}

void FMaterialEditorWidget::ApplyPreviewMaterialToMesh()
{
    if (!PreviewMeshComp) return;
    UMaterial* TargetMaterial = PreviewMaterial ? PreviewMaterial : GetMaterial();
    if (TargetMaterial && TargetMaterial != AppliedPreviewMaterial)
    {
        PreviewMeshComp->SetMaterial(0, TargetMaterial);
        AppliedPreviewMaterial = TargetMaterial;
    }
}

void FMaterialEditorWidget::Render(float DeltaTime)
{
    RenderDocument(DeltaTime);
}

void FMaterialEditorWidget::RenderDocument(float DeltaTime)
{
    (void)DeltaTime;
    UMaterial* Material = GetMaterial();
    if (!Material)
    {
        Close();
        return;
    }

    EnsureContext();
    QueueNodeEditorShortcuts();

    RenderToolbar(Material);

    const float LeftWidth   = 280.0f;
    const float RightWidth  = 360.0f;
    const float Spacing     = ImGui::GetStyle().ItemSpacing.x;
    const float Height      = ImGui::GetContentRegionAvail().y;
    const float TotalWidth  = ImGui::GetContentRegionAvail().x;
    const float CanvasWidth = max(320.0f, TotalWidth - LeftWidth - RightWidth - Spacing * 2.0f);

    // ── Left column: Settings + Parameters + Palette ──
    ImGui::BeginChild("MaterialLeftPanel", ImVec2(LeftWidth, Height), ImGuiChildFlags_Borders);
    RenderLeftPanel(Material);
    ImGui::EndChild();

    // ── Center: node graph canvas ──
    ImGui::SameLine();
    ImGui::BeginChild("MaterialGraphCanvas", ImVec2(CanvasWidth, Height), ImGuiChildFlags_Borders);
    RenderGraphCanvas(Material);
    ImGui::EndChild();

    // ── Right column: node inspector (top) + compiler results (bottom) ──
    ImGui::SameLine();
    ImGui::BeginChild("MaterialRightPanel", ImVec2(RightWidth, Height), ImGuiChildFlags_Borders);
    const float InnerHeight    = ImGui::GetContentRegionAvail().y;
    const float CompilerHeight = min(240.0f, InnerHeight * 0.42f);
    ImGui::BeginChild("MaterialDetails", ImVec2(0, InnerHeight - CompilerHeight - ImGui::GetStyle().ItemSpacing.y), ImGuiChildFlags_None);
    RenderDetailsPanel(Material);
    ImGui::EndChild();
    ImGui::BeginChild("MaterialCompiler", ImVec2(0, 0), ImGuiChildFlags_Borders);
    RenderCompilerPanel(Material);
    ImGui::EndChild();
    ImGui::EndChild();
}

bool FMaterialEditorWidget::IsEditingObject(UObject* Object) const
{
    return IsOpen() && GetEditedObject() == Object;
}

FString FMaterialEditorWidget::GetDocumentTitle() const
{
    if (UMaterial* Material = GetMaterial())
    {
        return Material->GetAssetPathFileName().empty() ? FString("Material") : Material->GetAssetPathFileName();
    }
    return "Material";
}

FString FMaterialEditorWidget::GetDocumentPayloadId() const
{
    if (UMaterial* Material = GetMaterial())
    {
        return Material->GetAssetPathFileName();
    }
    return "Material";
}

void FMaterialEditorWidget::AddReferencedObjects(FReferenceCollector& Collector)
{
    FAssetEditorWidget::AddReferencedObjects(Collector);
    Collector.AddReferencedObject(PreviewMaterial, "FMaterialEditorWidget::PreviewMaterial");
}

void FMaterialEditorWidget::EnsureContext()
{
    if (NodeEditorContext) return;

    // ax::NodeEditor 의 view state(JSON: zoom/pan/selection/...)는 디스크 파일이 아닌 머티리얼 .uasset
    // 의 FMaterialGraphDocument::EditorSettings 에 저장한다. Load/Save 콜백이 그 문자열을 읽고 쓴다.
    ed::Config Config;
    Config.SettingsFile = nullptr;
    Config.UserPointer  = this;

    Config.LoadSettings = [](char* OutData, void* UserPointer) -> size_t
    {
        auto* Self = static_cast<FMaterialEditorWidget*>(UserPointer);
        if (!Self) return 0;
        UMaterial* Material = Self->GetMaterial();
        const FString& Settings = Material ? Material->GetGraphDocument().EditorSettings : Self->PendingLoadSettings;
        if (OutData) std::memcpy(OutData, Settings.data(), Settings.size());
        return Settings.size();
    };

    Config.SaveSettings = [](const char* Data, size_t Size, ed::SaveReasonFlags /*Reason*/, void* UserPointer) -> bool
    {
        auto* Self = static_cast<FMaterialEditorWidget*>(UserPointer);
        if (!Self || !Self->GetMaterial()) return false;
        UMaterial* Material = Self->GetMaterial();
        FString NewSettings(Data, Size);
        if (NewSettings != Material->GetGraphDocument().EditorSettings)
        {
            Material->GetGraphDocument().EditorSettings = std::move(NewSettings);
            // ax 가 위치를 자기 store 에 저장했다는 뜻 — 우리 PosX/Y 도 이미 매 프레임 sync 되므로 dirty 만 마크.
            Self->MarkDirty();
        }
        return true;
    };

    NodeEditorContext = ed::CreateEditor(&Config);
}

void FMaterialEditorWidget::DestroyContext()
{
    if (NodeEditorContext)
    {
        ed::DestroyEditor(NodeEditorContext);
        NodeEditorContext = nullptr;
    }
}

UMaterial* FMaterialEditorWidget::GetMaterial() const
{
    return Cast<UMaterial>(EditedObject);
}

void FMaterialEditorWidget::RenderToolbar(UMaterial* Material)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));

    // Two primary actions: Compile updates the live preview; Save persists source + runtime.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.46f, 0.36f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.48f, 0.20f, 1.0f));
    if (ToolbarButton("Compile")) CompilePreview(Material);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compile the graph and refresh the preview. Does not save.");

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.50f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.64f, 0.34f, 1.0f));
    if (ToolbarButton(IsDirty() ? "Save *" : "Save")) SaveAll(Material);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save graph source, compile, and bind the runtime shader to this material.");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImGui::BeginDisabled(UndoStack.size() <= 1);
    if (ToolbarButton("Undo")) UndoGraphEdit();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(RedoStack.empty());
    if (ToolbarButton("Redo")) RedoGraphEdit();
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ToolbarButton("Add Node"))
    {
        // ed 외부에서 호출되는 경로 — screen 좌표 (마우스 위치) 를 캡쳐해서 AddNodeMenuItem 의 표준
        // 변환 경로를 그대로 사용한다.
        PendingNewNodeScreenPos = ImGui::GetMousePos();
        PendingNewNodePosition  = ImVec2(160.0f, 120.0f); // fallback (no ed transform)
        AddNodeSearchBuf[0]     = 0;
        ImGui::OpenPopup("MaterialAddNodePopup");
    }

    ImGui::SameLine();
    if (ToolbarButton("Export"))
    {
        const FString OutPath = FString(FPaths::ToUtf8(FPaths::RootDir())) + "/Saved/MaterialGraph_Debug.json";
        ExportGraphToJsonFile(Material, OutPath);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export the current working graph to Saved/MaterialGraph_Debug.json. Default Save still embeds graph data in the .uasset.");

    ImGui::SameLine();
    if (ToolbarButton("Import"))
    {
        const FString InPath = FString(FPaths::ToUtf8(FPaths::RootDir())) + "/Saved/MaterialGraph_Debug.json";
        ImportGraphFromJsonFile(Material, InPath);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import Saved/MaterialGraph_Debug.json exported by this editor. Existing working graph is replaced only after a valid snapshot is found.");

    ImGui::SameLine();
    bool bAutoPreview = Material->GetGraphDocument().bAutoPreview;
    if (ImGui::Checkbox("Live", &bAutoPreview))
    {
        Material->GetGraphDocument().bAutoPreview = bAutoPreview;
        MarkDirty();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto re-compile the preview on every edit.");

    // Right-aligned compile status badge.
    if (!LastCompileStatus.empty() || !LastCompileError.empty())
    {
        const char*  Status = !LastCompileError.empty() ? "Compile Error" : (bLastCompileOk ? "Compiled" : "Ready");
        const ImVec4 Col    = !LastCompileError.empty() ? ImVec4(0.95f, 0.40f, 0.32f, 1.0f)
                : bLastCompileOk ? ImVec4(0.45f, 0.90f, 0.55f, 1.0f)
                : ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
        const float TextW = ImGui::CalcTextSize(Status).x;
        ImGui::SameLine(max(0.0f, ImGui::GetWindowWidth() - TextW - 24.0f));
        ImGui::TextColored(Col, "%s", Status);
    }

    ImGui::PopStyleVar();
    ImGui::Separator();

    RenderAddNodePopup(Material);
}

namespace
{
    const char* ValueTypeLabel(EMaterialValueType Type)
    {
        switch (Type)
        {
        case EMaterialValueType::Float:
            return "Float";
        case EMaterialValueType::Float2:
            return "Float2";
        case EMaterialValueType::Float3:
            return "Float3";
        case EMaterialValueType::Float4:
            return "Float4";
        case EMaterialValueType::Color:
            return "Color";
        case EMaterialValueType::Bool:
            return "Bool";
        case EMaterialValueType::Texture2D:
            return "Texture2D";
        }
        return "Float";
    }

    const char* BlendModeLabel(EBlendMode Mode)
    {
        switch (Mode)
        {
        case EBlendMode::Opaque:
            return "Opaque";
        case EBlendMode::Masked:
            return "Masked";
        case EBlendMode::Transparent:
            return "Transparent";
        case EBlendMode::Additive:
            return "Additive";
        case EBlendMode::Modulate:
            return "Modulate";
        default:
            return "Opaque";
        }
    }

    const char* ShadingModelLabel(EMaterialShadingModel Model)
    {
        switch (Model)
        {
        case EMaterialShadingModel::Unlit:
            return "Unlit";
        case EMaterialShadingModel::DefaultLit:
            return "Default Lit";
        case EMaterialShadingModel::Phong:
            return "Phong";
        case EMaterialShadingModel::Lambert:
            return "Lambert";
        default:
            return "Default Lit";
        }
    }

    // 생성된 HLSL 파일을 디스크에서 읽어 뷰어에 표시. 패널이 펼쳐졌을 때만 호출(경로 변경 시 1회).
    FString ReadGeneratedHlsl(const FString& ProjectRelativePath)
    {
        const std::filesystem::path AbsolutePath = std::filesystem::path(FPaths::RootDir()) / FPaths::ToWide(ProjectRelativePath);
        std::ifstream               File(AbsolutePath);
        if (!File.is_open())
        {
            return FString("// Generated shader not found on disk:\n// ") + ProjectRelativePath;
        }
        std::stringstream Buffer;
        Buffer << File.rdbuf();
        return Buffer.str();
    }
}

void FMaterialEditorWidget::RenderLeftPanel(UMaterial* Material)
{
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.24f, 0.26f, 0.32f, 1.0f));
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderSettingsPanel(Material);
    }
    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderParametersPanel(Material);
    }
    if (ImGui::CollapsingHeader("Palette", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderPalettePanel(Material);
    }
    ImGui::PopStyleColor();
}

void FMaterialEditorWidget::RenderSettingsPanel(UMaterial* Material)
{
    FMaterialGraphDocument& Doc      = Material->GetGraphDocument();
    FMaterialSettings&      Settings = Material->GetMaterialSettings();

    // Labels are drawn above each control so they never clip in the narrow sidebar.
    auto LabeledCombo = [](const char* Label, const char* Preview) -> bool
    {
        ImGui::TextUnformatted(Label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        FString Id = FString("##") + Label;
        return ImGui::BeginCombo(Id.c_str(), Preview);
    };

    if (LabeledCombo("Target", ToString(Doc.Target)))
    {
        const EMaterialGraphTarget Targets[] = {
            EMaterialGraphTarget::Surface, EMaterialGraphTarget::Decal, EMaterialGraphTarget::PostProcess,
            EMaterialGraphTarget::ParticleSprite, EMaterialGraphTarget::ParticleMesh,
        };
        for (EMaterialGraphTarget Candidate : Targets)
        {
            const bool bSelected = (Candidate == Doc.Target);
            if (ImGui::Selectable(ToString(Candidate), bSelected))
            {
                Doc.Target = Candidate;
                WorkingGraph.RebuildOutputPinsForDomain(Candidate);
                CommitGraphEdit();
            }
            if (bSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    EMaterialDomain Domain      = Material->GetDomain();
    const char*     DomainLabel = Domain == EMaterialDomain::Surface ? "Surface" : Domain == EMaterialDomain::PostProcess ? "PostProcess" : Domain == EMaterialDomain::UI ? "UI" : "Decal";
    if (LabeledCombo("Domain", DomainLabel))
    {
        struct
        {
            EMaterialDomain Domain;
            const char*     Label;
        } Choices[] = {
            { EMaterialDomain::Surface, "Surface" }, { EMaterialDomain::PostProcess, "PostProcess" },
            { EMaterialDomain::UI, "UI" }, { EMaterialDomain::Decal, "Decal" },
        };
        for (const auto& Choice : Choices)
        {
            if (ImGui::Selectable(Choice.Label, Domain == Choice.Domain))
            {
                Material->SetDomainBlend(Choice.Domain, Material->GetBlendMode());
                Material->GetMaterialSettings().Domain = Choice.Domain;
                Material->GetMaterialSettings().BlendMode = Material->GetBlendMode();
                if (Choice.Domain == EMaterialDomain::Decal && Doc.Target != EMaterialGraphTarget::Decal)
                {
                    Doc.Target = EMaterialGraphTarget::Decal;
                    WorkingGraph.RebuildOutputPinsForDomain(Doc.Target);
                }
                else if (Choice.Domain == EMaterialDomain::PostProcess && Doc.Target != EMaterialGraphTarget::PostProcess)
                {
                    Doc.Target = EMaterialGraphTarget::PostProcess;
                    WorkingGraph.RebuildOutputPinsForDomain(Doc.Target);
                }
                else if (Choice.Domain == EMaterialDomain::Surface &&
                    (Doc.Target == EMaterialGraphTarget::Decal || Doc.Target == EMaterialGraphTarget::PostProcess))
                {
                    Doc.Target = EMaterialGraphTarget::Surface;
                    WorkingGraph.RebuildOutputPinsForDomain(Doc.Target);
                }
                MarkMaterialSourceEdited();
            }
        }
        ImGui::EndCombo();
    }

    EBlendMode Blend = Material->GetBlendMode();
    if (LabeledCombo("Blend Mode", BlendModeLabel(Blend)))
    {
        const EBlendMode Modes[] = { EBlendMode::Opaque, EBlendMode::Masked, EBlendMode::Transparent, EBlendMode::Additive, EBlendMode::Modulate };
        for (EBlendMode Mode : Modes)
        {
            if (ImGui::Selectable(BlendModeLabel(Mode), Blend == Mode))
            {
                Material->SetDomainBlend(Material->GetDomain(), Mode);
                Material->GetMaterialSettings().Domain = Material->GetDomain();
                Material->GetMaterialSettings().BlendMode = Mode;
                MarkMaterialSourceEdited();
            }
        }
        ImGui::EndCombo();
    }

    if (LabeledCombo("Shading Model", ShadingModelLabel(Settings.ShadingModel)))
    {
        const EMaterialShadingModel Models[] = { EMaterialShadingModel::Unlit, EMaterialShadingModel::DefaultLit, EMaterialShadingModel::Phong, EMaterialShadingModel::Lambert };
        for (EMaterialShadingModel Model : Models)
        {
            if (ImGui::Selectable(ShadingModelLabel(Model), Settings.ShadingModel == Model))
            {
                Settings.ShadingModel = Model;
                MarkMaterialSourceEdited();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    bool bTwoSided = Material->IsTwoSided();
    if (ImGui::Checkbox("Two Sided", &bTwoSided))
    {
        Material->SetTwoSided(bTwoSided);
        Settings.bTwoSided = bTwoSided;
        MarkMaterialSourceEdited();
    }
    if (ImGui::Checkbox("Receive Lighting", &Settings.bReceiveLighting)) MarkMaterialSourceEdited();
    if (ImGui::Checkbox("Cast Shadow", &Settings.bCastShadow)) MarkMaterialSourceEdited();

    if (Blend == EBlendMode::Masked)
    {
        ImGui::TextUnformatted("Mask Clip");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##MaskClip", &Settings.OpacityMaskClipValue, 0.0f, 1.0f, "%.3f")) MarkMaterialSourceEdited();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Resolved Runtime State");
    ImGui::TextDisabled("Pass       : %s", RenderStateStrings::ToString(RenderStateStrings::RenderPassMap, Material->GetRenderPass()));
    ImGui::TextDisabled("Blend      : %s", RenderStateStrings::ToString(RenderStateStrings::BlendStateMap, Material->GetBlendState()));
    ImGui::TextDisabled("Depth      : %s", RenderStateStrings::ToString(RenderStateStrings::DepthStencilStateMap, Material->GetDepthStencilState()));
    ImGui::TextDisabled("Rasterizer : %s", RenderStateStrings::ToString(RenderStateStrings::RasterizerStateMap, Material->GetRasterizerState()));
    ImGui::TextDisabled("HLSL target: %s", ToString(Doc.Target));
}


void FMaterialEditorWidget::MarkMaterialSourceEdited(bool bAutoPreview)
{
    if (!bRestoringSnapshot)
    {
        UndoStack.push_back(MakeGraphSnapshot());
        if (UndoStack.size() > 128) UndoStack.erase(UndoStack.begin());
        RedoStack.clear();
    }

    MarkDirty();

    if (UMaterial* Material = GetMaterial())
    {
        Material->GetGraphDocument().bPreviewDirty = true;
        LastCompileError.clear();
        LastCompileStatus = "Material source changed. Compile preview or save all to apply.";
        bLastCompileOk = false;

        if (bAutoPreview && Material->GetGraphDocument().bAutoPreview)
        {
            CompilePreview(Material);
        }
    }
}

void FMaterialEditorWidget::SyncParameterDefinitionToWorkingGraph(const FString& OldName, const FMaterialParameterDefinition& Definition)
{
    const FString& NewName = Definition.Name;
    for (FMaterialGraphNode& Node : WorkingGraph.Nodes)
    {
        const bool bParameterNode =
            Node.Type == EMaterialGraphNodeType::ScalarParameter ||
            Node.Type == EMaterialGraphNodeType::VectorParameter ||
            Node.Type == EMaterialGraphNodeType::ColorParameter ||
            Node.Type == EMaterialGraphNodeType::TextureObject;
        if (!bParameterNode)
        {
            continue;
        }

        if ((!OldName.empty() && Node.ParameterName == OldName) || Node.ParameterName == NewName)
        {
            Node.ParameterName = NewName;
            switch (Definition.Type)
            {
            case EMaterialValueType::Float:
                if (Node.Type == EMaterialGraphNodeType::ScalarParameter)
                {
                    Node.Value = Definition.DefaultValue;
                }
                break;
            case EMaterialValueType::Color:
                if (Node.Type == EMaterialGraphNodeType::ColorParameter)
                {
                    Node.Value = Definition.DefaultValue;
                }
                break;
            case EMaterialValueType::Float3:
            case EMaterialValueType::Float4:
                if (Node.Type == EMaterialGraphNodeType::VectorParameter)
                {
                    Node.Value = Definition.DefaultValue;
                }
                break;
            case EMaterialValueType::Texture2D:
                if (Node.Type == EMaterialGraphNodeType::TextureObject)
                {
                    Node.TexturePath = Definition.DefaultTexturePath;
                }
                break;
            default:
                break;
            }
        }
    }
}

void FMaterialEditorWidget::SyncParameterDefinitionsToWorkingGraph(UMaterial* Material)
{
    if (!Material)
    {
        return;
    }

    for (const FMaterialParameterDefinition& Definition : Material->GetParameterDefinitions())
    {
        SyncParameterDefinitionToWorkingGraph(Definition.Name, Definition);
    }
}

void FMaterialEditorWidget::RenderParametersPanel(UMaterial* Material)
{
    FMaterialGraphDocument&               Doc  = Material->GetGraphDocument();
    TArray<FMaterialParameterDefinition>& Defs = Material->GetParameterDefinitions();

    // Adds both a definition and a matching parameter node, wired to a stable name.
    auto AddParameter = [&](const char* BaseName, EMaterialValueType Type, EMaterialGraphNodeType NodeType, const FVector4& Default)
    {
        FString Name    = BaseName;
        int32   Suffix  = 1;
        bool    bUnique = false;
        while (!bUnique)
        {
            bUnique = true;
            for (const FMaterialParameterDefinition& D : Defs) if (D.Name == Name)
            {
                Name    = FString(BaseName) + std::to_string(Suffix++);
                bUnique = false;
                break;
            }
        }
        FMaterialParameterDefinition Def;
        Def.Name         = Name;
        Def.Type         = Type;
        Def.DefaultValue = Default;
        Defs.push_back(Def);
        if (FMaterialGraphNode* Node = WorkingGraph.AddNodeOfType(NodeType, PendingNewNodePosition.x, PendingNewNodePosition.y, Doc.Target))
        {
            Node->ParameterName = Name;
            Node->Value         = Default;
        }
        CommitGraphEdit();
    };

    if (Defs.empty())
    {
        ImGui::TextDisabled("No exposed parameters.");
    }

    int32 RemoveIndex = -1;
    for (int32 i = 0; i < static_cast<int32>(Defs.size()); ++i)
    {
        FMaterialParameterDefinition& Def = Defs[i];
        ImGui::PushID(i);
        const bool bOpen = ImGui::TreeNodeEx(
            "##param",
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding,
            "%s  (%s)",
            Def.Name.empty() ? "(unnamed)" : Def.Name.c_str(),
            ValueTypeLabel(Def.Type)
        );
        if (bOpen)
        {
            char NameBuf[128];
            std::snprintf(NameBuf, sizeof(NameBuf), "%s", Def.Name.c_str());
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##Name", NameBuf, sizeof(NameBuf)))
            {
                const FString OldName = Def.Name;
                Def.Name = NameBuf;
                SyncParameterDefinitionToWorkingGraph(OldName, Def);
                MarkMaterialSourceEdited();
            }

            ImGui::TextUnformatted("Default");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (Def.Type == EMaterialValueType::Float)
            {
                if (ImGui::DragFloat("##Default", &Def.DefaultValue.X, 0.01f))
                {
                    SyncParameterDefinitionToWorkingGraph(Def.Name, Def);
                    MarkMaterialSourceEdited();
                }
            }
            else if (Def.Type == EMaterialValueType::Color)
            {
                if (ImGui::ColorEdit4("##Default", &Def.DefaultValue.X, ImGuiColorEditFlags_NoInputs))
                {
                    SyncParameterDefinitionToWorkingGraph(Def.Name, Def);
                    MarkMaterialSourceEdited();
                }
            }
            else if (Def.Type == EMaterialValueType::Texture2D)
            {
                if (InputFString("##Default", Def.DefaultTexturePath, 512))
                {
                    SyncParameterDefinitionToWorkingGraph(Def.Name, Def);
                    MarkMaterialSourceEdited();
                }
            }
            else
            {
                if (ImGui::DragFloat4("##Default", &Def.DefaultValue.X, 0.01f))
                {
                    SyncParameterDefinitionToWorkingGraph(Def.Name, Def);
                    MarkMaterialSourceEdited();
                }
            }

            char GroupBuf[96];
            std::snprintf(GroupBuf, sizeof(GroupBuf), "%s", Def.Group.c_str());
            ImGui::TextUnformatted("Group");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##Group", GroupBuf, sizeof(GroupBuf)))
            {
                Def.Group = GroupBuf;
                MarkMaterialSourceEdited();
            }
            if (ImGui::Checkbox("Expose to Instance", &Def.bExposeToInstance)) MarkMaterialSourceEdited();

            if (ImGui::SmallButton("Remove")) RemoveIndex = i;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (RemoveIndex >= 0)
    {
        Defs.erase(Defs.begin() + RemoveIndex);
        MarkMaterialSourceEdited();
    }

    ImGui::Spacing();
    if (ImGui::SmallButton("+ Scalar")) AddParameter("Scalar", EMaterialValueType::Float, EMaterialGraphNodeType::ScalarParameter, FVector4(0, 0, 0, 0));
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Vector")) AddParameter("Vector", EMaterialValueType::Float4, EMaterialGraphNodeType::VectorParameter, FVector4(0, 0, 0, 1));
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Color")) AddParameter("Color", EMaterialValueType::Color, EMaterialGraphNodeType::ColorParameter, FVector4(1, 1, 1, 1));
}

void FMaterialEditorWidget::RenderPalettePanel(UMaterial* Material)
{
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##palettesearch", "Search nodes...", PaletteSearchBuf, sizeof(PaletteSearchBuf));

    struct FPaletteItem
    {
        const char*            Category;
        EMaterialGraphNodeType Type;
    };
    static const FPaletteItem Items[] = {
        { "Constants", EMaterialGraphNodeType::ConstantFloat },
        { "Constants", EMaterialGraphNodeType::ConstantFloat2 },
        { "Constants", EMaterialGraphNodeType::ConstantFloat3 },
        { "Constants", EMaterialGraphNodeType::ConstantFloat4 },
        { "Parameters", EMaterialGraphNodeType::ScalarParameter },
        { "Parameters", EMaterialGraphNodeType::VectorParameter },
        { "Parameters", EMaterialGraphNodeType::ColorParameter },
        { "Texture", EMaterialGraphNodeType::TextureObject },
        { "Texture", EMaterialGraphNodeType::TextureSample },
        { "Math", EMaterialGraphNodeType::Add },
        { "Math", EMaterialGraphNodeType::Subtract },
        { "Math", EMaterialGraphNodeType::Multiply },
        { "Math", EMaterialGraphNodeType::Divide },
        { "Math", EMaterialGraphNodeType::Lerp },
        { "Math", EMaterialGraphNodeType::Clamp },
        { "Math", EMaterialGraphNodeType::Power },
        { "Math", EMaterialGraphNodeType::OneMinus },
        { "Math", EMaterialGraphNodeType::Saturate },
        { "Math", EMaterialGraphNodeType::ConstantBiasScale },
        { "Math", EMaterialGraphNodeType::Distance },
        { "Math", EMaterialGraphNodeType::Normalize },
        { "Math", EMaterialGraphNodeType::Dot },
        { "Math", EMaterialGraphNodeType::Cross },
        { "Math", EMaterialGraphNodeType::Append },
        { "Math", EMaterialGraphNodeType::ComponentMask },
        { "Conversion", EMaterialGraphNodeType::MakeFloat2 },
        { "Conversion", EMaterialGraphNodeType::MakeFloat3 },
        { "Conversion", EMaterialGraphNodeType::MakeFloat4 },
        { "Conversion", EMaterialGraphNodeType::BreakFloat2 },
        { "Conversion", EMaterialGraphNodeType::BreakFloat3 },
        { "Conversion", EMaterialGraphNodeType::BreakFloat4 },
        { "Coordinates", EMaterialGraphNodeType::TexCoord },
        { "Coordinates", EMaterialGraphNodeType::Panner },
        { "Coordinates", EMaterialGraphNodeType::Time },
        { "Particle", EMaterialGraphNodeType::VertexColor },
        { "Particle", EMaterialGraphNodeType::ParticleColor },
        { "Particle", EMaterialGraphNodeType::ParticleSubUV },
        { "Particle", EMaterialGraphNodeType::DynamicParameter },
        { "Utility", EMaterialGraphNodeType::Reroute },
        { "Utility", EMaterialGraphNodeType::Comment },
    };

    const char* CurrentCategory = nullptr;
    const bool  bSearching      = PaletteSearchBuf[0] != 0;
    for (const FPaletteItem& Item : Items)
    {
        const char* Label = NodeLabel(Item.Type);
        if (bSearching && !StringContainsInsensitive(Label, PaletteSearchBuf)) continue;

        if (!CurrentCategory || std::strcmp(CurrentCategory, Item.Category) != 0)
        {
            CurrentCategory = Item.Category;
            ImGui::Spacing();
            ImGui::TextDisabled("%s", Item.Category);
            ImGui::Separator();
        }

        ImGui::PushStyleColor(ImGuiCol_Text, NodeHeaderColor(Item.Type));
        ImGui::Bullet();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Selectable(Label))
        {
            // Palette 는 ed 컨텍스트 외부 호출 — graph 좌표로 직접 stagger 한다.
            // 캔버스에 들어오면 ed::ReloadEditorSettings 에 따라 ax 가 이미 자리잡은 위치를 우선 적용.
            const ImVec2 Spawn = ImVec2(PendingNewNodePosition.x + 30.0f, PendingNewNodePosition.y + 30.0f);
            if (FMaterialGraphNode* Node = WorkingGraph.AddNodeOfType(Item.Type, Spawn.x, Spawn.y, Material->GetGraphDocument().Target))
            {
                Node->PosX = Spawn.x;
                Node->PosY = Spawn.y;
                if (NodeEditorContext)
                {
                    FScopedNodeEditorCurrent Scope(NodeEditorContext);
                    ed::SetNodePosition(ToNodeId(Node->NodeId), Spawn);
                }
                PendingNewNodePosition = Spawn;
                SelectOnlyNodes(TArray<uint32>{ Node->NodeId });
                CommitGraphEdit();
            }
        }
    }
}

void FMaterialEditorWidget::RenderGraphCanvas(UMaterial* Material)
{
    if (!NodeEditorContext) return;

    FScopedNodeEditorCurrent Scope(NodeEditorContext);
    ed::Begin("MaterialGraph");

    ProcessQueuedNodeEditorCommands();

    for (FMaterialGraphNode& Node : WorkingGraph.Nodes)
    {
        if (!bPositionsPushed)
        {
            ed::SetNodePosition(ToNodeId(Node.NodeId), ImVec2(Node.PosX, Node.PosY));
        }

        // Comment 는 ed::Group 으로 — 내부에 겹친 다른 노드들을 같이 드래그/리사이즈한다 (UE BP Comment).
        if (Node.Type == EMaterialGraphNodeType::Comment)
        {
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(118, 96, 30, 70));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(224, 198, 96, 200));
            ed::BeginNode(ToNodeId(Node.NodeId));
            ImGui::PushID(static_cast<int>(Node.NodeId));
            ImGui::TextColored(ImVec4(1.0f, 0.93f, 0.5f, 1.0f), "%s", Node.ParameterName.empty() ? "Comment" : Node.ParameterName.c_str());
            const ImVec2 GroupSize(max(120.0f, Node.Value.X), max(60.0f, Node.Value.Y));
            ed::Group(GroupSize);
            ImGui::PopID();
            ed::EndNode();

            const ImVec2 ActualSize = ed::GetNodeSize(ToNodeId(Node.NodeId));
            if (ActualSize.x > 1.0f && ActualSize.y > 1.0f &&
                (std::fabs(Node.Value.X - ActualSize.x) > 0.5f || std::fabs(Node.Value.Y - ActualSize.y) > 0.5f))
            {
                Node.Value.X = ActualSize.x;
                Node.Value.Y = ActualSize.y;
                MarkDirty();
            }
            ed::PopStyleColor(2);
            continue;
        }

        ed::BeginNode(ToNodeId(Node.NodeId));
        RenderNodeBody(Node);
        ed::EndNode();
    }

    bPositionsPushed = true;

    for (const FMaterialGraphLink& Link : WorkingGraph.Links)
    {
        ImVec4 LinkColor(0.75f, 0.75f, 0.75f, 1.0f);
        if (const FMaterialGraphPin* From = WorkingGraph.FindPin(Link.FromPinId))
        {
            LinkColor = PinTypeColor(From->Type);
        }
        ed::Link(ToLinkId(Link.LinkId), ToPinId(Link.FromPinId), ToPinId(Link.ToPinId), LinkColor);
    }

    if (bPendingInitialContentFit && !WorkingGraph.Nodes.empty())
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
                uint32 FromPinId = 0;
                uint32 ToPinId   = 0;
                if (WorkingGraph.CanLinkPins(PinIdToU32(StartId), PinIdToU32(EndId), &FromPinId, &ToPinId))
                {
                    if (ed::AcceptNewItem())
                    {
                        WorkingGraph.AddLink(FromPinId, ToPinId);
                        CommitGraphEdit();
                    }
                }
                else
                {
                    ed::RejectNewItem(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), 2.0f);
                }
            }
        }

        ed::PinId NewNodePinId = 0;
        if (ed::QueryNewNode(&NewNodePinId))
        {
            if (NewNodePinId && ed::AcceptNewItem(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), 1.5f))
            {
                PendingPinSpawnPinId = PinIdToU32(NewNodePinId);
                // 표준 패턴 — popup OpenPopup 시점에는 SCREEN 좌표만 저장하고, 노드 생성 직후 그 자리에서
                // ed::ScreenToCanvas + ed::SetNodePosition 으로 변환·적용한다 (imgui-node-editor 공식 예제와 동일).
                PendingNewNodeScreenPos = ImGui::GetMousePos();
                PinSpawnSearchBuf[0]    = 0;
                bShowPinSpawnMenu       = true; // popup is opened inside the Suspend block below
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
                if (WorkingGraph.RemoveLink(LinkIdToU32(DeletedLink))) CommitGraphEdit();
            }
        }
        ed::NodeId DeletedNode;
        bool       bDeletedNode = false;
        while (ed::QueryDeletedNode(&DeletedNode))
        {
            const uint32 NodeId = NodeIdToU32(DeletedNode);
            if (IsOutputNode(WorkingGraph, NodeId))
            {
                // Do not accept deletion of the root output node. The node-editor will keep it.
                continue;
            }
            if (ed::AcceptDeletedItem())
            {
                bDeletedNode |= WorkingGraph.RemoveNode(NodeId);
            }
        }
        if (bDeletedNode) CommitGraphEdit();
    }
    ed::EndDelete();

    // All context menus / spawn popups are opened AND begun inside one Suspend/Resume block so
    // ImGui draws them in screen space (not the node-editor's transformed canvas space).
    // 노드 생성 위치는 PendingNewNodeScreenPos(SCREEN 좌표) 에 저장만 해두고, 실제 ed::ScreenToCanvas +
    // ed::SetNodePosition 변환은 menu item 핸들러(AddNodeMenuItem) 안에서 수행한다.
    ed::NodeId ContextNodeId = 0;
    ed::LinkId ContextLinkId = 0;
    ed::Suspend();
    if (ed::ShowNodeContextMenu(&ContextNodeId))
    {
        ImGui::OpenPopup("MaterialNodeMenu");
    }
    else if (ed::ShowLinkContextMenu(&ContextLinkId))
    {
        ImGui::OpenPopup("MaterialLinkMenu");
    }
    else if (ed::ShowBackgroundContextMenu())
    {
        PendingNewNodeScreenPos = ImGui::GetMousePos();
        AddNodeSearchBuf[0]     = 0;
        ImGui::OpenPopup("MaterialCanvasAddNode");
    }

    if (ImGui::BeginPopup("MaterialNodeMenu"))
    {
        if (ImGui::MenuItem("Duplicate")) bQueuedDuplicateSelected = true;
        if (ImGui::MenuItem("Group Selection as Comment")) bQueuedGroupSelected = true;
        ImGui::Separator();
        const bool bContextIsOutput = ContextNodeId && IsOutputNode(WorkingGraph, NodeIdToU32(ContextNodeId));
        if (bContextIsOutput)
        {
            ImGui::BeginDisabled();
            ImGui::MenuItem("Delete");
            ImGui::EndDisabled();
            ImGui::TextDisabled("Material Output cannot be deleted.");
        }
        else if (ImGui::MenuItem("Delete"))
        {
            if (ContextNodeId && WorkingGraph.RemoveNode(NodeIdToU32(ContextNodeId))) CommitGraphEdit();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("MaterialLinkMenu"))
    {
        if (ImGui::MenuItem("Break Link"))
        {
            if (ContextLinkId && WorkingGraph.RemoveLink(LinkIdToU32(ContextLinkId))) CommitGraphEdit();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("MaterialCanvasAddNode"))
    {
        RenderAddNodeMenuBody(Material);
        ImGui::EndPopup();
    }

    if (bShowPinSpawnMenu)
    {
        ImGui::OpenPopup("MaterialPinSpawnPopup");
        bShowPinSpawnMenu = false;
    }
    if (ImGui::BeginPopup("MaterialPinSpawnPopup"))
    {
        RenderPinSpawnMenuBody(Material);
        ImGui::EndPopup();
    }
    ed::Resume();

    TArray<uint32> MovedNodeIds;
    for (FMaterialGraphNode& Node : WorkingGraph.Nodes)
    {
        const ImVec2 Pos = ed::GetNodePosition(ToNodeId(Node.NodeId));
        if (std::fabs(Node.PosX - Pos.x) > 0.01f || std::fabs(Node.PosY - Pos.y) > 0.01f)
        {
            Node.PosX = Pos.x;
            Node.PosY = Pos.y;
            MovedNodeIds.push_back(Node.NodeId);
            MarkDirty();
        }
    }
    if (!MovedNodeIds.empty() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ed::NodeId ExistingSelected[512];
        const int ExistingCount = ed::GetSelectedNodes(ExistingSelected, 512);
        std::unordered_set<uint32> ExistingIds;
        for (int i = 0; i < ExistingCount; ++i) ExistingIds.insert(NodeIdToU32(ExistingSelected[i]));

        bool bMovedNodeAlreadySelected = false;
        for (uint32 NodeId : MovedNodeIds)
        {
            if (ExistingIds.count(NodeId))
            {
                bMovedNodeAlreadySelected = true;
                break;
            }
        }
        if (!bMovedNodeAlreadySelected)
        {
            SelectOnlyNodes(MovedNodeIds);
        }
    }

    {
        ed::NodeId SelectedNodes[1];
        const int  Count = ed::GetSelectedNodes(SelectedNodes, 1);
        SelectedNodeId   = (Count > 0) ? NodeIdToU32(SelectedNodes[0]) : 0;
    }

    ed::End();
}

void FMaterialEditorWidget::RenderNodeBody(FMaterialGraphNode& Node)
{
    ImGui::PushID(static_cast<int>(Node.NodeId));

    // ── Header: category-colored title ──
    const ImVec4  Header = NodeHeaderColor(Node.Type);
    const FString Title  = Node.DisplayName != FName::None ? Node.DisplayName.ToString() : FString(NodeLabel(Node.Type));
    ImGui::TextColored(Header, "%s", Title.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(Header.x, Header.y, Header.z, 0.45f), "  %s", NodeCategoryLabel(Node.Type));
    ImGui::Dummy(ImVec2(0.0f, 3.0f));

    int32 NumInput  = 0;
    int32 NumOutput = 0;
    for (const FMaterialGraphPin& Pin : Node.Pins)
    {
        if (Pin.Kind == EMaterialGraphPinKind::Input) ++NumInput;
        else ++NumOutput;
    }

    const bool bHasValueEditor =
            Node.Type == EMaterialGraphNodeType::ConstantFloat || Node.Type == EMaterialGraphNodeType::ConstantFloat2 ||
            Node.Type == EMaterialGraphNodeType::ConstantFloat3 || Node.Type == EMaterialGraphNodeType::ConstantFloat4 ||
            Node.Type == EMaterialGraphNodeType::ColorParameter || Node.Type == EMaterialGraphNodeType::VectorParameter ||
            Node.Type == EMaterialGraphNodeType::ScalarParameter || Node.Type == EMaterialGraphNodeType::ComponentMask ||
            Node.Type == EMaterialGraphNodeType::TextureObject || Node.Type == EMaterialGraphNodeType::TextureSample ||
            Node.Type == EMaterialGraphNodeType::TexCoord ||
            Node.Type == EMaterialGraphNodeType::ParticleSubUV || Node.Type == EMaterialGraphNodeType::ConstantBiasScale;

    // ── Left column: input pins + inline value editor ──
    ImGui::BeginGroup();
    for (FMaterialGraphPin& Pin : Node.Pins)
    {
        if (Pin.Kind != EMaterialGraphPinKind::Input) continue;
        ed::BeginPin(ToPinId(Pin.PinId), ed::PinKind::Input);
        ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
        DrawPinIcon(PinTypeColor(Pin.Type), IsPinLinked(WorkingGraph, Pin.PinId));
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextColored(PinTypeColor(Pin.Type), "%s", Pin.DisplayName.ToString().c_str());
        ed::EndPin();
    }
    if (bHasValueEditor)
    {
        RenderNodeValueEditor(Node, true);
    }
    ImGui::EndGroup();

    // ── Right column: output pins ──
    if (NumOutput > 0)
    {
        if (NumInput > 0 || bHasValueEditor) ImGui::SameLine(0.0f, 20.0f);
        ImGui::BeginGroup();
        for (FMaterialGraphPin& Pin : Node.Pins)
        {
            if (Pin.Kind != EMaterialGraphPinKind::Output) continue;
            ed::BeginPin(ToPinId(Pin.PinId), ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
            ImGui::TextColored(PinTypeColor(Pin.Type), "%s", Pin.DisplayName.ToString().c_str());
            ImGui::SameLine(0.0f, 6.0f);
            DrawPinIcon(PinTypeColor(Pin.Type), IsPinLinked(WorkingGraph, Pin.PinId));
            ed::EndPin();
        }
        ImGui::EndGroup();
    }

    ImGui::PopID();
}

void FMaterialEditorWidget::RenderNodeValueEditor(FMaterialGraphNode& Node, bool bCompact)
{
    const float Width = bCompact ? 130.0f : -1.0f;
    ImGui::PushID(static_cast<int>(Node.NodeId) * 7 + 3);

    switch (Node.Type)
    {
    case EMaterialGraphNodeType::ScalarParameter:
    case EMaterialGraphNodeType::VectorParameter:
    case EMaterialGraphNodeType::ColorParameter:
        ImGui::TextDisabled("[%s]", Node.ParameterName.empty() ? "param" : Node.ParameterName.c_str());
        if (Node.Type == EMaterialGraphNodeType::ColorParameter)
        {
            if (bCompact) DrawColorSwatch(Node.Value);
            else if (ImGui::ColorEdit4("##v", &Node.Value.X, ImGuiColorEditFlags_NoInputs)) MarkMaterialSourceEdited();
        }
        else if (Node.Type == EMaterialGraphNodeType::ScalarParameter)
        {
            ImGui::SetNextItemWidth(Width);
            if (ImGui::DragFloat("##v", &Node.Value.X, 0.01f)) MarkMaterialSourceEdited();
        }
        else if (!bCompact) // VectorParameter, full editor in inspector only
        {
            if (ImGui::DragFloat4("##v", &Node.Value.X, 0.01f)) MarkMaterialSourceEdited();
        }
        break;
    case EMaterialGraphNodeType::ConstantFloat:
        ImGui::SetNextItemWidth(Width);
        if (ImGui::DragFloat("##v", &Node.Value.X, 0.01f)) MarkMaterialSourceEdited();
        break;
    case EMaterialGraphNodeType::ConstantFloat2:
        ImGui::SetNextItemWidth(Width);
        if (ImGui::DragFloat2("##v", &Node.Value.X, 0.01f)) MarkMaterialSourceEdited();
        break;
    case EMaterialGraphNodeType::ConstantFloat3:
        if (bCompact)
        {
            DrawColorSwatch(Node.Value);
        }
        else
        {
            if (ImGui::ColorEdit3("##v", &Node.Value.X, ImGuiColorEditFlags_NoInputs)) MarkMaterialSourceEdited();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat3("##vd", &Node.Value.X, 0.01f)) MarkMaterialSourceEdited();
        }
        break;
    case EMaterialGraphNodeType::ConstantFloat4:
        ImGui::SetNextItemWidth(Width);
        if (ImGui::DragFloat4("##v", &Node.Value.X, 0.01f)) MarkMaterialSourceEdited();
        break;
    case EMaterialGraphNodeType::ConstantBiasScale:
        ImGui::SetNextItemWidth(Width);
        if (ImGui::DragFloat2("Bias/Scale", &Node.Value.X, 0.01f)) MarkMaterialSourceEdited();
        break;
    case EMaterialGraphNodeType::TexCoord:
        ImGui::SetNextItemWidth(Width);
        if (ImGui::DragFloat("UV Ch", &Node.Value.X, 1.0f, 0.0f, 3.0f, "%.0f")) MarkMaterialSourceEdited();
        break;
    case EMaterialGraphNodeType::ParticleSubUV:
        ImGui::SetNextItemWidth(Width);
        if (ImGui::DragFloat2("Cols/Rows", &Node.Value.X, 1.0f, 1.0f, 64.0f, "%.0f")) MarkMaterialSourceEdited();
        break;
    case EMaterialGraphNodeType::ComponentMask:
    {
        char MaskBuf[8] = {};
        std::snprintf(MaskBuf, sizeof(MaskBuf), "%s", Node.Mask.c_str());
        ImGui::SetNextItemWidth(Width);
        if (ImGui::InputText("Mask", MaskBuf, sizeof(MaskBuf)))
        {
            Node.Mask = MaskBuf;
            MarkMaterialSourceEdited();
        }
        break;
    }
    case EMaterialGraphNodeType::TextureObject:
    {
        const ImVec2 ThumbSize(72.0f, 72.0f);
        const ImVec2 ThumbMin = ImGui::GetCursorScreenPos();
        const ImVec2 ThumbMax(ThumbMin.x + ThumbSize.x, ThumbMin.y + ThumbSize.y);
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        DrawList->AddRectFilled(ThumbMin, ThumbMax, IM_COL32(28, 28, 34, 255), 6.0f);
        DrawList->AddRect(ThumbMin, ThumbMax, IM_COL32(126, 88, 190, 220), 6.0f, 0, 1.5f);

        if (!Node.TexturePath.empty())
        {
            if (ID3D11ShaderResourceView* Thumb = FEditorTextureManager::Get().GetOrLoadThumbnail(Node.TexturePath))
            {
                ImGui::Image((ImTextureID)Thumb, ThumbSize);
            }
            else
            {
                ImGui::Dummy(ThumbSize);
                const ImVec2 TextSize = ImGui::CalcTextSize("Texture");
                DrawList->AddText(ImVec2(ThumbMin.x + (ThumbSize.x - TextSize.x) * 0.5f, ThumbMin.y + 28.0f), IM_COL32(180, 160, 220, 255), "Texture");
            }
        }
        else
        {
            ImGui::Dummy(ThumbSize);
            const ImVec2 TextSize = ImGui::CalcTextSize("Click");
            DrawList->AddText(ImVec2(ThumbMin.x + (ThumbSize.x - TextSize.x) * 0.5f, ThumbMin.y + 20.0f), IM_COL32(150, 150, 160, 255), "Click");
            const ImVec2 TextSize2 = ImGui::CalcTextSize("Texture");
            DrawList->AddText(ImVec2(ThumbMin.x + (ThumbSize.x - TextSize2.x) * 0.5f, ThumbMin.y + 38.0f), IM_COL32(150, 150, 160, 255), "Texture");
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Click thumbnail to choose a texture");
        }
        if (ImGui::IsItemClicked())
        {
            FString Selected = SelectTextureFileWithDialog(Node.TexturePath);
            if (!Selected.empty())
            {
                Node.TexturePath = Selected;
                MarkMaterialSourceEdited();
            }
        }

        std::filesystem::path P(Node.TexturePath);
        const FString Base = Node.TexturePath.empty() ? FString("(no texture)") : P.filename().string();
        ImGui::TextWrapped("%s", Base.c_str());
        ImGui::TextDisabled("Slot: %s", MaterialTextureSlot::ToString(static_cast<int32>(Node.TextureSlot)).c_str());
        break;
    }
    case EMaterialGraphNodeType::TextureSample:
    {
        const FMaterialGraphNode* TextureNode = FindConnectedTextureObjectNode(WorkingGraph, Node);
        if (TextureNode && !TextureNode->TexturePath.empty())
        {
            std::filesystem::path P(TextureNode->TexturePath);
            ImGui::TextDisabled("Samples: %s", P.filename().string().c_str());
        }
        else if (TextureNode)
        {
            ImGui::TextDisabled("Samples connected texture");
        }
        else
        {
            ImGui::TextDisabled("Connect Texture input");
        }
        ImGui::TextDisabled("Uses Texture + UV pins");
        break;
    }
    default:
        break;
    }

    ImGui::PopID();
}

void FMaterialEditorWidget::RenderDetailsPanel(UMaterial* Material)
{
    if (SelectedNodeId != 0)
    {
        if (FMaterialGraphNode* Node = WorkingGraph.FindNode(SelectedNodeId))
        {
            RenderNodeInspector(Material, *Node);
            return;
        }
        SelectedNodeId = 0;
    }

    ImGui::TextDisabled("Select a node to edit its details.");
    ImGui::Separator();
    ImGui::Text("Nodes: %zu", WorkingGraph.Nodes.size());
    ImGui::Text("Links: %zu", WorkingGraph.Links.size());
    ImGui::Spacing();
    ImGui::TextDisabled("Saved:   %s", Material->GetGraphDocument().LastSavedGraphHash.c_str());
    ImGui::TextDisabled("Working: %s", ComputeMaterialGraphStructuralHash(WorkingGraph).c_str());
    if (PreviewMaterial)
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "Preview material active");
    }

    ImGui::Spacing();
    if (ImGui::Button("Frame Graph"))
    {
        FScopedNodeEditorCurrent Scope(NodeEditorContext);
        ed::NavigateToContent(0.25f);
    }
}

void FMaterialEditorWidget::RenderNodeInspector(UMaterial* Material, FMaterialGraphNode& Node)
{
    const ImVec4 Header = NodeHeaderColor(Node.Type);
    ImGui::TextColored(Header, "%s", Node.DisplayName != FName::None ? Node.DisplayName.ToString().c_str() : NodeLabel(Node.Type));
    ImGui::SameLine();
    ImGui::TextDisabled("#%u  %s", Node.NodeId, NodeCategoryLabel(Node.Type));
    ImGui::Separator();

    // Parameter nodes: name binds the generated cbuffer field; keep it editable here.
    if (Node.Type == EMaterialGraphNodeType::ScalarParameter ||
        Node.Type == EMaterialGraphNodeType::VectorParameter ||
        Node.Type == EMaterialGraphNodeType::ColorParameter)
    {
        char NameBuf[128];
        std::snprintf(NameBuf, sizeof(NameBuf), "%s", Node.ParameterName.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("Parameter Name", NameBuf, sizeof(NameBuf)))
        {
            Node.ParameterName = NameBuf;
            MarkMaterialSourceEdited();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Default Value");
        RenderNodeValueEditor(Node, false);
    }
    else if (Node.Type == EMaterialGraphNodeType::TextureObject)
    {
        ImGui::TextUnformatted("Texture Object");
        ImGui::TextDisabled("Click the thumbnail in the graph node to choose the texture.");
        if (!Node.TexturePath.empty())
        {
            ImGui::TextWrapped("%s", Node.TexturePath.c_str());
        }
        else
        {
            ImGui::TextDisabled("No texture selected.");
        }

        int32 Slot = static_cast<int32>(Node.TextureSlot);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("Slot", MaterialTextureSlot::ToString(Slot).c_str()))
        {
            for (int32 s = 0; s < static_cast<int32>(EMaterialTextureSlot::Max); ++s)
            {
                if (ImGui::Selectable(MaterialTextureSlot::ToString(s).c_str(), Slot == s))
                {
                    Node.TextureSlot = static_cast<EMaterialTextureSlot>(s);
                    MarkMaterialSourceEdited();
                }
            }
            ImGui::EndCombo();
        }
    }
    else if (Node.Type == EMaterialGraphNodeType::TextureSample)
    {
        ImGui::TextUnformatted("Texture Sample");
        ImGui::TextWrapped("Samples the Texture input with the UV input and outputs RGBA/RGB/channels. This node does not own a texture asset.");
        if (const FMaterialGraphNode* TextureNode = FindConnectedTextureObjectNode(WorkingGraph, Node))
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Connected Texture Object");
            if (!TextureNode->TexturePath.empty())
            {
                ImGui::TextWrapped("%s", TextureNode->TexturePath.c_str());
            }
            else
            {
                ImGui::TextDisabled("Connected texture object has no texture selected.");
            }
        }
        else
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No Texture input connected.");
        }
    }
    else if (Node.Type == EMaterialGraphNodeType::Comment)
    {
        ImGui::SetNextItemWidth(-1);
        if (InputFString("Comment", Node.ParameterName)) MarkMaterialSourceEdited();
        if (ImGui::DragFloat2("Size", &Node.Value.X, 1.0f, 60.0f, 4000.0f, "%.0f")) MarkMaterialSourceEdited();
    }
    else
    {
        RenderNodeValueEditor(Node, false);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Pins");
    for (const FMaterialGraphPin& Pin : Node.Pins)
    {
        const bool bInput = Pin.Kind == EMaterialGraphPinKind::Input;
        ImGui::PushStyleColor(ImGuiCol_Text, PinTypeColor(Pin.Type));
        ImGui::Bullet();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("%s %s", bInput ? "In " : "Out", Pin.DisplayName.ToString().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s%s)", ToString(Pin.Type), IsPinLinked(WorkingGraph, Pin.PinId) ? ", linked" : "");
    }

    ImGui::Spacing();
    if (Node.Type == EMaterialGraphNodeType::Output)
    {
        ImGui::TextDisabled("Material Output is required and cannot be deleted.");
    }
    else if (ImGui::Button("Delete Node"))
    {
        if (WorkingGraph.RemoveNode(Node.NodeId))
        {
            SelectedNodeId = 0;
            CommitGraphEdit();
        }
    }
}

void FMaterialEditorWidget::RenderCompilerPanel(UMaterial* Material)
{
    // Top of the panel: live 3D preview of the compiled material on a lit sphere.
    RenderPreviewPanel(Material);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Compile Status");

    if (!LastCompileError.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.42f, 0.32f, 1.0f));
        ImGui::TextWrapped("Error: %s", LastCompileError.c_str());
        ImGui::PopStyleColor();
    }
    else if (!LastCompileStatus.empty())
    {
        ImGui::TextWrapped("%s", LastCompileStatus.c_str());
    }
    else
    {
        ImGui::TextDisabled("Not compiled yet.");
    }

    const FMaterialCompileRecord& Record = Material->GetLastCompileRecord();
    if (!Record.GeneratedShaderPath.empty())
    {
        std::filesystem::path P(Record.GeneratedShaderPath);
        ImGui::TextDisabled("Shader: %s", P.filename().string().c_str());
    }
}

void FMaterialEditorWidget::RenderPreviewPanel(UMaterial* Material)
{
    ImGui::TextUnformatted("Material Preview");
    if (!bPreviewSceneReady)
    {
        ImGui::TextDisabled("Preview scene unavailable.");
        return;
    }

    ApplyPreviewMaterialToMesh();

    const float Avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 Size(Avail, max(160.0f, Avail * 0.72f));
    const ImVec2 ViewportPos = ImGui::GetCursorScreenPos();

    PreviewViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);
    FViewport* VP = PreviewViewportClient.GetViewport();
    if (VP && Size.x > 0 && Size.y > 0)
    {
        VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
        if (VP->GetSRV())
        {
            ImGui::Image((ImTextureID)VP->GetSRV(), Size);
            FSlateApplication::Get().SetViewportImGuiHovered(&PreviewViewportClient, ImGui::IsItemHovered());
        }
        else
        {
            ImGui::Dummy(Size);
        }
    }
    else
    {
        ImGui::Dummy(Size);
    }

    if (PreviewMaterial)
    {
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "Showing: Preview (graph compile)");
    }
    else
    {
        ImGui::TextDisabled("Showing: Saved runtime");
    }
    (void)Material;
}

void FMaterialEditorWidget::RenderAddNodePopup(UMaterial* Material)
{
    if (ImGui::BeginPopup("MaterialAddNodePopup"))
    {
        RenderAddNodeMenuBody(Material);
        ImGui::EndPopup();
    }
}

void FMaterialEditorWidget::RenderAddNodeMenuBody(UMaterial* Material)
{
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##Search", "Search nodes...", AddNodeSearchBuf, sizeof(AddNodeSearchBuf));
    ImGui::Separator();

    struct FMenuItem
    {
        const char*            Category;
        EMaterialGraphNodeType Type;
    };
    static const FMenuItem Items[] = {
        { "Texture", EMaterialGraphNodeType::TextureObject },
        { "Texture", EMaterialGraphNodeType::TextureSample },
        { "Parameters", EMaterialGraphNodeType::ScalarParameter },
        { "Parameters", EMaterialGraphNodeType::VectorParameter },
        { "Parameters", EMaterialGraphNodeType::ColorParameter },
        { "Constants", EMaterialGraphNodeType::ConstantFloat },
        { "Constants", EMaterialGraphNodeType::ConstantFloat2 },
        { "Constants", EMaterialGraphNodeType::ConstantFloat3 },
        { "Constants", EMaterialGraphNodeType::ConstantFloat4 },
        { "Math", EMaterialGraphNodeType::Add },
        { "Math", EMaterialGraphNodeType::Subtract },
        { "Math", EMaterialGraphNodeType::Multiply },
        { "Math", EMaterialGraphNodeType::Divide },
        { "Math", EMaterialGraphNodeType::Lerp },
        { "Math", EMaterialGraphNodeType::Clamp },
        { "Math", EMaterialGraphNodeType::Power },
        { "Math", EMaterialGraphNodeType::OneMinus },
        { "Math", EMaterialGraphNodeType::Saturate },
        { "Math", EMaterialGraphNodeType::ConstantBiasScale },
        { "Math", EMaterialGraphNodeType::Distance },
        { "Math", EMaterialGraphNodeType::Normalize },
        { "Math", EMaterialGraphNodeType::Dot },
        { "Math", EMaterialGraphNodeType::Cross },
        { "Math", EMaterialGraphNodeType::ComponentMask },
        { "Math", EMaterialGraphNodeType::Append },
        { "Conversion", EMaterialGraphNodeType::MakeFloat2 },
        { "Conversion", EMaterialGraphNodeType::MakeFloat3 },
        { "Conversion", EMaterialGraphNodeType::MakeFloat4 },
        { "Conversion", EMaterialGraphNodeType::BreakFloat2 },
        { "Conversion", EMaterialGraphNodeType::BreakFloat3 },
        { "Conversion", EMaterialGraphNodeType::BreakFloat4 },
        { "Coordinates", EMaterialGraphNodeType::TexCoord },
        { "Coordinates", EMaterialGraphNodeType::Panner },
        { "Coordinates", EMaterialGraphNodeType::Time },
        { "Particle", EMaterialGraphNodeType::VertexColor },
        { "Particle", EMaterialGraphNodeType::ParticleColor },
        { "Particle", EMaterialGraphNodeType::ParticleSubUV },
        { "Particle", EMaterialGraphNodeType::DynamicParameter },
        { "Utility", EMaterialGraphNodeType::Reroute },
        { "Utility", EMaterialGraphNodeType::Comment },
    };

    // When searching, show a flat filtered list. Otherwise group into category submenus.
    if (AddNodeSearchBuf[0] != 0)
    {
        for (const FMenuItem& Item : Items)
        {
            AddNodeMenuItem(Material, Item.Type);
        }
        return;
    }

    const char* CurrentCategory = nullptr;
    bool        bMenuOpen       = false;
    for (const FMenuItem& Item : Items)
    {
        if (!CurrentCategory || std::strcmp(CurrentCategory, Item.Category) != 0)
        {
            if (CurrentCategory && bMenuOpen) ImGui::EndMenu();
            CurrentCategory = Item.Category;
            bMenuOpen       = ImGui::BeginMenu(Item.Category);
        }
        if (bMenuOpen)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NodeHeaderColor(Item.Type));
            AddNodeMenuItem(Material, Item.Type);
            ImGui::PopStyleColor();
        }
    }
    if (CurrentCategory && bMenuOpen) ImGui::EndMenu();
}

void FMaterialEditorWidget::RenderPinSpawnMenuBody(UMaterial* Material)
{
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##PinSearch", "Search compatible nodes...", PinSpawnSearchBuf, sizeof(PinSpawnSearchBuf));
    ImGui::Separator();
    // Only type-compatible nodes are listed (AddContextNodeMenuItem filters by pin compatibility).
    const EMaterialGraphNodeType Candidates[] = {
        EMaterialGraphNodeType::Multiply, EMaterialGraphNodeType::Add, EMaterialGraphNodeType::Subtract,
        EMaterialGraphNodeType::Divide, EMaterialGraphNodeType::Lerp, EMaterialGraphNodeType::OneMinus,
        EMaterialGraphNodeType::Saturate, EMaterialGraphNodeType::Clamp, EMaterialGraphNodeType::Power,
        EMaterialGraphNodeType::TextureSample, EMaterialGraphNodeType::ComponentMask, EMaterialGraphNodeType::Append,
        EMaterialGraphNodeType::Normalize, EMaterialGraphNodeType::Dot, EMaterialGraphNodeType::Reroute,
    };
    bool bAny = false;
    for (EMaterialGraphNodeType Type : Candidates)
    {
        bAny |= AddContextNodeMenuItem(Material, Type);
    }
    (void)bAny;
}

void FMaterialEditorWidget::SaveGraph(UMaterial* Material)
{
    if (!Material) return;
    SyncParameterDefinitionsToWorkingGraph(Material);
    const EMaterialGraphTarget GraphTarget = ResolveEditorGraphTarget(Material);
    WorkingGraph.RepairPinsForDomain(GraphTarget);
    Material->EnableGraphMaterial();
    Material->GetGraphDocument().Target = GraphTarget;
    Material->GetGraphDocument().Graph = WorkingGraph;
    Material->MarkGraphSaved();
    if (FMaterialManager::Get().SaveMaterialSourceOnly(Material, Material->GetAssetPathFileName()))
    {
        ClearDirty();
        LastCompileStatus = "Graph source saved.";
        LastCompileError.clear();
    }
    else
    {
        LastCompileError = "Failed to save material graph source.";
    }
}

void FMaterialEditorWidget::CompilePreview(UMaterial* Material)
{
    SyncParameterDefinitionsToWorkingGraph(Material);
    WorkingGraph.RepairPinsForDomain(ResolveEditorGraphTarget(Material));
    LastCompileError.clear();
    if (FMaterialManager::Get().CompileMaterialGraphPreview(Material, WorkingGraph, PreviewMaterial, &LastCompileError))
    {
        LastCompileStatus = "Preview compile succeeded. Source asset was not modified.";
        bLastCompileOk    = true;
    }
    else
    {
        bLastCompileOk = false;
    }
}

void FMaterialEditorWidget::ApplyCompile(UMaterial* Material, bool bPersistCompiledState)
{
    SyncParameterDefinitionsToWorkingGraph(Material);
    const EMaterialGraphTarget GraphTarget = ResolveEditorGraphTarget(Material);
    WorkingGraph.RepairPinsForDomain(GraphTarget);
    Material->GetGraphDocument().Target = GraphTarget;
    LastCompileError.clear();
    if (FMaterialManager::Get().CompileMaterialGraphRuntime(Material, WorkingGraph, bPersistCompiledState, &LastCompileError))
    {
        LastCompileStatus = bPersistCompiledState ? "Runtime compile applied and saved." : "Runtime compile applied. Graph source was not saved.";
        bLastCompileOk    = true;
    }
    else
    {
        bLastCompileOk = false;
    }
}

void FMaterialEditorWidget::SaveAll(UMaterial* Material)
{
    SaveGraph(Material);
    ApplyCompile(Material, true);
}

// ── 디버그용 JSON export/import ────────────────────────────────────────────
// 평소엔 graph(노드/링크/값) 과 view state(zoom/pan) 가 모두 .uasset 안에 들어있다.
// 이 두 함수는 머티리얼 그래프를 외부 도구로 검토하거나 재사용하기 위한 보조 경로.

void FMaterialEditorWidget::ExportGraphToJsonFile(UMaterial* Material, const FString& AbsolutePath)
{
    if (!Material) return;
    SyncParameterDefinitionsToWorkingGraph(Material);
    const FMaterialGraph&         G   = WorkingGraph;
    const FMaterialGraphDocument& Doc = Material->GetGraphDocument();
    const FString                 SnapshotHex = BytesToHex(MakeGraphSnapshot());

    std::stringstream JS;
    JS << "{\n";
    JS << "  \"format\": \"KraftonMaterialGraph\",\n";
    JS << "  \"version\": 2,\n";
    JS << "  \"target\": \"" << ToString(Doc.Target) << "\",\n";
    JS << "  \"snapshotHex\": \"" << SnapshotHex << "\",\n";
    JS << "  \"nodes\": [\n";
    for (size_t i = 0; i < G.Nodes.size(); ++i)
    {
        const FMaterialGraphNode& N = G.Nodes[i];
        JS << "    {\n";
        JS << "      \"id\": " << N.NodeId << ",\n";
        JS << "      \"type\": \"" << ToString(N.Type) << "\",\n";
        JS << "      \"displayName\": \"" << JsonEscape(N.DisplayName.ToString()) << "\",\n";
        JS << "      \"pos\": [" << N.PosX << ", " << N.PosY << "],\n";
        JS << "      \"parameter\": \"" << JsonEscape(N.ParameterName) << "\",\n";
        JS << "      \"texture\": \"" << JsonEscape(N.TexturePath) << "\",\n";
        JS << "      \"textureSlot\": \"" << ToString(N.TextureSlot) << "\",\n";
        JS << "      \"mask\": \"" << JsonEscape(N.Mask) << "\",\n";
        JS << "      \"value\": [" << N.Value.X << ", " << N.Value.Y << ", " << N.Value.Z << ", " << N.Value.W << "],\n";
        JS << "      \"pins\": [";
        for (size_t p = 0; p < N.Pins.size(); ++p)
        {
            const FMaterialGraphPin& Pin = N.Pins[p];
            JS << "{ \"id\": " << Pin.PinId
               << ", \"kind\": \"" << (Pin.Kind == EMaterialGraphPinKind::Input ? "Input" : "Output") << "\""
               << ", \"type\": \"" << ToString(Pin.Type) << "\""
               << ", \"name\": \"" << JsonEscape(Pin.DisplayName.ToString()) << "\" }";
            if (p + 1 < N.Pins.size()) JS << ", ";
        }
        JS << "]\n";
        JS << "    }" << (i + 1 < G.Nodes.size() ? "," : "") << "\n";
    }
    JS << "  ],\n";
    JS << "  \"links\": [\n";
    for (size_t i = 0; i < G.Links.size(); ++i)
    {
        const FMaterialGraphLink& L = G.Links[i];
        JS << "    { \"id\": " << L.LinkId << ", \"from\": " << L.FromPinId << ", \"to\": " << L.ToPinId << " }"
           << (i + 1 < G.Links.size() ? "," : "") << "\n";
    }
    JS << "  ]\n";
    JS << "}\n";

    const std::filesystem::path Path(FPaths::ToWide(AbsolutePath));
    std::error_code Ec;
    std::filesystem::create_directories(Path.parent_path(), Ec);

    std::ofstream File(Path, std::ios::binary | std::ios::trunc);
    if (File.is_open())
    {
        File << JS.str();
        LastCompileError.clear();
        LastCompileStatus = "Exported graph JSON to " + AbsolutePath;
        bLastCompileOk = true;
    }
    else
    {
        LastCompileStatus.clear();
        LastCompileError = "Failed to write JSON: " + AbsolutePath;
        bLastCompileOk = false;
    }
}

bool FMaterialEditorWidget::ImportGraphFromJsonFile(UMaterial* Material, const FString& AbsolutePath)
{
    if (!Material) return false;
    std::ifstream File(std::filesystem::path(FPaths::ToWide(AbsolutePath)), std::ios::binary);
    if (!File.is_open())
    {
        LastCompileStatus.clear();
        LastCompileError = "Failed to open JSON: " + AbsolutePath;
        bLastCompileOk = false;
        return false;
    }

    std::stringstream Buffer;
    Buffer << File.rdbuf();
    const FString Text = Buffer.str();

    FString SnapshotHex;
    if (!ExtractJsonStringField(Text, "snapshotHex", SnapshotHex))
    {
        LastCompileStatus.clear();
        LastCompileError = "JSON import failed: snapshotHex is missing. Re-export with the current material editor.";
        bLastCompileOk = false;
        return false;
    }

    TArray<uint8> Snapshot;
    if (!HexToBytes(SnapshotHex, Snapshot) || Snapshot.empty())
    {
        LastCompileStatus.clear();
        LastCompileError = "JSON import failed: invalid snapshotHex.";
        bLastCompileOk = false;
        return false;
    }

    CommitGraphEdit();
    if (!RestoreGraphSnapshot(Snapshot))
    {
        LastCompileStatus.clear();
        LastCompileError = "JSON import failed: graph snapshot could not be restored.";
        bLastCompileOk = false;
        return false;
    }

    Material->GetGraphDocument().Graph = WorkingGraph;
    Material->GetGraphDocument().bPreviewDirty = true;
    CommitGraphEdit();
    LastCompileError.clear();
    LastCompileStatus = "Imported graph JSON from " + AbsolutePath;
    bLastCompileOk = true;
    return true;
}

void FMaterialEditorWidget::CaptureInitialUndoSnapshot()
{
    UndoStack.clear();
    RedoStack.clear();
    UndoStack.push_back(MakeGraphSnapshot());
}

void FMaterialEditorWidget::CommitGraphEdit()
{
    if (bRestoringSnapshot) return;
    UndoStack.push_back(MakeGraphSnapshot());
    if (UndoStack.size() > 128) UndoStack.erase(UndoStack.begin());
    RedoStack.clear();
    bPositionsPushed = false;
    MarkDirty();

    if (UMaterial* Material = GetMaterial())
    {
        Material->GetGraphDocument().bPreviewDirty = true;
        if (Material->GetGraphDocument().bAutoPreview)
        {
            CompilePreview(Material);
        }
    }
}

void FMaterialEditorWidget::UndoGraphEdit()
{
    if (UndoStack.size() <= 1) return;
    TArray<uint8> Current = UndoStack.back();
    UndoStack.pop_back();
    RedoStack.push_back(Current);
    RestoreGraphSnapshot(UndoStack.back());
}

void FMaterialEditorWidget::RedoGraphEdit()
{
    if (RedoStack.empty()) return;
    TArray<uint8> Snapshot = RedoStack.back();
    RedoStack.pop_back();
    UndoStack.push_back(Snapshot);
    RestoreGraphSnapshot(Snapshot);
}

TArray<uint8> FMaterialEditorWidget::MakeGraphSnapshot() const
{
    FMaterialGraph Copy = WorkingGraph;
    FMemoryArchive Saver(true);

    uint32 Magic   = MaterialEditorSnapshotMagic;
    uint32 Version = MaterialEditorSnapshotVersion;
    Saver << Magic;
    Saver << Version;

    EMaterialGraphTarget Target = EMaterialGraphTarget::Surface;
    FMaterialSettings Settings;
    TArray<FMaterialParameterDefinition> Definitions;
    if (UMaterial* Material = GetMaterial())
    {
        Target      = Material->GetGraphDocument().Target;
        Settings    = Material->GetMaterialSettings();
        Definitions = Material->GetParameterDefinitions();
    }

    Saver << Target;
    Saver << Settings;
    Saver << Definitions;
    Saver << Copy;
    return Saver.GetBuffer();
}

bool FMaterialEditorWidget::RestoreGraphSnapshot(const TArray<uint8>& Snapshot)
{
    if (Snapshot.empty()) return false;
    bRestoringSnapshot = true;

    const bool bFullSourceSnapshot = Snapshot.size() >= sizeof(uint32)
        && *reinterpret_cast<const uint32*>(Snapshot.data()) == MaterialEditorSnapshotMagic;

    FMemoryArchive Loader(Snapshot, false);
    if (bFullSourceSnapshot)
    {
        uint32 Magic = 0;
        uint32 Version = 0;
        Loader << Magic;
        Loader << Version;

        EMaterialGraphTarget Target = EMaterialGraphTarget::Surface;
        FMaterialSettings Settings;
        TArray<FMaterialParameterDefinition> Definitions;
        FMaterialGraph Graph;

        Loader << Target;
        Loader << Settings;
        Loader << Definitions;
        Loader << Graph;

        WorkingGraph = Graph;
        if (UMaterial* Material = GetMaterial())
        {
            Material->GetGraphDocument().Target = Target;
            Material->GetParameterDefinitions() = Definitions;
            Material->SetDomainBlend(Settings.Domain, Settings.BlendMode);
            Material->SetTwoSided(Settings.bTwoSided);
            Material->GetMaterialSettings() = Settings;
            Material->GetGraphDocument().bPreviewDirty = true;
        }
    }
    else
    {
        // Backward compatibility for JSON/debug snapshots exported before source-state snapshots existed.
        Loader << WorkingGraph;
        if (UMaterial* Material = GetMaterial())
        {
            Material->GetGraphDocument().bPreviewDirty = true;
        }
    }

    bRestoringSnapshot = false;
    bPositionsPushed   = false;
    MarkDirty();
    return true;
}

bool FMaterialEditorWidget::GatherSelectedNodes(TArray<FMaterialGraphNode>& OutNodes, TArray<FMaterialGraphLink>& OutLinks) const
{
    OutNodes.clear();
    OutLinks.clear();
    if (!NodeEditorContext) return false;
    FScopedNodeEditorCurrent Scope(NodeEditorContext);

    ed::NodeId Selected[512];
    const int  Count = ed::GetSelectedNodes(Selected, 512);
    if (Count <= 0) return false;

    std::unordered_set<uint32> SelectedIds;
    for (int i = 0; i < Count; ++i) SelectedIds.insert(NodeIdToU32(Selected[i]));

    for (const FMaterialGraphNode& Node : WorkingGraph.Nodes)
    {
        if (SelectedIds.count(Node.NodeId) && Node.Type != EMaterialGraphNodeType::Output)
        {
            FMaterialGraphNode Copy = Node;
            const ImVec2       Pos  = ed::GetNodePosition(ToNodeId(Node.NodeId));
            Copy.PosX               = Pos.x;
            Copy.PosY               = Pos.y;
            OutNodes.push_back(Copy);
        }
    }

    for (const FMaterialGraphLink& Link : WorkingGraph.Links)
    {
        const FMaterialGraphPin* From = WorkingGraph.FindPin(Link.FromPinId);
        const FMaterialGraphPin* To   = WorkingGraph.FindPin(Link.ToPinId);
        if (From && To && SelectedIds.count(From->OwningNodeId) && SelectedIds.count(To->OwningNodeId))
        {
            OutLinks.push_back(Link);
        }
    }
    return !OutNodes.empty();
}

bool FMaterialEditorWidget::CloneNodeFragment(const TArray<FMaterialGraphNode>& SourceNodes, const TArray<FMaterialGraphLink>& SourceLinks, const ImVec2& TargetAnchor, TArray<uint32>* OutNewNodeIds)
{
    if (SourceNodes.empty()) return false;
    const ImVec2                       SourceAnchor = ComputeNodeFragmentMin(SourceNodes);
    const ImVec2                       Delta(TargetAnchor.x - SourceAnchor.x, TargetAnchor.y - SourceAnchor.y);
    std::unordered_map<uint32, uint32> PinIdMap;
    TArray<uint32>                     NewNodeIds;

    for (const FMaterialGraphNode& SrcNode : SourceNodes)
    {
        if (SrcNode.Type == EMaterialGraphNodeType::Output) continue;
        FMaterialGraphNode* NewNode = WorkingGraph.AddNodeOfType(SrcNode.Type, SrcNode.PosX + Delta.x, SrcNode.PosY + Delta.y, GetMaterial()->GetGraphDocument().Target);
        if (!NewNode) continue;
        NewNode->DisplayName   = SrcNode.DisplayName;
        NewNode->ParameterName = SrcNode.ParameterName;
        NewNode->TexturePath   = SrcNode.TexturePath;
        NewNode->TextureSlot   = SrcNode.TextureSlot;
        NewNode->Value         = SrcNode.Value;
        NewNode->Mask          = SrcNode.Mask;
        const size_t PinCount  = min(NewNode->Pins.size(), SrcNode.Pins.size());
        for (size_t i = 0; i < PinCount; ++i)
        {
            PinIdMap[SrcNode.Pins[i].PinId] = NewNode->Pins[i].PinId;
        }
        NewNodeIds.push_back(NewNode->NodeId);
        if (NodeEditorContext) ed::SetNodePosition(ToNodeId(NewNode->NodeId), ImVec2(NewNode->PosX, NewNode->PosY));
    }

    for (const FMaterialGraphLink& SrcLink : SourceLinks)
    {
        auto FromIt = PinIdMap.find(SrcLink.FromPinId);
        auto ToIt   = PinIdMap.find(SrcLink.ToPinId);
        if (FromIt != PinIdMap.end() && ToIt != PinIdMap.end())
        {
            uint32 FromPinId = 0;
            uint32 ToPinId   = 0;
            if (WorkingGraph.CanLinkPins(FromIt->second, ToIt->second, &FromPinId, &ToPinId))
            {
                WorkingGraph.AddLink(FromPinId, ToPinId);
            }
        }
    }

    if (OutNewNodeIds) *OutNewNodeIds = NewNodeIds;
    return !NewNodeIds.empty();
}

void FMaterialEditorWidget::SelectOnlyNodes(const TArray<uint32>& NodeIds)
{
    if (!NodeEditorContext) return;
    FScopedNodeEditorCurrent Scope(NodeEditorContext);
    ed::ClearSelection();
    bool bAppend = false;
    for (uint32 NodeId : NodeIds)
    {
        ed::SelectNode(ToNodeId(NodeId), bAppend);
        bAppend = true;
    }
}

void FMaterialEditorWidget::CopySelectedNodes()
{
    GatherSelectedNodes(ClipboardNodes, ClipboardLinks);
}

void FMaterialEditorWidget::PasteCopiedNodes(const ImVec2* OverrideAnchor)
{
    if (ClipboardNodes.empty()) return;
    ImVec2         Anchor = OverrideAnchor ? *OverrideAnchor : ImVec2(ComputeNodeFragmentMin(ClipboardNodes).x + 24.0f, ComputeNodeFragmentMin(ClipboardNodes).y + 24.0f);
    TArray<uint32> NewIds;
    if (CloneNodeFragment(ClipboardNodes, ClipboardLinks, Anchor, &NewIds))
    {
        SelectOnlyNodes(NewIds);
        CommitGraphEdit();
    }
}

void FMaterialEditorWidget::DuplicateSelectedNodes()
{
    CopySelectedNodes();
    PasteCopiedNodes();
}

void FMaterialEditorWidget::DeleteSelectedNodes()
{
    if (!NodeEditorContext) return;
    FScopedNodeEditorCurrent Scope(NodeEditorContext);
    ed::NodeId               Selected[512];
    const int                Count = ed::GetSelectedNodes(Selected, 512);
    if (Count <= 0) return;
    bool bChanged = false;
    for (int i = 0; i < Count; ++i)
    {
        const uint32 NodeId = NodeIdToU32(Selected[i]);
        if (IsOutputNode(WorkingGraph, NodeId)) continue;
        bChanged |= WorkingGraph.RemoveNode(NodeId);
    }
    if (bChanged) CommitGraphEdit();
}

void FMaterialEditorWidget::GroupSelectedNodesAsComment()
{
    if (!NodeEditorContext || !GetMaterial()) return;
    FScopedNodeEditorCurrent Scope(NodeEditorContext);

    ed::NodeId Selected[512];
    const int Count = ed::GetSelectedNodes(Selected, 512);
    if (Count <= 0) return;

    float MinX = FLT_MAX;
    float MinY = FLT_MAX;
    float MaxX = -FLT_MAX;
    float MaxY = -FLT_MAX;
    bool  bAny = false;

    for (int i = 0; i < Count; ++i)
    {
        const uint32 NodeIdU = NodeIdToU32(Selected[i]);
        const FMaterialGraphNode* SrcNode = WorkingGraph.FindNode(NodeIdU);
        if (!SrcNode) continue;
        const FMaterialNodeBounds Bounds = ComputeLiveMaterialNodeBounds(*SrcNode, true);
        if (!Bounds.bValid) continue;

        bAny = true;
        MinX = min(MinX, Bounds.Min.x);
        MinY = min(MinY, Bounds.Min.y);
        MaxX = max(MaxX, Bounds.Max.x);
        MaxY = max(MaxY, Bounds.Max.y);
    }
    if (!bAny) return;

    const float  Pad    = 28.0f;
    const float  Header = 30.0f;
    const ImVec2 GroupPos(MinX - Pad, MinY - Pad - Header);
    const ImVec2 GroupSize((MaxX - MinX) + Pad * 2.0f, (MaxY - MinY) + Pad * 2.0f + Header);

    FMaterialGraphNode* Comment = WorkingGraph.AddNodeOfType(
        EMaterialGraphNodeType::Comment,
        GroupPos.x,
        GroupPos.y,
        GetMaterial()->GetGraphDocument().Target);
    if (!Comment) return;

    Comment->ParameterName = "Comment";
    Comment->Value = FVector4(GroupSize.x, GroupSize.y, 0.0f, 0.0f);
    ed::SetNodePosition(ToNodeId(Comment->NodeId), GroupPos);

    ed::ClearSelection();
    ed::SelectNode(ToNodeId(Comment->NodeId), false);
    SelectedNodeId = Comment->NodeId;

    CommitGraphEdit();
}

bool FMaterialEditorWidget::AddNodeMenuItem(UMaterial* Material, EMaterialGraphNodeType Type, const char* Label)
{
    const char* Display = Label ? Label : NodeLabel(Type);
    if (!StringContainsInsensitive(Display, AddNodeSearchBuf)) return false;
    if (Type == EMaterialGraphNodeType::Output && WorkingGraph.HasOutputNode())
    {
        ImGui::BeginDisabled();
        ImGui::MenuItem(Display);
        ImGui::EndDisabled();
        return false;
    }
    if (!ImGui::MenuItem(Display)) return false;

    // 표준 패턴 (imgui-node-editor blueprints-example):
    // 1) 노드 본문에 좌표를 0,0 으로 일단 만들고
    // 2) 그 자리에서 ed::ScreenToCanvas(저장된 screen 좌표) 결과를 ed::SetNodePosition 으로 셋팅한다.
    // child window 안에서도 정확히 마우스 위치에 노드가 생긴다.
    FMaterialGraphNode* Node = WorkingGraph.AddNodeOfType(Type, 0.0f, 0.0f, Material->GetGraphDocument().Target);
    if (!Node) return false;

    const bool bHaveScreenPos = (PendingNewNodeScreenPos.x != 0.0f || PendingNewNodeScreenPos.y != 0.0f);
    const ImVec2 CanvasPos = bHaveScreenPos
        ? ed::ScreenToCanvas(PendingNewNodeScreenPos)
        : PendingNewNodePosition;
    Node->PosX = CanvasPos.x;
    Node->PosY = CanvasPos.y;
    ed::SetNodePosition(ToNodeId(Node->NodeId), CanvasPos);

    SelectOnlyNodes(TArray<uint32>{ Node->NodeId });
    CommitGraphEdit();
    // popup 닫기 후 다음 우클릭이 같은 screen pos 를 재사용하지 않도록 reset.
    PendingNewNodeScreenPos = ImVec2(0, 0);
    return true;
}

bool FMaterialEditorWidget::AddContextNodeMenuItem(UMaterial* Material, EMaterialGraphNodeType Type)
{
    const char* Display = NodeLabel(Type);
    if (!StringContainsInsensitive(Display, PinSpawnSearchBuf)) return false;
    if (Type == EMaterialGraphNodeType::Output && WorkingGraph.HasOutputNode()) return false;
    if (!NodeTypeCanConnectToPendingPin(Type)) return false;
    if (!ImGui::MenuItem(Display)) return false;

    // Same standard pattern as AddNodeMenuItem — capture SCREEN coord at popup-open time,
    // then convert to canvas + ed::SetNodePosition here.
    FMaterialGraphNode*      Node    = WorkingGraph.AddNodeOfType(Type, 0.0f, 0.0f, Material->GetGraphDocument().Target);
    const FMaterialGraphPin* DragPin = WorkingGraph.FindPin(PendingPinSpawnPinId);
    if (Node)
    {
        const bool bHaveScreenPos = (PendingNewNodeScreenPos.x != 0.0f || PendingNewNodeScreenPos.y != 0.0f);
        const ImVec2 CanvasPos = bHaveScreenPos
            ? ed::ScreenToCanvas(PendingNewNodeScreenPos)
            : PendingPinSpawnPos;
        Node->PosX = CanvasPos.x;
        Node->PosY = CanvasPos.y;
        ed::SetNodePosition(ToNodeId(Node->NodeId), CanvasPos);

        if (DragPin)
        {
            if (FMaterialGraphPin* Compatible = FindFirstCompatiblePinOnNode(*Node, *DragPin))
            {
                uint32 FromPinId = 0;
                uint32 ToPinId   = 0;
                if (WorkingGraph.CanLinkPins(DragPin->PinId, Compatible->PinId, &FromPinId, &ToPinId))
                {
                    WorkingGraph.AddLink(FromPinId, ToPinId);
                }
            }
        }
        SelectOnlyNodes(TArray<uint32>{ Node->NodeId });
        CommitGraphEdit();
        PendingNewNodeScreenPos = ImVec2(0, 0);
        return true;
    }
    return false;
}

bool FMaterialEditorWidget::NodeTypeCanConnectToPendingPin(EMaterialGraphNodeType Type) const
{
    const FMaterialGraphPin* DragPin = WorkingGraph.FindPin(PendingPinSpawnPinId);
    if (!DragPin) return false;
    FMaterialGraph      Temp = WorkingGraph;
    FMaterialGraphNode* Node = Temp.AddNodeOfType(Type, 0, 0, EMaterialGraphTarget::Surface);
    if (!Node) return false;
    for (const FMaterialGraphPin& Pin : Node->Pins)
    {
        uint32 From = 0, To = 0;
        if (Temp.CanLinkPins(DragPin->PinId, Pin.PinId, &From, &To)) return true;
    }
    return false;
}

FMaterialGraphPin* FMaterialEditorWidget::FindFirstCompatiblePinOnNode(FMaterialGraphNode& Node, const FMaterialGraphPin& DragPin) const
{
    for (FMaterialGraphPin& Pin : Node.Pins)
    {
        uint32 From = 0, To = 0;
        if (WorkingGraph.CanLinkPins(DragPin.PinId, Pin.PinId, &From, &To)) return &Pin;
    }
    return nullptr;
}

void FMaterialEditorWidget::QueueNodeEditorShortcuts()
{
    ImGuiIO& IO = ImGui::GetIO();
    if (!IO.KeyCtrl) return;

    if (IO.WantTextInput) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        if (IO.KeyShift) RedoGraphEdit();
        else UndoGraphEdit();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y)) RedoGraphEdit();
    if (ImGui::IsKeyPressed(ImGuiKey_C)) bQueuedCopySelected = true;
    if (ImGui::IsKeyPressed(ImGuiKey_V)) bQueuedPasteNodes = true;
    if (ImGui::IsKeyPressed(ImGuiKey_D)) bQueuedDuplicateSelected = true;
    if (ImGui::IsKeyPressed(ImGuiKey_G)) bQueuedGroupSelected = true;
    if (ImGui::IsKeyPressed(ImGuiKey_S)) if (UMaterial* Material = GetMaterial()) SaveGraph(Material);
}

void FMaterialEditorWidget::ProcessQueuedNodeEditorCommands()
{
    if (bQueuedCopySelected)
    {
        CopySelectedNodes();
        bQueuedCopySelected = false;
    }
    if (bQueuedPasteNodes)
    {
        PasteCopiedNodes();
        bQueuedPasteNodes = false;
    }
    if (bQueuedDuplicateSelected)
    {
        DuplicateSelectedNodes();
        bQueuedDuplicateSelected = false;
    }
    if (bQueuedDeleteSelected)
    {
        DeleteSelectedNodes();
        bQueuedDeleteSelected = false;
    }
    if (bQueuedGroupSelected)
    {
        GroupSelectedNodesAsComment();
        bQueuedGroupSelected = false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelectedNodes();
}
