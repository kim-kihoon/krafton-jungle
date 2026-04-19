#include "Common/ConstantBuffers.hlsl"
Texture2D<float> SceneDepth : register(t0);
Texture2D BaseColorTex : register(t1);
SamplerState LinearSampler : register(s0);
struct PS_Input
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
struct FireBallData
{
    float3 Center;
    float _Padding;
    float Intensity;
    float Radius;
    float RadiusFallOff;
    float4 Color;
};
cbuffer FireBallBuffer : register(b7)
{
    float4x4 FireBallInvViewProj;
    uint FireBallCount;
    float3 _pad0;
    FireBallData FireBalls[16];
}
PS_Input VS(uint vertexID : SV_VertexID)
{
    PS_Input output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
float3 ReconstructWorldPos(float2 screenUV, float depth)
{
    float2 ndc;
    ndc.x = screenUV.x * 2.0f - 1.0f;
    ndc.y = -(screenUV.y * 2.0f - 1.0f);
    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, FireBallInvViewProj);
    worldPos /= worldPos.w;
    return worldPos.xyz;
}
float4 PS(PS_Input input) : SV_TARGET
{
    float4 baseColor = BaseColorTex.Sample(LinearSampler, input.uv);
    float depth = SceneDepth.Sample(LinearSampler, input.uv).r;
    if (depth >= 1.0f || FireBallCount == 0)
    {
        return baseColor;
    }
    float3 worldPos = ReconstructWorldPos(input.uv, depth);
    float3 totalLight = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (uint i = 0; i < 16; ++i)
    {
        if (i >= FireBallCount)
        {
            break;
        }
        float dist = distance(worldPos, FireBalls[i].Center);
        float t = saturate(1.0f - dist / max(FireBalls[i].Radius, 0.0001f));
        float glow = pow(t, max(FireBalls[i].RadiusFallOff, 0.0001f));
        totalLight += FireBalls[i].Color.rgb * FireBalls[i].Intensity * glow;
    }
    return float4(baseColor.rgb + totalLight, baseColor.a);
}