#include "Materials/Graph/MaterialGraphTypes.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace
{
    struct FOutputPinSchema
    {
        const char* Name;
        EMaterialGraphPinType Type;
    };

    struct FPinSchema
    {
        EMaterialGraphPinKind Kind;
        EMaterialGraphPinType Type;
        const char* Name;
    };

    TArray<FOutputPinSchema> GetOutputPinSchema(EMaterialGraphTarget Domain)
    {
        switch (Domain)
        {
        case EMaterialGraphTarget::ParticleSprite:
        case EMaterialGraphTarget::ParticleMesh:
            return {
                { "Color", EMaterialGraphPinType::Float3 },
                { "Emissive", EMaterialGraphPinType::Float3 },
                { "Opacity", EMaterialGraphPinType::Float },
                { "UVOffset", EMaterialGraphPinType::Float2 },
            };
        case EMaterialGraphTarget::Decal:
        case EMaterialGraphTarget::Surface:
        default:
            return {
                { "BaseColor", EMaterialGraphPinType::Float3 },
                { "Normal", EMaterialGraphPinType::Float3 },
                { "Roughness", EMaterialGraphPinType::Float },
                { "Metallic", EMaterialGraphPinType::Float },
                { "Emissive", EMaterialGraphPinType::Float3 },
                { "Opacity", EMaterialGraphPinType::Float },
                { "OpacityMask", EMaterialGraphPinType::Float },
            };
        case EMaterialGraphTarget::PostProcess:
            return {
                { "Color", EMaterialGraphPinType::Float3 },
                { "Opacity", EMaterialGraphPinType::Float },
            };
        }
    }

    const char* GetDefaultNodeDisplayName(EMaterialGraphNodeType Type)
    {
        switch (Type)
        {
        case EMaterialGraphNodeType::Output: return "Material Output";
        case EMaterialGraphNodeType::TextureObject: return "Texture Object";
        case EMaterialGraphNodeType::TextureSample: return "Texture Sample";
        case EMaterialGraphNodeType::ScalarParameter: return "Scalar Parameter";
        case EMaterialGraphNodeType::VectorParameter: return "Vector Parameter";
        case EMaterialGraphNodeType::ColorParameter: return "Color Parameter";
        case EMaterialGraphNodeType::TexCoord: return "TexCoord";
        case EMaterialGraphNodeType::VertexColor: return "Vertex Color";
        case EMaterialGraphNodeType::ParticleColor: return "Particle Color";
        case EMaterialGraphNodeType::ComponentMask: return "Component Mask";
        case EMaterialGraphNodeType::ParticleSubUV: return "Particle SubUV";
        case EMaterialGraphNodeType::DynamicParameter: return "Dynamic Parameter";
        case EMaterialGraphNodeType::MakeFloat2: return "Make Float2";
        case EMaterialGraphNodeType::MakeFloat3: return "Make Float3";
        case EMaterialGraphNodeType::MakeFloat4: return "Make Float4";
        case EMaterialGraphNodeType::BreakFloat2: return "Break Float2";
        case EMaterialGraphNodeType::BreakFloat3: return "Break Float3";
        case EMaterialGraphNodeType::BreakFloat4: return "Break Float4";
        default: return ToString(Type);
        }
    }

    TArray<FPinSchema> GetPinSchema(const FMaterialGraphNode& Node, EMaterialGraphTarget Domain)
    {
        switch (Node.Type)
        {
        case EMaterialGraphNodeType::Output:
        {
            TArray<FPinSchema> Pins;
            for (const FOutputPinSchema& Pin : GetOutputPinSchema(Domain))
            {
                Pins.push_back({ EMaterialGraphPinKind::Input, Pin.Type, Pin.Name });
            }
            return Pins;
        }
        case EMaterialGraphNodeType::TextureObject:
            return { { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Texture2D, "Texture" } };
        case EMaterialGraphNodeType::TextureSample:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Texture2D, "Texture" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float2, "UV" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, "RGB" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "R" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "G" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "B" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "A" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "RGBA" },
            };
        case EMaterialGraphNodeType::ScalarParameter:
        case EMaterialGraphNodeType::ConstantFloat:
            return { { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Value" } };
        case EMaterialGraphNodeType::VectorParameter:
        case EMaterialGraphNodeType::ConstantFloat4:
            return { { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "Value" } };
        case EMaterialGraphNodeType::ColorParameter:
            return { { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Color, "Color" } };
        case EMaterialGraphNodeType::ConstantFloat2:
            return { { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, "Value" } };
        case EMaterialGraphNodeType::ConstantFloat3:
            return { { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, "Value" } };
        case EMaterialGraphNodeType::Add:
        case EMaterialGraphNodeType::Subtract:
        case EMaterialGraphNodeType::Multiply:
        case EMaterialGraphNodeType::Divide:
        case EMaterialGraphNodeType::Power:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "A" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "B" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "Result" },
            };
        case EMaterialGraphNodeType::OneMinus:
        case EMaterialGraphNodeType::Saturate:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "Value" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "Result" },
            };
        case EMaterialGraphNodeType::Clamp:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "Value" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "Min" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "Max" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "Result" },
            };
        case EMaterialGraphNodeType::Lerp:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "A" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "B" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "Alpha" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "Result" },
            };
        case EMaterialGraphNodeType::TexCoord:
        case EMaterialGraphNodeType::ParticleSubUV:
            return { { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, "UV" } };
        case EMaterialGraphNodeType::Panner:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float2, "UV" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "Time" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, "UV" },
            };
        case EMaterialGraphNodeType::Time:
            return { { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Time" } };
        case EMaterialGraphNodeType::VertexColor:
        case EMaterialGraphNodeType::ParticleColor:
            return {
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, "RGB" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "R" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "G" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "B" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "A" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "RGBA" },
            };
        case EMaterialGraphNodeType::Append:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "A" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "B" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, "Result" },
            };
        case EMaterialGraphNodeType::ComponentMask:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "Value" },
                { EMaterialGraphPinKind::Output, Node.Mask.size() == 1 ? EMaterialGraphPinType::Float : EMaterialGraphPinType::Float3, "Result" },
            };
        case EMaterialGraphNodeType::ConstantBiasScale:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "Value" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Result" },
            };
        case EMaterialGraphNodeType::Distance:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, "A" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, "B" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Result" },
            };
        case EMaterialGraphNodeType::Normalize:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, "Value" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, "Result" },
            };
        case EMaterialGraphNodeType::Dot:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, "A" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, "B" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Result" },
            };
        case EMaterialGraphNodeType::Cross:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, "A" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, "B" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, "Result" },
            };
        case EMaterialGraphNodeType::DynamicParameter:
            return {
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Param1" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Param2" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Param3" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Param4" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "RGBA" },
            };
        case EMaterialGraphNodeType::MakeFloat2:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "X" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "Y" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, "Result" },
            };
        case EMaterialGraphNodeType::MakeFloat3:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "X" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "Y" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "Z" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, "Result" },
            };
        case EMaterialGraphNodeType::MakeFloat4:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "X" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "Y" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "Z" },
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, "W" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "Result" },
            };
        case EMaterialGraphNodeType::BreakFloat2:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float2, "Value" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "X" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Y" },
            };
        case EMaterialGraphNodeType::BreakFloat3:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, "Value" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "X" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Y" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Z" },
            };
        case EMaterialGraphNodeType::BreakFloat4:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "Value" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "X" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Y" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "Z" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, "W" },
            };
        case EMaterialGraphNodeType::Reroute:
            return {
                { EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, "In" },
                { EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, "Out" },
            };
        case EMaterialGraphNodeType::Comment:
        default:
            return {};
        }
    }
}

