#include "../Common.hlsl"

Texture2D FinalSceneColor : register(t0);
SamplerState SampleState : register(s0);

cbuffer PostProcessBuffer : register(b10)
{
    float2 InvResolution;
    float Gamma;
    float VignetteIntensity;
    float VignetteRadius;
    float VignetteSoftness;
    float2 Padding0;
};

struct VSOutput
{
    float4 ClipPos : SV_POSITION;
};

VSOutput mainVS(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 pos;
    if (vertexID == 0)
        pos = float2(-1.0f, -1.0f);
    else if (vertexID == 1)
        pos = float2(-1.0f, 3.0f);
    else
        pos = float2(3.0f, -1.0f);

    output.ClipPos = float4(pos, 0.0f, 1.0f);
    return output;
}

float4 mainPS(VSOutput input) : SV_TARGET
{
    int2 ip = int2(input.ClipPos.xy);
    float2 uv = (float2(ip) + 0.5f) * InvResolution;
    float4 color = FinalSceneColor.Load(int3(ip, 0));

    const float vignetteIntensity = saturate(VignetteIntensity);
    if (vignetteIntensity > 0.0f)
    {
        const float2 centeredUV = uv * 2.0f - 1.0f;
        const float distanceFromCenter = length(centeredUV);
        const float radius = max(VignetteRadius, 0.0f);
        const float softness = max(VignetteSoftness, 0.0001f);
        const float vignette = smoothstep(radius, radius + softness, distanceFromCenter);
        color.rgb *= lerp(1.0f, 1.0f - vignette, vignetteIntensity);
    }

    const float safeGamma = max(Gamma, 0.0001f);
    color.rgb = pow(saturate(color.rgb), 1.0f / safeGamma);
    color.a = 1.0f;
    return color;
}
