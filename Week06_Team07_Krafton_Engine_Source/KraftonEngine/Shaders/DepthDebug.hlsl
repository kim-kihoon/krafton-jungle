#include "Common/ConstantBuffers.hlsl"

Texture2D<float> DepthTex : register(t0);
SamplerState PointSampler : register(s0);

struct PS_Input
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PS_Input VS(uint vertexID : SV_VertexID)
{
    PS_Input output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float3 Turbo(float t)
{
    float3 c0 = float3(0.13572138, 0.09140261, 0.10667330);
    float3 c1 = float3(4.61539260, 2.19418839, 1.86313389);
    float3 c2 = float3(-42.66032258, 4.84296658, -13.58300263);
    float3 c3 = float3(132.13108234, -14.18503333, 24.99671600);
    float3 c4 = float3(-152.94239396, 4.27729857, -23.61342477);
    float3 c5 = float3(59.28637943, 2.82956604, 8.23019831);
    return saturate(c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * c5)))));
}

float4 PS(PS_Input input) : SV_TARGET
{
    float rawDepth = DepthTex.Sample(PointSampler, input.uv);

    // D24 depth is heavily biased toward 1.0 in perspective projection.
    // Invert and apply a soft exponent so distant surfaces remain visible.
    float vis = 1.0 - saturate(rawDepth);
    vis = pow(vis, 0.20);

    // Toggle between grayscale and false color by swapping the return line.
    // return float4(vis, vis, vis, 1.0);
    return float4(Turbo(vis), 1.0);
}