FMaterialGraphNode* FMaterialGraph::AddNode(EMaterialGraphNodeType Type, const FName& DisplayName, float X, float Y)
{
    FMaterialGraphNode Node;
    Node.NodeId      = AllocateId();
    Node.Type        = Type;
    Node.DisplayName = DisplayName;
    Node.PosX        = X;
    Node.PosY        = Y;
    Nodes.push_back(std::move(Node));
    return &Nodes.back();
}

FMaterialGraphPin* FMaterialGraph::AddPin(FMaterialGraphNode& Node, EMaterialGraphPinKind Kind, EMaterialGraphPinType PinType, const FName& DisplayName)
{
    FMaterialGraphPin Pin;
    Pin.PinId        = AllocateId();
    Pin.OwningNodeId = Node.NodeId;
    Pin.Kind         = Kind;
    Pin.Type         = PinType;
    Pin.DisplayName  = DisplayName;
    Node.Pins.push_back(std::move(Pin));
    return &Node.Pins.back();
}

FMaterialGraphLink* FMaterialGraph::AddLink(uint32 FromPinId, uint32 ToPinId)
{
    FMaterialGraphLink Link;
    Link.LinkId    = AllocateId();
    Link.FromPinId = FromPinId;
    Link.ToPinId   = ToPinId;
    Links.push_back(std::move(Link));
    return &Links.back();
}

