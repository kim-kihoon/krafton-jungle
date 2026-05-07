#ifndef UBER_SURFACE_INCLUDED
#define UBER_SURFACE_INCLUDED

cbuffer UberFrame : register(b0)
{
    row_major float4x4 View;
    row_major float4x4 Projection;
    float3 CameraPosition;
    float _UberFramePad0;
    float bIsWireframe;
    float bLightingEnabled;
    float UberDebugViewMode;
    float _UberFramePad1;
    float3 WireframeRGB;
    float _UberFramePad2;
}

cbuffer UberPerObject : register(b1)
{
    row_major float4x4 World;
    row_major float4x4 WorldInverseTranspose;
    float4 PrimitiveColor;
}

cbuffer UberMaterial : register(b2)
{
    float3 BaseColor;
    float Opacity;

    float3 SpecularColor;
    float Shininess;

    float2 ScrollUV;
    uint bHasDiffuseMap;
    uint bHasSpecularMap;

    float3 EmissiveColor;
    uint bHasNormalMap;
}

Texture2D DiffuseMap : register(t0);
Texture2D SpecularMap : register(t1);
Texture2D NormalMap : register(t2);

#ifndef NIPS_SAMPLE_STATE_DECLARED
#define NIPS_SAMPLE_STATE_DECLARED
SamplerState SampleState : register(s0);
#endif

struct FUberVSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct FUberPSInput
{
    float4 ClipPos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float3 WorldTangent : TEXCOORD3;
    float3 WorldBitangent : TEXCOORD4;
    float3 VertexDiffuseLighting : TEXCOORD5;
    float3 VertexSpecularLighting : TEXCOORD6;
};

struct FUberPSOutput
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 WorldPos : SV_TARGET2;
};

struct FUberSurfaceData
{
    float3 WorldPos;
    float3 WorldNormal;
    float2 UV;
    float4 DiffuseSample;
    float3 Albedo;
    uint bIsEmissive;
};

float4 ApplyUberMVP(float3 Position)
{
    const float4 WorldPosition = mul(float4(Position, 1.0f), World);
    const float4 ViewPosition = mul(WorldPosition, View);
    return mul(ViewPosition, Projection);
}

float3 TransformNormalToWorld(float3 Normal)
{
    return normalize(mul(Normal, (float3x3)WorldInverseTranspose));
}

float3 TransformDirectionToWorld(float3 Direction)
{
    return mul(Direction, (float3x3)World);
}

float3 BuildFallbackWorldTangent(float3 WorldNormal)
{
    const float3 UpCandidate = abs(WorldNormal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    return normalize(cross(UpCandidate, WorldNormal));
}

float3 ResolveSurfaceWorldNormal(FUberPSInput Input, float2 UV, float3 GeometricWorldNormal)
{
#if defined(LIGHTING_MODEL_GOURAUD)
    return GeometricWorldNormal;
#else
    float3 WorldTangent = Input.WorldTangent - GeometricWorldNormal * dot(Input.WorldTangent, GeometricWorldNormal);
    if (dot(WorldTangent, WorldTangent) <= 1.0e-8f)
    {
        WorldTangent = BuildFallbackWorldTangent(GeometricWorldNormal);
    }
    else
    {
        WorldTangent = normalize(WorldTangent);
    }

    float3 WorldBitangent = Input.WorldBitangent;
    WorldBitangent = WorldBitangent - GeometricWorldNormal * dot(WorldBitangent, GeometricWorldNormal);
    WorldBitangent = WorldBitangent - WorldTangent * dot(WorldBitangent, WorldTangent);
    if (dot(WorldBitangent, WorldBitangent) <= 1.0e-8f)
    {
        WorldBitangent = normalize(cross(GeometricWorldNormal, WorldTangent));
    }
    else
    {
        WorldBitangent = normalize(WorldBitangent);
    }

    if (bHasNormalMap != 0u)
    {
        const float3 TangentSpaceNormal = NormalMap.Sample(SampleState, UV).xyz * 2.0f - 1.0f;
        const float3x3 TBN = float3x3(WorldTangent, WorldBitangent, GeometricWorldNormal);
        return normalize(mul(TangentSpaceNormal, TBN));
    }

    return GeometricWorldNormal;
#endif
}

FUberPSInput BuildSurfaceVertex(FUberVSInput Input)
{
    FUberPSInput Output;

    Output.WorldPos = mul(float4(Input.Position, 1.0f), World).xyz;
    Output.WorldNormal = TransformNormalToWorld(Input.Normal);
    Output.WorldTangent = normalize(TransformDirectionToWorld(Input.Tangent));
    Output.WorldBitangent = normalize(TransformDirectionToWorld(Input.Bitangent));
    Output.UV = Input.UV + ScrollUV;
    Output.ClipPos = ApplyUberMVP(Input.Position);
    Output.VertexDiffuseLighting = 1.0f.xxx;
    Output.VertexSpecularLighting = 0.0f.xxx;

    return Output;
}

FUberSurfaceData EvaluateSurface(FUberPSInput Input)
{
    FUberSurfaceData Surface;

    Surface.WorldPos = Input.WorldPos;
    Surface.WorldNormal = normalize(Input.WorldNormal);
    Surface.UV = Input.UV;
    Surface.DiffuseSample = float4(1.0f, 1.0f, 1.0f, 1.0f);

    if (bHasDiffuseMap != 0u)
    {
        Surface.DiffuseSample = DiffuseMap.Sample(SampleState, Surface.UV);
        clip(Surface.DiffuseSample.a - 0.001f);
    }

    Surface.WorldNormal = ResolveSurfaceWorldNormal(Input, Surface.UV, Surface.WorldNormal);
    Surface.Albedo = BaseColor * Surface.DiffuseSample.rgb;
    Surface.bIsEmissive = any(EmissiveColor > 0.0f) ? 1u : 0u;

    return Surface;
}

FUberPSOutput ComposeOutput(FUberSurfaceData Surface, float3 FinalColor)
{
    FUberPSOutput Output;
    float3 ResolvedColor = FinalColor;
    float NormalAlpha = 1.0f;

    if (Surface.bIsEmissive != 0u)
    {
        ResolvedColor = EmissiveColor * Surface.DiffuseSample.rgb;
        NormalAlpha = 2.0f;
    }
    else if (bIsWireframe > 0.5f)
    {
        ResolvedColor = WireframeRGB;
    }

    Output.Color = float4(ResolvedColor, 1.0f);
    Output.Normal = float4(Surface.WorldNormal * 0.5f + 0.5f, NormalAlpha);
    Output.WorldPos = float4(Surface.WorldPos, 1.0f);
    return Output;
}

FUberPSOutput ComposeDebugOutput(FUberSurfaceData Surface, float3 DebugColor)
{
    FUberPSOutput Output;
    Output.Color = float4(DebugColor, 1.0f);
    Output.Normal = float4(Surface.WorldNormal * 0.5f + 0.5f, 1.0f);
    Output.WorldPos = float4(Surface.WorldPos, 1.0f);
    return Output;
}

#endif
