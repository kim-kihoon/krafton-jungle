#include "Common/ConstantBuffers.hlsli"

cbuffer ShadowPassBuffer : register(b2)
{
    row_major float4x4 LightVP;
    row_major float4x4 CameraVP;
    uint bIsPSM;
    uint bPSMFlipNegativeW;
    uint2 _pad;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput VS(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(float4(input.Position, 1.0f), Model);
    if (bIsPSM != 0)
    {
        output.Position = mul(worldPos, LightVP);
    }
    else
    {
        output.Position = mul(worldPos, LightVP);
    }
    if (bPSMFlipNegativeW != 0 && output.Position.w < 0.0f)
    {
        output.Position *= -1.0f;
    }
    return output;
}

float4 PS(VSOutput input) : SV_TARGET
{
#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
    float depth = saturate(input.Position.z);
    return float4(depth, depth * depth, 0.0, 0.0);
#else
    return float4(0.0, 0.0, 0.0, 0.0);
#endif
}