FMaterialGraphNode* FMaterialGraph::AddNodeOfType(EMaterialGraphNodeType Type, float X, float Y, EMaterialGraphTarget Domain)
{
    if (Type == EMaterialGraphNodeType::Output && HasOutputNode())
    {
        return nullptr;
    }

    switch (Type)
    {
    case EMaterialGraphNodeType::Output:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Material Output"), X, Y);
        RebuildOutputPinsForDomain(Domain);
        return N;
    }
    case EMaterialGraphNodeType::TextureObject:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Texture Object"), X, Y);
        N->ParameterName      = "Diffuse";
        N->TextureSlot        = EMaterialTextureSlot::Diffuse;
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Texture2D, FName("Texture"));
        return N;
    }
    case EMaterialGraphNodeType::TextureSample:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Texture Sample"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Texture2D, FName("Texture"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float2, FName("UV"));
        // 채널별 분리 출력 + 풀 출력. InputExpr이 pin 이름으로 swizzle 처리.
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, FName("RGB"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("R"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("G"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("RGBA"));
        return N;
    }
    case EMaterialGraphNodeType::ScalarParameter:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Scalar Parameter"), X, Y);
        N->ParameterName      = "Scalar";
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Value"));
        return N;
    }
    case EMaterialGraphNodeType::VectorParameter:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Vector Parameter"), X, Y);
        N->ParameterName      = "Vector";
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("Value"));
        return N;
    }
    case EMaterialGraphNodeType::ColorParameter:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Color Parameter"), X, Y);
        N->ParameterName      = "Color";
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Color, FName("Color"));
        return N;
    }
    case EMaterialGraphNodeType::ConstantFloat:
    case EMaterialGraphNodeType::ConstantFloat2:
    case EMaterialGraphNodeType::ConstantFloat3:
    case EMaterialGraphNodeType::ConstantFloat4:
    {
        const EMaterialGraphPinType PinType =
                Type == EMaterialGraphNodeType::ConstantFloat ? EMaterialGraphPinType::Float :
                Type == EMaterialGraphNodeType::ConstantFloat2 ? EMaterialGraphPinType::Float2 :
                Type == EMaterialGraphNodeType::ConstantFloat3 ? EMaterialGraphPinType::Float3 :
                EMaterialGraphPinType::Float4;
        FMaterialGraphNode* N = AddNode(Type, FName(ToString(Type)), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Output, PinType, FName("Value"));
        return N;
    }
    case EMaterialGraphNodeType::Add:
    case EMaterialGraphNodeType::Subtract:
    case EMaterialGraphNodeType::Multiply:
    case EMaterialGraphNodeType::Divide:
    case EMaterialGraphNodeType::Power:
    {
        FMaterialGraphNode* N = AddNode(Type, FName(ToString(Type)), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::OneMinus:
    case EMaterialGraphNodeType::Saturate:
    {
        FMaterialGraphNode* N = AddNode(Type, FName(ToString(Type)), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("Value"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::Clamp:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Clamp"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("Value"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("Min"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("Max"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::Lerp:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Lerp"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("Alpha"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::TexCoord:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("TexCoord"), X, Y);
        // Value.X = UV channel index (0/1/2). 기본 0.
        N->Value = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, FName("UV"));
        return N;
    }
    case EMaterialGraphNodeType::Panner:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Panner"), X, Y);
        N->Value              = FVector4(0.1f, 0.0f, 0.0f, 0.0f);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float2, FName("UV"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("Time"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, FName("UV"));
        return N;
    }
    case EMaterialGraphNodeType::Time:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Time"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Time"));
        return N;
    }
    case EMaterialGraphNodeType::VertexColor:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Vertex Color"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, FName("RGB"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("R"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("G"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("RGBA"));
        return N;
    }
    case EMaterialGraphNodeType::ParticleColor:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Particle Color"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, FName("RGB"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("R"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("G"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("RGBA"));
        return N;
    }
    case EMaterialGraphNodeType::Append:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Append"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::ComponentMask:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Component Mask"), X, Y);
        N->Mask               = "RGB";
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("Value"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::ConstantBiasScale:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("ConstantBiasScale"), X, Y);
        // Value.X = Bias, Value.Y = Scale. UE 기본값 (-1,1 remap) 흉내.
        N->Value = FVector4(-0.5f, 2.0f, 0.0f, 0.0f);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("Value"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::Distance:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Distance"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::Normalize:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Normalize"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, FName("Value"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::Dot:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Dot"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::Cross:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Cross"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, FName("A"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, FName("B"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::ParticleSubUV:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Particle SubUV"), X, Y);
        // Value.X = Cols, Value.Y = Rows. 기본 4x4 아틀라스.
        N->Value = FVector4(4.0f, 4.0f, 0.0f, 0.0f);
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, FName("UV"));
        return N;
    }
    case EMaterialGraphNodeType::Reroute:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Reroute"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("In"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("Out"));
        return N;
    }
    case EMaterialGraphNodeType::Comment:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Comment"), X, Y);
        N->Value              = FVector4(420.0f, 180.0f, 0.0f, 0.0f);
        N->ParameterName      = "Comment";
        return N;
    }
    case EMaterialGraphNodeType::DynamicParameter:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Dynamic Parameter"), X, Y);
        // 4채널 분리 출력 + RGBA. swizzle helper에서 Param1~Param4 인식.
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Param1"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Param2"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Param3"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Param4"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("RGBA"));
        return N;
    }
    case EMaterialGraphNodeType::MakeFloat2:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Make Float2"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("X"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("Y"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float2, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::MakeFloat3:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Make Float3"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("X"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("Y"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("Z"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float3, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::MakeFloat4:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Make Float4"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("X"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("Y"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("Z"));
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float, FName("W"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float4, FName("Result"));
        return N;
    }
    case EMaterialGraphNodeType::BreakFloat2:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Break Float2"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float2, FName("Value"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("X"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Y"));
        return N;
    }
    case EMaterialGraphNodeType::BreakFloat3:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Break Float3"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float3, FName("Value"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("X"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Y"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Z"));
        return N;
    }
    case EMaterialGraphNodeType::BreakFloat4:
    {
        FMaterialGraphNode* N = AddNode(Type, FName("Break Float4"), X, Y);
        AddPin(*N, EMaterialGraphPinKind::Input, EMaterialGraphPinType::Float4, FName("Value"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("X"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Y"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("Z"));
        AddPin(*N, EMaterialGraphPinKind::Output, EMaterialGraphPinType::Float, FName("W"));
        return N;
    }
    }
    return nullptr;
}

bool FMaterialGraph::RemoveNode(uint32 NodeId)
{
    if (NodeId == 0) return false;

    TArray<uint32> PinIds;
    for (const FMaterialGraphNode& Node : Nodes)
    {
        if (Node.NodeId != NodeId) continue;
        if (Node.Type == EMaterialGraphNodeType::Output) return false;

        for (const FMaterialGraphPin& Pin : Node.Pins) PinIds.push_back(Pin.PinId);
        break;
    }

    Links.erase(
        std::remove_if(
            Links.begin(),
            Links.end(),
            [&PinIds](const FMaterialGraphLink& L)
            {
                for (uint32 PinId : PinIds)
                {
                    if (L.FromPinId == PinId || L.ToPinId == PinId) return true;
                }
                return false;
            }
        ),
        Links.end()
    );

    const size_t Before = Nodes.size();
    Nodes.erase(
        std::remove_if(
            Nodes.begin(),
            Nodes.end(),
            [NodeId](const FMaterialGraphNode& N)
            {
                return N.NodeId == NodeId;
            }
        ),
        Nodes.end()
    );
    return Nodes.size() != Before;
}

bool FMaterialGraph::RemoveLink(uint32 LinkId)
{
    if (LinkId == 0) return false;
    const size_t Before = Links.size();
    Links.erase(
        std::remove_if(
            Links.begin(),
            Links.end(),
            [LinkId](const FMaterialGraphLink& L)
            {
                return L.LinkId == LinkId;
            }
        ),
        Links.end()
    );
    return Links.size() != Before;
}

bool FMaterialGraph::CanLinkPins(uint32 PinAId, uint32 PinBId, uint32* OutFromPinId, uint32* OutToPinId) const
{
    if (PinAId == 0 || PinBId == 0 || PinAId == PinBId) return false;

    const FMaterialGraphPin* A = FindPin(PinAId);
    const FMaterialGraphPin* B = FindPin(PinBId);
    if (!A || !B) return false;
    if (A->OwningNodeId == B->OwningNodeId) return false;
    if (A->Kind == B->Kind) return false;

    const FMaterialGraphPin* From = A->Kind == EMaterialGraphPinKind::Output ? A : B;
    const FMaterialGraphPin* To   = From == A ? B : A;
    if (!IsMaterialGraphPinTypeConvertible(From->Type, To->Type)) return false;

    for (const FMaterialGraphLink& L : Links)
    {
        if (L.ToPinId == To->PinId) return false;
        if (L.FromPinId == From->PinId && L.ToPinId == To->PinId) return false;
    }

    if (OutFromPinId) *OutFromPinId = From->PinId;
    if (OutToPinId) *OutToPinId = To->PinId;
    return true;
}

bool FMaterialGraph::HasOutputNode() const
{
    return FindFirstNodeOfType(EMaterialGraphNodeType::Output) != nullptr;
}

FMaterialGraphNode* FMaterialGraph::FindNode(uint32 NodeId)
{
    for (FMaterialGraphNode& Node : Nodes)
    {
        if (Node.NodeId == NodeId) return &Node;
    }
    return nullptr;
}

const FMaterialGraphNode* FMaterialGraph::FindNode(uint32 NodeId) const
{
    for (const FMaterialGraphNode& Node : Nodes)
    {
        if (Node.NodeId == NodeId) return &Node;
    }
    return nullptr;
}

FMaterialGraphPin* FMaterialGraph::FindPin(uint32 PinId)
{
    for (FMaterialGraphNode& Node : Nodes)
    {
        for (FMaterialGraphPin& Pin : Node.Pins)
        {
            if (Pin.PinId == PinId) return &Pin;
        }
    }
    return nullptr;
}

const FMaterialGraphPin* FMaterialGraph::FindPin(uint32 PinId) const
{
    for (const FMaterialGraphNode& Node : Nodes)
    {
        for (const FMaterialGraphPin& Pin : Node.Pins)
        {
            if (Pin.PinId == PinId) return &Pin;
        }
    }
    return nullptr;
}

FMaterialGraphNode* FMaterialGraph::FindFirstNodeOfType(EMaterialGraphNodeType Type)
{
    for (FMaterialGraphNode& Node : Nodes)
    {
        if (Node.Type == Type) return &Node;
    }
    return nullptr;
}

const FMaterialGraphNode* FMaterialGraph::FindFirstNodeOfType(EMaterialGraphNodeType Type) const
{
    for (const FMaterialGraphNode& Node : Nodes)
    {
        if (Node.Type == Type) return &Node;
    }
    return nullptr;
}

void FMaterialGraph::InitializeDefault(EMaterialGraphTarget Domain)
{
    Nodes.clear();
    Links.clear();
    NextId = 1;

    // 파티클 도메인은 텍스처가 없어도 보이도록 ParticleColor → Color/Opacity 만 연결.
    // 사용자가 텍스처를 추가하면 TextureSample을 끼워 넣어 곱하면 됨.
    if (Domain == EMaterialGraphTarget::ParticleSprite || Domain == EMaterialGraphTarget::ParticleMesh)
    {
        uint32 RGBOutPin = 0, AOutPin = 0;
        if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::ParticleColor, -240.0f, 80.0f, Domain))
        {
            for (const FMaterialGraphPin& P : N->Pins)
            {
                if (P.Kind != EMaterialGraphPinKind::Output) continue;
                const FString PN = P.DisplayName.ToString();
                if (PN == "RGB") RGBOutPin = P.PinId;
                else if (PN == "A") AOutPin = P.PinId;
            }
        }

        FMaterialGraphNode* Out = AddNodeOfType(EMaterialGraphNodeType::Output, 80.0f, 80.0f, Domain);

        for (const FMaterialGraphPin& Pin : Out->Pins)
        {
            const FString PinName = Pin.DisplayName.ToString();
            if (PinName == "Color") AddLink(RGBOutPin, Pin.PinId);
            else if (PinName == "Opacity") AddLink(AOutPin, Pin.PinId);
        }
        return;
    }

    // Surface/Decal/PostProcess 등 — 텍스처 기반 기본 그래프.
    uint32 TexOut = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::TextureObject, -720.0f, -80.0f, Domain)) TexOut = N->Pins[0].PinId;

    uint32 UVOut = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::TexCoord, -720.0f, 120.0f, Domain)) UVOut = N->Pins[0].PinId;

    uint32 SampleTexIn = 0, SampleUVIn = 0, SampleOut = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::TextureSample, -480.0f, 0.0f, Domain))
    {
        SampleTexIn = N->Pins[0].PinId;
        SampleUVIn  = N->Pins[1].PinId;
        SampleOut   = N->Pins[2].PinId;
    }

    uint32 VertexColorOut = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::VertexColor, -480.0f, 220.0f, Domain)) VertexColorOut = N->Pins[0].PinId;

    uint32 MulAIn = 0, MulBIn = 0, MulOut = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::Multiply, -240.0f, 80.0f, Domain))
    {
        MulAIn = N->Pins[0].PinId;
        MulBIn = N->Pins[1].PinId;
        MulOut = N->Pins[2].PinId;
    }

    uint32 RGBIn = 0, RGBOut = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::ComponentMask, 0.0f, 0.0f, Domain))
    {
        RGBIn  = N->Pins[0].PinId;
        RGBOut = N->Pins[1].PinId;
    }

    uint32 AIn = 0, AOut = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::ComponentMask, 0.0f, 180.0f, Domain))
    {
        N->Mask = "A";
        if (N->Pins.size() >= 2) N->Pins[1].Type = EMaterialGraphPinType::Float;
        AIn  = N->Pins[0].PinId;
        AOut = N->Pins[1].PinId;
    }

    FMaterialGraphNode* Out = AddNodeOfType(EMaterialGraphNodeType::Output, 260.0f, 80.0f, Domain);

    AddLink(TexOut, SampleTexIn);
    AddLink(UVOut, SampleUVIn);
    AddLink(SampleOut, MulAIn);
    AddLink(VertexColorOut, MulBIn);
    AddLink(MulOut, RGBIn);
    AddLink(MulOut, AIn);

    for (const FMaterialGraphPin& Pin : Out->Pins)
    {
        const FString PinName = Pin.DisplayName.ToString();
        if (PinName == "Color" || PinName == "BaseColor")
        {
            AddLink(RGBOut, Pin.PinId);
        }
        else if (PinName == "Opacity")
        {
            AddLink(AOut, Pin.PinId);
        }
    }
}

