// FXAA.hlsl
// Fast Approximate Anti-Aliasing (Simplified FXAA 3.11)

#include "Common/ConstantBuffers.hlsl"

Texture2D ColorTex : register(t0);
SamplerState LinearSampler : register(s0);

struct PS_Input
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// VS: Fullscreen Triangle (same as OutlinePostProcess)
PS_Input VS(uint vertexID : SV_VertexID)
{
    PS_Input output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

// Helper: Calculate Luminance
float GetLuma(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

float4 PS(PS_Input input) : SV_TARGET
{
    float2 res;
    ColorTex.GetDimensions(res.x, res.y);
    float2 texelSize = 1.0 / res;

    float3 rgbM = ColorTex.Sample(LinearSampler, input.uv).rgb;
    float lumaM = GetLuma(rgbM);

    float lumaNW = GetLuma(ColorTex.Sample(LinearSampler, input.uv + float2(-1, -1) * texelSize).rgb);
    float lumaNE = GetLuma(ColorTex.Sample(LinearSampler, input.uv + float2(1, -1) * texelSize).rgb);
    float lumaSW = GetLuma(ColorTex.Sample(LinearSampler, input.uv + float2(-1, 1) * texelSize).rgb);
    float lumaSE = GetLuma(ColorTex.Sample(LinearSampler, input.uv + float2(1, 1) * texelSize).rgb);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float range = lumaMax - lumaMin;

    // Edge threshold
    if (range < max(0.0312, lumaMax * 0.125))
    {
        return float4(rgbM, 1.0);
    }

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * 0.125), 0.0001);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(float2(8.0, 8.0), max(float2(-8.0, -8.0), dir * rcpDirMin)) * texelSize;

    float3 rgbA = 0.5 * (
        ColorTex.Sample(LinearSampler, input.uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        ColorTex.Sample(LinearSampler, input.uv + dir * (2.0 / 3.0 - 0.5)).rgb);

    float3 rgbB = rgbA * 0.5 + 0.25 * (
        ColorTex.Sample(LinearSampler, input.uv + dir * (0.0 / 3.0 - 0.5)).rgb +
        ColorTex.Sample(LinearSampler, input.uv + dir * (3.0 / 3.0 - 0.5)).rgb);

    float lumaB = GetLuma(rgbB);

    if ((lumaB < lumaMin) || (lumaB > lumaMax))
    {
        return float4(rgbA, 1.0);
    }
    else
    {
        return float4(rgbB, 1.0);
    }
}
