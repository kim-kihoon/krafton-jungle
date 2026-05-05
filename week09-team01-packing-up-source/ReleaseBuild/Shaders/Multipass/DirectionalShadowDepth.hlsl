cbuffer DirectionalShadowBuffer : register(b0)
{
    row_major float4x4 LightViewProj;
    uint ShadowFilterType;
    float3 DirectionalShadowPadding;
};

static const uint SHADOW_FILTER_TYPE_PCF = 0u;
static const uint SHADOW_FILTER_TYPE_VSM = 1u;
static const uint SHADOW_FILTER_TYPE_ESM = 2u;
static const float SHADOW_ESM_EXPONENT = 40.0f;

cbuffer PerObjectBuffer : register(b1)
{
    row_major float4x4 World;
};

struct FDirectionalShadowVSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct FDirectionalShadowVSOutput
{
    float4 ClipPos : SV_POSITION;
};

FDirectionalShadowVSOutput mainVS(FDirectionalShadowVSInput Input)
{
    FDirectionalShadowVSOutput Output;

    float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
    Output.ClipPos = mul(WorldPos, LightViewProj);
    
    return Output;
}

float2 mainPS(FDirectionalShadowVSOutput Input) : SV_Target0
{
    float d = saturate(Input.ClipPos.z);
    if (ShadowFilterType == SHADOW_FILTER_TYPE_ESM)
    {
        const float e = exp(SHADOW_ESM_EXPONENT * d);
        return float2(e, e);
    }
    return float2(d, d * d);
}