void FMaterialGraph::RebuildOutputPinsForDomain(EMaterialGraphTarget Domain)
{
    FMaterialGraphNode* Output = FindFirstNodeOfType(EMaterialGraphNodeType::Output);
    if (!Output)
    {
        return;
    }

    TArray<uint32> OldPinIds;
    for (const FMaterialGraphPin& Pin : Output->Pins)
    {
        OldPinIds.push_back(Pin.PinId);
    }

    Links.erase(
        std::remove_if(
            Links.begin(),
            Links.end(),
            [&OldPinIds](const FMaterialGraphLink& L)
            {
                for (uint32 PinId : OldPinIds)
                {
                    if (L.FromPinId == PinId || L.ToPinId == PinId) return true;
                }
                return false;
            }
        ),
        Links.end()
    );
    Output->Pins.clear();

    for (const FOutputPinSchema& Pin : GetOutputPinSchema(Domain))
    {
        AddPin(*Output, EMaterialGraphPinKind::Input, Pin.Type, FName(Pin.Name));
    }
}

bool FMaterialGraph::RepairOutputPinsForDomain(EMaterialGraphTarget Domain)
{
    FMaterialGraphNode* Output = FindFirstNodeOfType(EMaterialGraphNodeType::Output);
    if (!Output)
    {
        return false;
    }

    bool bChanged = false;
    const TArray<FOutputPinSchema> Schema = GetOutputPinSchema(Domain);

    for (uint32 i = 0; i < static_cast<uint32>(Schema.size()); ++i)
    {
        if (i >= static_cast<uint32>(Output->Pins.size()))
        {
            AddPin(*Output, EMaterialGraphPinKind::Input, Schema[i].Type, FName(Schema[i].Name));
            bChanged = true;
            continue;
        }

        FMaterialGraphPin& Pin = Output->Pins[i];
        if (Pin.Kind != EMaterialGraphPinKind::Input)
        {
            Pin.Kind = EMaterialGraphPinKind::Input;
            bChanged = true;
        }
        if (Pin.Type != Schema[i].Type)
        {
            Pin.Type = Schema[i].Type;
            bChanged = true;
        }
        if (Pin.DisplayName.ToString() != Schema[i].Name)
        {
            Pin.DisplayName = FName(Schema[i].Name);
            bChanged = true;
        }
        if (Pin.OwningNodeId != Output->NodeId)
        {
            Pin.OwningNodeId = Output->NodeId;
            bChanged = true;
        }
    }

    if (Output->Pins.size() > Schema.size())
    {
        TArray<uint32> RemovedPinIds;
        for (uint32 i = static_cast<uint32>(Schema.size()); i < static_cast<uint32>(Output->Pins.size()); ++i)
        {
            RemovedPinIds.push_back(Output->Pins[i].PinId);
        }

        Links.erase(
            std::remove_if(
                Links.begin(),
                Links.end(),
                [&RemovedPinIds](const FMaterialGraphLink& L)
                {
                    for (uint32 PinId : RemovedPinIds)
                    {
                        if (L.FromPinId == PinId || L.ToPinId == PinId) return true;
                    }
                    return false;
                }
            ),
            Links.end()
        );
        Output->Pins.resize(Schema.size());
        bChanged = true;
    }

    return bChanged;
}

