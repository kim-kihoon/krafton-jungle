// DoFForegroundBlur.hlsl
// Foreground blur layer from negative signed CoC. Alpha carries the foreground mask.

#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/DoFCommon.hlsli"

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 AccumulateForeground(float2 uv, float2 offset, float distancePixels, float kernelWeight, inout float totalWeight, inout float totalCoverage, inout float totalKernelWeight)
{
    float2 sampleUV = saturate(uv + offset);
    float sampleCoC = CoCTexture.SampleLevel(LinearClampSampler, sampleUV, 0);
    float fgMask = saturate(-sampleCoC);
    float sampleRadius = max(fgMask, DoFForegroundMinCoCForRadius) * MaxBlurRadius;
    float apertureCoverage = smoothstep(-DoFForegroundGatherFeather, DoFForegroundGatherFeather, sampleRadius - distancePixels);
    float weight = fgMask * apertureCoverage * kernelWeight;
    float4 sampleColor = SceneColorTexture.SampleLevel(LinearClampSampler, sampleUV, 0);
    totalCoverage += fgMask * apertureCoverage * kernelWeight;
    totalKernelWeight += kernelWeight;
    totalWeight += weight;
    return sampleColor * weight;
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float2 uv = input.uv;
    float4 sharp = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0);
    float centerCoC = CoCTexture.SampleLevel(LinearClampSampler, uv, 0);
    float centerFgCoC = saturate(-centerCoC);

    float2 texel = DoFGetSceneTexel();
    float radius = MaxBlurRadius;

    float totalWeight = centerFgCoC * DoFForegroundCenterWeight;
    float totalCoverage = centerFgCoC * DoFForegroundCenterWeight;
    float totalKernelWeight = DoFForegroundCenterWeight;
    float4 blur = sharp * totalWeight;
    float rotation = DoFStablePixelHash(uv) * 6.28318530718f;

    [unroll]
    for (int i = 0; i < DoFBlurSampleCount; ++i)
    {
        float2 diskOffset = DoFSpiralDiskOffset(i, DoFBlurSampleCount, rotation);
        float diskRadius = length(diskOffset);
        blur += AccumulateForeground(uv, texel * diskOffset * radius,
            diskRadius * radius, DoFDiskSampleWeight(diskRadius), totalWeight, totalCoverage, totalKernelWeight);
    }

    float4 color = totalWeight > DoFMinAccumulatedWeight ? blur / max(totalWeight, DoFMinAccumulatedWeight) : sharp;
    color.a = saturate(totalCoverage / max(totalKernelWeight, DoFMinAccumulatedWeight));
    return color;
}
