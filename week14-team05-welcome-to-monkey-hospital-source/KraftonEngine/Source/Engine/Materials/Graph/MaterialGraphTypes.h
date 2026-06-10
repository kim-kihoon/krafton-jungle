#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Object/FName.h"

class FArchive;
#include "Render/Types/MaterialTextureSlot.h"
#include "Render/Types/RenderStateTypes.h"
#include "Render/Types/RenderTypes.h"

enum class EMaterialGraphTarget : uint8
{
    Surface,
    ParticleSprite,
    ParticleMesh,
    Decal,
    PostProcess
};

enum class EMaterialGraphPinKind : uint8
{
    Input,
    Output
};

enum class EMaterialGraphPinType : uint8
{
    Float,
    Float2,
    Float3,
    Float4,
    Color,
    UV,
    Texture2D,
    Sampler,
    Bool
};

enum class EMaterialGraphNodeType : uint8
{
    Output,

    TextureObject,
    TextureSample,

    ScalarParameter,
    VectorParameter,
    ColorParameter,

    ConstantFloat,
    ConstantFloat2,
    ConstantFloat3,
    ConstantFloat4,

    Add,
    Subtract,
    Multiply,
    Divide,
    OneMinus,
    Saturate,
    Clamp,
    Power,

    Lerp,

    TexCoord,
    Panner,
    Time,

    VertexColor,
    ParticleColor,

    Append,
    ComponentMask,

    // 추가 수학 노드
    ConstantBiasScale, // V * Scale + Bias
    Distance,          // length(A - B) → Float
    Normalize,         // normalize(V)
    Dot,               // dot(A, B) → Float
    Cross,             // cross(A, B) → Float3

    // 파티클 전용
    ParticleSubUV,    // atlas Rows/Cols 파라미터, UV(Float2) 출력
    DynamicParameter, // Param1/2/3/4 + RGBA 출력

    // Conversion — float scalars ↔ vectors. UE 의 Make/Break 컨벤션과 동일.
    MakeFloat2,       // X, Y → float2
    MakeFloat3,       // X, Y, Z → float3
    MakeFloat4,       // X, Y, Z, W → float4
    BreakFloat2,      // float2 → X, Y
    BreakFloat3,      // float3 → X, Y, Z
    BreakFloat4,      // float4 → X, Y, Z, W

    // 에디터 편의 노드 — Blueprint editor 와 같은 workflow 를 위한 노드.
    Reroute,
    Comment,
};

struct FMaterialGraphPin
{
    uint32                PinId        = 0;
    uint32                OwningNodeId = 0;
    EMaterialGraphPinKind Kind         = EMaterialGraphPinKind::Input;
    EMaterialGraphPinType Type         = EMaterialGraphPinType::Float;
    FName                 DisplayName;
};

struct FMaterialGraphLink
{
    uint32 LinkId    = 0;
    uint32 FromPinId = 0;
    uint32 ToPinId   = 0;
};

struct FMaterialGraphNode
{
    uint32                    NodeId = 0;
    EMaterialGraphNodeType    Type   = EMaterialGraphNodeType::Output;
    FName                     DisplayName;
    float                     PosX = 0.0f;
    float                     PosY = 0.0f;
    TArray<FMaterialGraphPin> Pins;

    FString              ParameterName;
    FString              TexturePath;
    EMaterialTextureSlot TextureSlot = EMaterialTextureSlot::Diffuse;
    FVector4             Value       = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    FString              Mask        = "RGBA";
};

struct FMaterialCompiledParameter
{
    EMaterialGraphPinType Type  = EMaterialGraphPinType::Float4;
    FVector4              Value = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
};

struct FMaterialCompiledTexture
{
    FString              Path;
    EMaterialTextureSlot Slot = EMaterialTextureSlot::Diffuse;
};

struct FMaterialGraph
{
    TArray<FMaterialGraphNode> Nodes;
    TArray<FMaterialGraphLink> Links;
    uint32                     NextId = 1;

    uint32 AllocateId()
    {
        return NextId++;
    }

    FMaterialGraphNode* AddNode(EMaterialGraphNodeType Type, const FName& DisplayName, float X, float Y);
    FMaterialGraphPin*  AddPin(FMaterialGraphNode& Node, EMaterialGraphPinKind Kind, EMaterialGraphPinType PinType, const FName& DisplayName);
    FMaterialGraphLink* AddLink(uint32 FromPinId, uint32 ToPinId);
    FMaterialGraphNode* AddNodeOfType(EMaterialGraphNodeType Type, float X, float Y, EMaterialGraphTarget Domain);

    bool RemoveNode(uint32 NodeId);
    bool RemoveLink(uint32 LinkId);
    bool CanLinkPins(uint32 PinAId, uint32 PinBId, uint32* OutFromPinId = nullptr, uint32* OutToPinId = nullptr) const;
    bool HasOutputNode() const;

    FMaterialGraphNode*       FindNode(uint32 NodeId);
    const FMaterialGraphNode* FindNode(uint32 NodeId) const;
    FMaterialGraphPin*        FindPin(uint32 PinId);
    const FMaterialGraphPin*  FindPin(uint32 PinId) const;
    FMaterialGraphNode*       FindFirstNodeOfType(EMaterialGraphNodeType Type);
    const FMaterialGraphNode* FindFirstNodeOfType(EMaterialGraphNodeType Type) const;

    void InitializeDefault(EMaterialGraphTarget Domain);
    void RebuildOutputPinsForDomain(EMaterialGraphTarget Domain);
    bool RepairOutputPinsForDomain(EMaterialGraphTarget Domain);
    bool RepairPinsForDomain(EMaterialGraphTarget Domain);

    // 텍스처 기반 파티클 프리셋 — 한 번에 TextureObject/Sample/Multiply/Mask/Output 세팅.
    // 호출 시 기존 노드/링크 모두 클리어.
    void ApplyTexturedParticlePreset(EMaterialGraphTarget Domain);
};

const char* ToString(EMaterialGraphTarget Domain);
const char* ToString(EMaterialGraphPinType Type);
const char* ToString(EMaterialGraphNodeType Type);
const char* ToString(EMaterialTextureSlot Slot);

EMaterialGraphTarget   MaterialGraphTargetFromString(const FString& Str, EMaterialGraphTarget Default = EMaterialGraphTarget::Surface);
EMaterialGraphPinType  MaterialPinTypeFromString(const FString& Str, EMaterialGraphPinType Default = EMaterialGraphPinType::Float);
EMaterialGraphNodeType MaterialNodeTypeFromString(const FString& Str, EMaterialGraphNodeType Default = EMaterialGraphNodeType::Output);
EMaterialTextureSlot   MaterialTextureSlotFromString(const FString& Str, EMaterialTextureSlot Default = EMaterialTextureSlot::Diffuse);

FArchive& operator<<(FArchive& Ar, FMaterialGraphPin& Pin);
FArchive& operator<<(FArchive& Ar, FMaterialGraphLink& Link);
FArchive& operator<<(FArchive& Ar, FMaterialGraphNode& Node);
FArchive& operator<<(FArchive& Ar, FMaterialCompiledParameter& Parameter);
FArchive& operator<<(FArchive& Ar, FMaterialCompiledTexture& Texture);
FArchive& operator<<(FArchive& Ar, FMaterialGraph& Graph);

FString ComputeMaterialGraphStructuralHash(const FMaterialGraph& Graph);

bool IsMaterialGraphPinTypeConvertible(EMaterialGraphPinType From, EMaterialGraphPinType To);