bool FMaterialGraph::RepairPinsForDomain(EMaterialGraphTarget Domain)
{
    bool bChanged = RepairOutputPinsForDomain(Domain);

    for (FMaterialGraphNode& Node : Nodes)
    {
        const FString ExpectedNodeName = GetDefaultNodeDisplayName(Node.Type);
        if (!ExpectedNodeName.empty() && Node.DisplayName.ToString() != ExpectedNodeName)
        {
            Node.DisplayName = FName(ExpectedNodeName);
            bChanged = true;
        }

        if (Node.Type == EMaterialGraphNodeType::Output)
        {
            continue;
        }

        const TArray<FPinSchema> Schema = GetPinSchema(Node, Domain);
        for (uint32 i = 0; i < static_cast<uint32>(Schema.size()); ++i)
        {
            if (i >= static_cast<uint32>(Node.Pins.size()))
            {
                AddPin(Node, Schema[i].Kind, Schema[i].Type, FName(Schema[i].Name));
                bChanged = true;
                continue;
            }

            FMaterialGraphPin& Pin = Node.Pins[i];
            if (Pin.Kind != Schema[i].Kind)
            {
                Pin.Kind = Schema[i].Kind;
                bChanged = true;
            }
            if (Pin.Type != Schema[i].Type)
            {
                Pin.Type = Schema[i].Type;
                bChanged = true;
            }
            if (Pin.DisplayName.ToString() != Schema[i].Name)
            {
                Pin.DisplayName = FName(Schema[i].Name);
                bChanged = true;
            }
            if (Pin.OwningNodeId != Node.NodeId)
            {
                Pin.OwningNodeId = Node.NodeId;
                bChanged = true;
            }
        }

        if (Node.Pins.size() > Schema.size())
        {
            TArray<uint32> RemovedPinIds;
            for (uint32 i = static_cast<uint32>(Schema.size()); i < static_cast<uint32>(Node.Pins.size()); ++i)
            {
                RemovedPinIds.push_back(Node.Pins[i].PinId);
            }

            Links.erase(
                std::remove_if(
                    Links.begin(),
                    Links.end(),
                    [&RemovedPinIds](const FMaterialGraphLink& L)
                    {
                        for (uint32 PinId : RemovedPinIds)
                        {
                            if (L.FromPinId == PinId || L.ToPinId == PinId) return true;
                        }
                        return false;
                    }
                ),
                Links.end()
            );
            Node.Pins.resize(Schema.size());
            bChanged = true;
        }
    }

    return bChanged;
}

