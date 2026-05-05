cbuffer SpotShadowBuffer : register(b0)
{
    row_major float4x4 LightViewProj;
    float ShadowResolution;
    float ShadowBias;
    float ShadowFarPlane;
    uint ShadowFilterType;
    float3 SpotShadowPadding;
};

static const uint SHADOW_FILTER_TYPE_PCF = 0u;
static const uint SHADOW_FILTER_TYPE_VSM = 1u;
static const uint SHADOW_FILTER_TYPE_ESM = 2u;
static const float SHADOW_ESM_EXPONENT = 40.0f;

cbuffer PerObjectBuffer : register(b1)
{
    row_major float4x4 World;
};

struct FSpotShadowVSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct FSpotShadowVSOutput
{
    float4 ClipPos : SV_POSITION;
    float4 ClipPosW : TEXCOORD0;
};

FSpotShadowVSOutput mainVS(FSpotShadowVSInput Input)
{
    FSpotShadowVSOutput Output;

    const float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
    Output.ClipPos = mul(WorldPos, LightViewProj);
    Output.ClipPosW = Output.ClipPos;

    return Output;
}

float2 mainPS(FSpotShadowVSOutput Input) : SV_Target0
{
    float d = saturate(Input.ClipPosW.w / max(ShadowFarPlane, 1.0e-4f));
    if (ShadowFilterType == SHADOW_FILTER_TYPE_ESM)
    {
        const float e = exp(SHADOW_ESM_EXPONENT * d);
        return float2(e, e);
    }

    return float2(d, d * d);
}
