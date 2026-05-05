cbuffer PointShadowBuffer : register(b0)
{
    row_major float4x4 LightViewProj;
    float3 LightPosition;
    float  FarPlane;
    float  ShadowBias;
    float  ShadowResolution;
    uint   ShadowFilterType;
    float  PointShadowPadding;
};

static const uint SHADOW_FILTER_TYPE_PCF = 0u;
static const uint SHADOW_FILTER_TYPE_VSM = 1u;
static const uint SHADOW_FILTER_TYPE_ESM = 2u;
static const float SHADOW_ESM_EXPONENT = 40.0f;

cbuffer PerObjectBuffer : register(b1)
{
    row_major float4x4 World;
};

struct FPointShadowVSInput
{
    float3 Position  : POSITION;
    float4 Color     : COLOR;
    float3 Normal    : NORMAL;
    float2 UV        : TEXCOORD;
    float3 Tangent   : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct FPointShadowVSOutput
{
    float4 ClipPos  : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
};

struct FPointShadowPSOutput
{
    float2 Moments : SV_Target0;
    float Depth : SV_Depth;
};

FPointShadowVSOutput mainVS(FPointShadowVSInput Input)
{
    FPointShadowVSOutput Output;

    float4 WorldPos4 = mul(float4(Input.Position, 1.0f), World);
    Output.WorldPos  = WorldPos4.xyz;
    Output.ClipPos   = mul(WorldPos4, LightViewProj);

    return Output;
}

FPointShadowPSOutput mainPS(FPointShadowVSOutput Input)
{
    float Dist = length(Input.WorldPos - LightPosition);
    float d = saturate(Dist / FarPlane);

    FPointShadowPSOutput Output;
    Output.Depth = d;
    if (ShadowFilterType == SHADOW_FILTER_TYPE_ESM)
    {
        const float e = exp(SHADOW_ESM_EXPONENT * d);
        Output.Moments = float2(e, e);
    }
    else
    {
        Output.Moments = float2(d, d * d);
    }

    return Output;
}