void FMaterialGraph::ApplyTexturedParticlePreset(EMaterialGraphTarget Domain)
{
    Nodes.clear();
    Links.clear();
    NextId = 1;

    // ⚠ 노드 포인터를 AddNodeOfType 호출 사이에 유지하면 안 됨 — push_back이 재할당하면 dangling.
    //     각 if-block 안에서 PinId만 즉시 캡처하고 포인터는 버린다. Out 노드만 마지막에 추가되어 안전.

    uint32 TexOut = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::TextureObject, -720.0f, -80.0f, Domain))
    {
        TexOut = N->Pins[0].PinId;
    }

    uint32 SampleTexIn = 0, SampleRGB = 0, SampleA = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::TextureSample, -480.0f, 0.0f, Domain))
    {
        for (const FMaterialGraphPin& P : N->Pins)
        {
            const FString PN = P.DisplayName.ToString();
            if (P.Kind == EMaterialGraphPinKind::Input && PN == "Texture") SampleTexIn = P.PinId;
            else if (P.Kind == EMaterialGraphPinKind::Output && PN == "RGB") SampleRGB = P.PinId;
            else if (P.Kind == EMaterialGraphPinKind::Output && PN == "A") SampleA = P.PinId;
        }
    }

    uint32 PColRGB = 0, PColA = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::ParticleColor, -480.0f, 220.0f, Domain))
    {
        for (const FMaterialGraphPin& P : N->Pins)
        {
            if (P.Kind != EMaterialGraphPinKind::Output) continue;
            const FString PN = P.DisplayName.ToString();
            if (PN == "RGB") PColRGB = P.PinId;
            else if (PN == "A") PColA = P.PinId;
        }
    }

    uint32 MulRGB_A = 0, MulRGB_B = 0, MulRGB_Out = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::Multiply, -200.0f, 0.0f, Domain))
    {
        MulRGB_A   = N->Pins[0].PinId;
        MulRGB_B   = N->Pins[1].PinId;
        MulRGB_Out = N->Pins[2].PinId;
    }

    uint32 MulA_A = 0, MulA_B = 0, MulA_Out = 0;
    if (FMaterialGraphNode* N = AddNodeOfType(EMaterialGraphNodeType::Multiply, -200.0f, 180.0f, Domain))
    {
        MulA_A   = N->Pins[0].PinId;
        MulA_B   = N->Pins[1].PinId;
        MulA_Out = N->Pins[2].PinId;
    }

    // Output은 마지막 — 추가 push_back이 없으니 포인터를 잠시 유지해도 안전.
    FMaterialGraphNode* Out = AddNodeOfType(EMaterialGraphNodeType::Output, 80.0f, 80.0f, Domain);

    AddLink(TexOut, SampleTexIn);
    AddLink(SampleRGB, MulRGB_A);
    AddLink(PColRGB, MulRGB_B);
    AddLink(SampleA, MulA_A);
    AddLink(PColA, MulA_B);

    if (Out)
    {
        for (const FMaterialGraphPin& Pin : Out->Pins)
        {
            const FString PinName = Pin.DisplayName.ToString();
            if (PinName == "Color" || PinName == "BaseColor") AddLink(MulRGB_Out, Pin.PinId);
            else if (PinName == "Opacity") AddLink(MulA_Out, Pin.PinId);
        }
    }
}

const char* ToString(EMaterialGraphTarget Domain)
{
    switch (Domain)
    {
    case EMaterialGraphTarget::Surface:
        return "Surface";
    case EMaterialGraphTarget::ParticleSprite:
        return "ParticleSprite";
    case EMaterialGraphTarget::ParticleMesh:
        return "ParticleMesh";
    case EMaterialGraphTarget::Decal:
        return "Decal";
    case EMaterialGraphTarget::PostProcess:
        return "PostProcess";
    }
    return "Surface";
}

const char* ToString(EMaterialGraphPinType Type)
{
    switch (Type)
    {
    case EMaterialGraphPinType::Float:
        return "Float";
    case EMaterialGraphPinType::Float2:
        return "Float2";
    case EMaterialGraphPinType::Float3:
        return "Float3";
    case EMaterialGraphPinType::Float4:
        return "Float4";
    case EMaterialGraphPinType::Color:
        return "Color";
    case EMaterialGraphPinType::UV:
        return "UV";
    case EMaterialGraphPinType::Texture2D:
        return "Texture2D";
    case EMaterialGraphPinType::Sampler:
        return "Sampler";
    case EMaterialGraphPinType::Bool:
        return "Bool";
    }
    return "Float";
}

