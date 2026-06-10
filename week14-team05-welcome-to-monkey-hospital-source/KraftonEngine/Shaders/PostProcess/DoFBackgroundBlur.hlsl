// DoFBackgroundBlur.hlsl
// Depth-aware far/background blur from positive signed CoC.

#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/DoFCommon.hlsli"

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float ViewDistanceFromDepth(float2 uv, float depth)
{
    if (depth <= 0.0f)
    {
        return FocusDistance + max(FocusRange, DoFNearerSampleRejectBias);
    }

    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldH = mul(clipPos, InvViewProj);
    float3 worldPos = worldH.xyz / max(worldH.w, 1.0e-6f);
    return length(worldPos - CameraWorldPos);
}

float4 AccumulateBackground(float2 uv, float2 offset, float currentDistance, float kernelWeight, inout float totalWeight)
{
    float2 sampleUV = saturate(uv + offset);
    float sampleCoC = CoCTexture.SampleLevel(LinearClampSampler, sampleUV, 0);
    float sampleDepth = SceneDepthTexture.SampleLevel(PointClampSampler, sampleUV, 0);
    float sampleDistance = ViewDistanceFromDepth(sampleUV, sampleDepth);

    float bgWeight = saturate(sampleCoC);
    float nearerReject = sampleDistance + DoFNearerSampleRejectBias < currentDistance ? DoFNearerSampleRejectWeight : 1.0f;
    float weight = bgWeight * nearerReject * kernelWeight;
    float4 sampleColor = SceneColorTexture.SampleLevel(LinearClampSampler, sampleUV, 0);
    totalWeight += weight;
    return sampleColor * weight;
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float2 uv = input.uv;
    float4 sharp = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0);
    float centerCoC = CoCTexture.SampleLevel(LinearClampSampler, uv, 0);
    float centerBgCoC = saturate(centerCoC);

    float2 texel = DoFGetSceneTexel();
    float radius = max(centerBgCoC, DoFBackgroundMinCoCForRadius) * MaxBlurRadius;

    float centerDepth = SceneDepthTexture.SampleLevel(PointClampSampler, uv, 0);
    float centerDistance = ViewDistanceFromDepth(uv, centerDepth);

    float totalWeight = DoFBackgroundCenterWeight * centerBgCoC;
    float4 blur = sharp * totalWeight;
    float rotation = DoFStablePixelHash(uv) * 6.28318530718f;

    [unroll]
    for (int i = 0; i < DoFBlurSampleCount; ++i)
    {
        float2 diskOffset = DoFSpiralDiskOffset(i, DoFBlurSampleCount, rotation);
        float diskRadius = length(diskOffset);
        blur += AccumulateBackground(uv, texel * diskOffset * radius, centerDistance,
            DoFDiskSampleWeight(diskRadius), totalWeight);
    }

    float safeWeight = max(totalWeight, DoFMinAccumulatedWeight);
    if (totalWeight > DoFMinAccumulatedWeight)
    {
        return blur / safeWeight;
    }

    return sharp;
}
