// HorrorPostProcess.hlsl
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

cbuffer HorrorPostProcessBuffer : register(b2)
{
    float VignetteIntensity;
    float VignetteRadius;
    float VignetteSoftness;
    float GrainStrength;

    float GrainScale;
    float GrainDarkPower;
    float NoiseMin;
    float NoiseMax;

    float RandomSeed;
    float ChromaticStrength;
    float Time;
    float _Pad0;

    float4 VignetteColor;
    float4 NoiseColor;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    PS_Input_UV output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float Hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float SanitizeChannel(float value)
{
    return value == value ? saturate(value) : 1.0f;
}

float3 SanitizeSceneColor(float3 color)
{
    return float3(
        SanitizeChannel(color.r),
        SanitizeChannel(color.g),
        SanitizeChannel(color.b)
    );
}


float3 SampleChromatic(float2 uv, float2 texelSize)
{
    float2 centered = uv - 0.5f;
    float dist = length(centered);
    float2 dir = dist > 1.0e-5f ? centered / dist : float2(0.0f, 0.0f);
    float2 offset = dir * ChromaticStrength * dist * min(texelSize.x, texelSize.y);

    float2 uvR = saturate(uv + offset);
    float2 uvB = saturate(uv - offset);

    float r = SceneColorTexture.SampleLevel(LinearClampSampler, uvR, 0).r;
    float g = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0).g;
    float b = SceneColorTexture.SampleLevel(LinearClampSampler, uvB, 0).b;
    return SanitizeSceneColor(float3(r, g, b));
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    uint width;
    uint height;
    SceneColorTexture.GetDimensions(width, height);

    float2 texelSize = float2(1.0f / max(width, 1u), 1.0f / max(height, 1u));
    float2 uv = input.uv;

    float4 centerColor = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0);
    float3 originalColor = SanitizeSceneColor(centerColor.rgb);
    float3 color = ChromaticStrength > 0.0f
        ? SampleChromatic(uv, texelSize)
        : originalColor.rgb;

    float2 centered = uv - 0.5f;
    float vignetteDistance = length(centered) * 1.41421356f;
    float vignetteEnd = max(VignetteRadius + max(VignetteSoftness, 1.0e-4f), VignetteRadius);
    float vignette = smoothstep(VignetteRadius, vignetteEnd, vignetteDistance) * VignetteIntensity;
    color = lerp(color, VignetteColor.rgb, saturate(vignette) * VignetteColor.a);

    float2 pixelCoord = uv * float2(width, height);
    float grainCellSize = max(GrainScale, 1.0f);
    float2 grainCoord = floor(pixelCoord / grainCellSize);
    float randomValue = Hash21(grainCoord + float2(RandomSeed, Time * 37.0f));
    float remappedNoise = lerp(NoiseMin, NoiseMax, randomValue);

    float luma = dot(color, float3(0.299f, 0.587f, 0.114f));
    float darkFactor = pow(saturate(1.0f - luma), max(GrainDarkPower, 0.0f));
    float grain = (remappedNoise - 0.5f) * 2.0f * GrainStrength * darkFactor;
    color += grain * NoiseColor.rgb * NoiseColor.a;

    return float4(SanitizeSceneColor(color), centerColor.a);
}