const char* ToString(EMaterialGraphNodeType Type)
{
    switch (Type)
    {
    case EMaterialGraphNodeType::Output:
        return "Output";
    case EMaterialGraphNodeType::TextureObject:
        return "TextureObject";
    case EMaterialGraphNodeType::TextureSample:
        return "TextureSample";
    case EMaterialGraphNodeType::ScalarParameter:
        return "ScalarParameter";
    case EMaterialGraphNodeType::VectorParameter:
        return "VectorParameter";
    case EMaterialGraphNodeType::ColorParameter:
        return "ColorParameter";
    case EMaterialGraphNodeType::ConstantFloat:
        return "ConstantFloat";
    case EMaterialGraphNodeType::ConstantFloat2:
        return "ConstantFloat2";
    case EMaterialGraphNodeType::ConstantFloat3:
        return "ConstantFloat3";
    case EMaterialGraphNodeType::ConstantFloat4:
        return "ConstantFloat4";
    case EMaterialGraphNodeType::Add:
        return "Add";
    case EMaterialGraphNodeType::Subtract:
        return "Subtract";
    case EMaterialGraphNodeType::Multiply:
        return "Multiply";
    case EMaterialGraphNodeType::Divide:
        return "Divide";
    case EMaterialGraphNodeType::OneMinus:
        return "OneMinus";
    case EMaterialGraphNodeType::Saturate:
        return "Saturate";
    case EMaterialGraphNodeType::Clamp:
        return "Clamp";
    case EMaterialGraphNodeType::Power:
        return "Power";
    case EMaterialGraphNodeType::Lerp:
        return "Lerp";
    case EMaterialGraphNodeType::TexCoord:
        return "TexCoord";
    case EMaterialGraphNodeType::Panner:
        return "Panner";
    case EMaterialGraphNodeType::Time:
        return "Time";
    case EMaterialGraphNodeType::VertexColor:
        return "VertexColor";
    case EMaterialGraphNodeType::ParticleColor:
        return "ParticleColor";
    case EMaterialGraphNodeType::Append:
        return "Append";
    case EMaterialGraphNodeType::ComponentMask:
        return "ComponentMask";
    case EMaterialGraphNodeType::ConstantBiasScale:
        return "ConstantBiasScale";
    case EMaterialGraphNodeType::Distance:
        return "Distance";
    case EMaterialGraphNodeType::Normalize:
        return "Normalize";
    case EMaterialGraphNodeType::Dot:
        return "Dot";
    case EMaterialGraphNodeType::Cross:
        return "Cross";
    case EMaterialGraphNodeType::ParticleSubUV:
        return "ParticleSubUV";
    case EMaterialGraphNodeType::DynamicParameter:
        return "DynamicParameter";
    case EMaterialGraphNodeType::MakeFloat2:
        return "MakeFloat2";
    case EMaterialGraphNodeType::MakeFloat3:
        return "MakeFloat3";
    case EMaterialGraphNodeType::MakeFloat4:
        return "MakeFloat4";
    case EMaterialGraphNodeType::BreakFloat2:
        return "BreakFloat2";
    case EMaterialGraphNodeType::BreakFloat3:
        return "BreakFloat3";
    case EMaterialGraphNodeType::BreakFloat4:
        return "BreakFloat4";
    case EMaterialGraphNodeType::Reroute:
        return "Reroute";
    case EMaterialGraphNodeType::Comment:
        return "Comment";
    }
    return "Output";
}

const char* ToString(EMaterialTextureSlot Slot)
{
    switch (Slot)
    {
    case EMaterialTextureSlot::Diffuse:
        return "Diffuse";
    case EMaterialTextureSlot::Normal:
        return "Normal";
    case EMaterialTextureSlot::Roughness:
        return "Roughness";
    case EMaterialTextureSlot::Metallic:
        return "Metallic";
    case EMaterialTextureSlot::Emissive:
        return "Emissive";
    case EMaterialTextureSlot::AO:
        return "AO";
    case EMaterialTextureSlot::Custom0:
        return "Custom0";
    case EMaterialTextureSlot::Custom1:
        return "Custom1";
    default:
        return "Diffuse";
    }
}

EMaterialGraphTarget MaterialGraphTargetFromString(const FString& Str, EMaterialGraphTarget Default)
{
    if (Str == "Surface") return EMaterialGraphTarget::Surface;
    if (Str == "ParticleSprite") return EMaterialGraphTarget::ParticleSprite;
    if (Str == "ParticleMesh") return EMaterialGraphTarget::ParticleMesh;
    if (Str == "Decal") return EMaterialGraphTarget::Decal;
    if (Str == "PostProcess") return EMaterialGraphTarget::PostProcess;
    return Default;
}

EMaterialGraphPinType MaterialPinTypeFromString(const FString& Str, EMaterialGraphPinType Default)
{
    if (Str == "Float") return EMaterialGraphPinType::Float;
    if (Str == "Float2") return EMaterialGraphPinType::Float2;
    if (Str == "Float3") return EMaterialGraphPinType::Float3;
    if (Str == "Float4") return EMaterialGraphPinType::Float4;
    if (Str == "Color") return EMaterialGraphPinType::Color;
    if (Str == "UV") return EMaterialGraphPinType::UV;
    if (Str == "Texture2D") return EMaterialGraphPinType::Texture2D;
    if (Str == "Sampler") return EMaterialGraphPinType::Sampler;
    if (Str == "Bool") return EMaterialGraphPinType::Bool;
    return Default;
}

EMaterialGraphNodeType MaterialNodeTypeFromString(const FString& Str, EMaterialGraphNodeType Default)
{
    for (int32 i = 0; i <= static_cast<int32>(EMaterialGraphNodeType::Comment); ++i)
    {
        const EMaterialGraphNodeType Type = static_cast<EMaterialGraphNodeType>(i);
        if (Str == ToString(Type)) return Type;
    }
    return Default;
}

