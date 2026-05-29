#ifndef VERTEX_LAYOUTS_HLSL
#define VERTEX_LAYOUTS_HLSL

// ============================================================
// VS Input Layouts — C++ VertexTypes.h 와 1:1 대응
// ============================================================

// FVertex (Position + Color)
// 사용: Primitive, Editor, Gizmo, Outline, Line
struct VS_Input_PC
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

// FVertexPNCT (Position + Normal + Color + TexCoord)
// 사용: StaticMesh, OutlinePNCT
struct VS_Input_PNCT
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
    float2 texcoord : TEXTCOORD;
};

struct VS_Input_PNCTT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXTCOORD;
    float4 tangent : TANGENT;
};

struct VS_Input_ParticleSprite
{
    float3 position : POSITION;
    float3 size : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 color : COLOR;
    float rotation : TEXCOORD2;
    float subImage : TEXCOORD3;
    float3 velocity : TEXCOORD4;
};

// INSTANCE_* inputs are routed to the slot-1 per-instance stream by Shader.cpp.
struct VS_Input_MeshParticle
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXTCOORD;
    float4 tangent : TANGENT;
    float4 instanceTransform0 : INSTANCE_TRANSFORM0;
    float4 instanceTransform1 : INSTANCE_TRANSFORM1;
    float4 instanceTransform2 : INSTANCE_TRANSFORM2;
    float4 instanceTransform3 : INSTANCE_TRANSFORM3;
    float4 instanceColor : INSTANCE_COLOR;
    float4 dynamicParam : INSTANCE_DYNAMICPARAM;
};

struct VS_Input_BeamParticle
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

struct VS_Input_RibbonParticle
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

// FVertexPNCTBW (Position + Normal + Color + TexCoord + Tangent + BoneIndex + BoneWeight)
// 사용: SkeletalMesh
struct VS_Input_PNCTBW
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXTCOORD;
    float4 tangent : TANGENT;
    int4 boneIndices : BONEINDICES;
    float4 boneWeights : BONEWEIGHTS;
};

// FTextureVertex (Position + TexCoord)
// 사용: Font, SubUV, OverlayFont
struct VS_Input_PT
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD;
};

// Position only (Outline primitive expansion)
struct VS_Input_P
{
    float3 position : POSITION;
};

// ============================================================
// PS Input (VS -> PS 전달 구조체)
// ============================================================

// SV_POSITION + Color
struct PS_Input_Color
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

// SV_POSITION + TexCoord
struct PS_Input_Tex
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

// SV_POSITION + Normal + Color + TexCoord (StaticMesh)
struct PS_Input_Full
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
    float2 texcoord : TEXTCOORD;
};

// SV_POSITION + UV (PostProcess: HeightFog, Outline, SceneDepth)
struct PS_Input_UV
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// SV_POSITION only (Outline)
struct PS_Input_PosOnly
{
    float4 position : SV_POSITION;
};

struct PS_Input_Particle
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color    : COLOR;
};

// SV_POSITION + Color + WorldPos (Editor)
struct PS_Input_ColorWorld
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float3 worldPos : TEXCOORD0;
};

struct PS_Input_Decal
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 color : COLOR;
};

// SV_POSITION + Depth (ShadowDepth)
struct PS_Input_Shadow
{
    float4 position : SV_POSITION;
    float  depth    : TEXCOORD0;    // VSM용 normalized depth
};

#endif // VERTEX_LAYOUTS_HLSL
