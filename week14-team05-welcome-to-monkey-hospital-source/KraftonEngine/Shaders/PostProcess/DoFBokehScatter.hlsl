// DoFBokehScatter.hlsl
// MVP highlight bokeh layer for bright large-CoC samples.

#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/DoFCommon.hlsli"

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 AccumulateBokehSprite(float2 uv, float2 texel)
{
    float3 bokeh = 0.0f;
    float totalWeight = 0.0f;
    int searchRadius = min((int)ceil(max(MaxBlurRadius, 1.0f)), DoFMaxBokehSearchRadius);
    float feather = max(DoFBokehMinFeather, MaxBlurRadius * DoFBokehFeatherScale);

    [loop]
    for (int y = -searchRadius; y <= searchRadius; ++y)
    {
        [loop]
        for (int x = -searchRadius; x <= searchRadius; ++x)
        {
            float2 offsetPixels = float2((float)x, (float)y);
            float distancePixels = length(offsetPixels);
            if (distancePixels > MaxBlurRadius + feather)
            {
                continue;
            }

            float2 sampleUV = uv + offsetPixels * texel;
            if (sampleUV.x < 0.0f || sampleUV.x > 1.0f || sampleUV.y < 0.0f || sampleUV.y > 1.0f)
            {
                continue;
            }

            float sourceCoC = CoCTexture.SampleLevel(LinearClampSampler, sampleUV, 0);
            float sourceRadius = abs(sourceCoC) * MaxBlurRadius;
            float coverage = smoothstep(-feather, feather, sourceRadius - distancePixels);
            if (coverage <= 0.0f)
            {
                continue;
            }

            float3 sourceColor = SceneColorTexture.SampleLevel(LinearClampSampler, sampleUV, 0).rgb;
            float candidate = DoFBokehCandidateMask(sourceCoC, sourceColor);
            if (candidate <= 0.0f)
            {
                continue;
            }

            float3 highlight = sourceColor;
            float weight = coverage * candidate;
            bokeh += highlight * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight <= DoFMinAccumulatedWeight)
    {
        return 0.0f;
    }

    return float4(bokeh / totalWeight, saturate(totalWeight));
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    return AccumulateBokehSprite(input.uv, DoFGetSceneTexel());
}