EMaterialTextureSlot MaterialTextureSlotFromString(const FString& Str, EMaterialTextureSlot Default)
{
    if (Str == "Diffuse") return EMaterialTextureSlot::Diffuse;
    if (Str == "Normal") return EMaterialTextureSlot::Normal;
    if (Str == "Roughness") return EMaterialTextureSlot::Roughness;
    if (Str == "Metallic") return EMaterialTextureSlot::Metallic;
    if (Str == "Emissive") return EMaterialTextureSlot::Emissive;
    if (Str == "AO") return EMaterialTextureSlot::AO;
    if (Str == "Custom0") return EMaterialTextureSlot::Custom0;
    if (Str == "Custom1") return EMaterialTextureSlot::Custom1;
    return Default;
}

// Binary graph serialization used by 20 .uasset material source payloads.
FArchive& operator<<(FArchive& Ar, FMaterialGraphPin& Pin)
{
    Ar << Pin.PinId;
    Ar << Pin.OwningNodeId;
    Ar << Pin.Kind;
    Ar << Pin.Type;
    Ar << Pin.DisplayName;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FMaterialGraphLink& Link)
{
    Ar << Link.LinkId;
    Ar << Link.FromPinId;
    Ar << Link.ToPinId;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FMaterialGraphNode& Node)
{
    Ar << Node.NodeId;
    Ar << Node.Type;
    Ar << Node.DisplayName;
    Ar << Node.PosX;
    Ar << Node.PosY;
    Ar << Node.Pins;
    Ar << Node.ParameterName;
    Ar << Node.TexturePath;
    Ar << Node.TextureSlot;
    Ar << Node.Value;
    Ar << Node.Mask;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FMaterialCompiledParameter& Parameter)
{
    Ar << Parameter.Type;
    Ar << Parameter.Value;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FMaterialCompiledTexture& Texture)
{
    Ar << Texture.Path;
    Ar << Texture.Slot;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FMaterialGraph& Graph)
{
    Ar << Graph.Nodes;
    Ar << Graph.Links;
    Ar << Graph.NextId;
    return Ar;
}

bool IsMaterialGraphPinTypeConvertible(EMaterialGraphPinType From, EMaterialGraphPinType To)
{
    if (From == To) return true;

    // vector ↔ vector 는 코드젠의 ConvertExpr이 broadcast/swizzle/zero-pad로 처리.
    // 링크는 허용하고 변환은 HLSL 생성 시점에 일관되게.
    auto IsVec = [](EMaterialGraphPinType T)
    {
        switch (T)
        {
        case EMaterialGraphPinType::Float:
        case EMaterialGraphPinType::Float2:
        case EMaterialGraphPinType::Float3:
        case EMaterialGraphPinType::Float4:
        case EMaterialGraphPinType::Color:
        case EMaterialGraphPinType::UV:
            return true;
        default:
            return false;
        }
    };
    if (IsVec(From) && IsVec(To)) return true;

    return false;
}

static void HashCombineString(uint64& Hash, const FString& Text)
{
    for (char C : Text)
    {
        Hash ^= static_cast<uint8>(C);
        Hash *= 1099511628211ull;
    }
}

static void HashCombineU32(uint64& Hash, uint32 Value)
{
    for (int i = 0; i < 4; ++i)
    {
        Hash ^= static_cast<uint8>((Value >> (i * 8)) & 0xffu);
        Hash *= 1099511628211ull;
    }
}

static void HashCombineFloat(uint64& Hash, float Value)
{
    uint32 Bits = 0;
    static_assert(sizeof(Bits) == sizeof(Value), "float hash expects 32-bit float");
    memcpy(&Bits, &Value, sizeof(float));
    HashCombineU32(Hash, Bits);
}

FString ComputeMaterialGraphStructuralHash(const FMaterialGraph& Graph)
{
    uint64 Hash = 1469598103934665603ull;
    HashCombineU32(Hash, Graph.NextId);
    for (const FMaterialGraphNode& Node : Graph.Nodes)
    {
        HashCombineU32(Hash, Node.NodeId);
        HashCombineU32(Hash, static_cast<uint32>(Node.Type));
        HashCombineString(Hash, Node.DisplayName.ToString());
        HashCombineFloat(Hash, Node.PosX);
        HashCombineFloat(Hash, Node.PosY);
        HashCombineString(Hash, Node.ParameterName);
        HashCombineString(Hash, Node.TexturePath);
        HashCombineU32(Hash, static_cast<uint32>(Node.TextureSlot));
        HashCombineFloat(Hash, Node.Value.X);
        HashCombineFloat(Hash, Node.Value.Y);
        HashCombineFloat(Hash, Node.Value.Z);
        HashCombineFloat(Hash, Node.Value.W);
        HashCombineString(Hash, Node.Mask);
        for (const FMaterialGraphPin& Pin : Node.Pins)
        {
            HashCombineU32(Hash, Pin.PinId);
            HashCombineU32(Hash, Pin.OwningNodeId);
            HashCombineU32(Hash, static_cast<uint32>(Pin.Kind));
            HashCombineU32(Hash, static_cast<uint32>(Pin.Type));
            HashCombineString(Hash, Pin.DisplayName.ToString());
        }
    }
    for (const FMaterialGraphLink& Link : Graph.Links)
    {
        HashCombineU32(Hash, Link.LinkId);
        HashCombineU32(Hash, Link.FromPinId);
        HashCombineU32(Hash, Link.ToPinId);
    }

    char Buffer[32] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%016llx", static_cast<unsigned long long>(Hash));
    return FString(Buffer);
}
