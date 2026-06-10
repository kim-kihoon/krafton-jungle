#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

cbuffer DOFCB : register(b2)
{
    float FocalLength;
    float Aperture;
    float FocusDistance;
    float NearClip;
    float FarClip;
    float ViewportWidth;
    float ViewportHeight;
    float _DOFPad0;
};

// Circular Kernel from GPU Zen 'Practical Gather-based Bokeh Depth of Field' by Wojciech Sterna
static const float2 offsets[] =
{
    2.0f * float2(1.000000f, 0.000000f),
    2.0f * float2(0.707107f, 0.707107f),
    2.0f * float2(-0.000000f, 1.000000f),
    2.0f * float2(-0.707107f, 0.707107f),
    2.0f * float2(-1.000000f, -0.000000f),
    2.0f * float2(-0.707106f, -0.707107f),
    2.0f * float2(0.000000f, -1.000000f),
    2.0f * float2(0.707107f, -0.707107f),

    4.0f * float2(1.000000f, 0.000000f),
    4.0f * float2(0.923880f, 0.382683f),
    4.0f * float2(0.707107f, 0.707107f),
    4.0f * float2(0.382683f, 0.923880f),
    4.0f * float2(-0.000000f, 1.000000f),
    4.0f * float2(-0.382684f, 0.923879f),
    4.0f * float2(-0.707107f, 0.707107f),
    4.0f * float2(-0.923880f, 0.382683f),
    4.0f * float2(-1.000000f, -0.000000f),
    4.0f * float2(-0.923879f, -0.382684f),
    4.0f * float2(-0.707106f, -0.707107f),
    4.0f * float2(-0.382683f, -0.923880f),
    4.0f * float2(0.000000f, -1.000000f),
    4.0f * float2(0.382684f, -0.923879f),
    4.0f * float2(0.707107f, -0.707107f),
    4.0f * float2(0.923880f, -0.382683f),

    6.0f * float2(1.000000f, 0.000000f),
    6.0f * float2(0.965926f, 0.258819f),
    6.0f * float2(0.866025f, 0.500000f),
    6.0f * float2(0.707107f, 0.707107f),
    6.0f * float2(0.500000f, 0.866026f),
    6.0f * float2(0.258819f, 0.965926f),
    6.0f * float2(-0.000000f, 1.000000f),
    6.0f * float2(-0.258819f, 0.965926f),
    6.0f * float2(-0.500000f, 0.866025f),
    6.0f * float2(-0.707107f, 0.707107f),
    6.0f * float2(-0.866026f, 0.500000f),
    6.0f * float2(-0.965926f, 0.258819f),
    6.0f * float2(-1.000000f, -0.000000f),
    6.0f * float2(-0.965926f, -0.258820f),
    6.0f * float2(-0.866025f, -0.500000f),
    6.0f * float2(-0.707106f, -0.707107f),
    6.0f * float2(-0.499999f, -0.866026f),
    6.0f * float2(-0.258819f, -0.965926f),
    6.0f * float2(0.000000f, -1.000000f),
    6.0f * float2(0.258819f, -0.965926f),
    6.0f * float2(0.500000f, -0.866025f),
    6.0f * float2(0.707107f, -0.707107f),
    6.0f * float2(0.866026f, -0.499999f),
    6.0f * float2(0.965926f, -0.258818f),
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float2 texelSize = float2(1.0f / max(ViewportWidth, 1.0f), 1.0f / max(ViewportHeight, 1.0f));
    float coc = abs(DOFCoCTexture.SampleLevel(PointClampSampler, input.uv, 0));
    float4 accum = SceneColorTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float weightSum = 1.0f;

    [unroll]
    for (int i = 0; i < 48; ++i)
    {
        float2 sampleUV = input.uv + offsets[i] * texelSize * coc;
        float sampleCoc = abs(DOFCoCTexture.SampleLevel(PointClampSampler, sampleUV, 0));
        float w = saturate(sampleCoc);
        accum += SceneColorTexture.SampleLevel(LinearClampSampler, sampleUV, 0) * w;
        weightSum += w;
    }

    return accum / max(weightSum, 0.0001f);
}
