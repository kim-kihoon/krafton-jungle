#include "../Common.hlsl"

// GBuffer
Texture2D SceneColor : register(t0);
Texture2D SceneNormal : register(t1);
Texture2D SceneDepth : register(t2);
Texture2D ScenePrevPassColor : register(t3);
Texture2D SceneWorldPos : register(t4);

SamplerState SampleState : register(s0);

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

float3 ReconstructWorldFarPosition(int2 ip)
{
    float2 safeViewportSize = max(ViewportSize, float2(1.0f, 1.0f));
    float2 uv = (float2(ip) + 0.5f) / safeViewportSize;
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 clipFar = float4(ndc, 1.0f, 1.0f);
    float4 worldFar = mul(clipFar, InverseViewProjection);
    float safeW = (abs(worldFar.w) > 1.0e-4f) ? worldFar.w : 1.0f;
    return worldFar.xyz / safeW;
}

float ComputeFogTransmittance(FogLayerData fog, float rawDepth, float3 fogReferenceWorldPos)
{
    if (fog.FogDensity <= 0.0f || fog.FogCutoffDistance <= 0.0f)
        return 1.0f;

    float3 rayVector = fogReferenceWorldPos - CameraPosition.xyz;
    float referenceDistance = length(rayVector);
    if (referenceDistance <= 1.0e-4f)
        return 1.0f;

    if (rawDepth < 1.0f && referenceDistance >= fog.FogCutoffDistance)
        return 1.0f;

    float3 rayDirection = rayVector / referenceDistance;
    float sampleDistance = (rawDepth >= 1.0f) ? min(referenceDistance, fog.FogCutoffDistance) : referenceDistance;
    float sampleWorldZ = (rawDepth >= 1.0f)
        ? CameraPosition.z + rayDirection.z * sampleDistance
        : fogReferenceWorldPos.z;

    float scaledDensity = fog.FogDensity * 0.1f;
    float heightFactor = exp(-fog.HeightFalloff * max(sampleWorldZ - fog.FogHeight, 0.0f));
    float travelDistance = max(sampleDistance - fog.FogStartDistance, 0.0f);
    if (travelDistance <= 0.0f)
        return 1.0f;

    float layerTransmittance = saturate(exp(-scaledDensity * heightFactor * travelDistance));
    float layerOpacity = min(saturate(1.0f - layerTransmittance), fog.FogMaxOpacity);
    return saturate(1.0f - layerOpacity);
}

float4 mainPS(VSOutput input) : SV_TARGET
{
    int2 ip = int2(input.ClipPos.xy);
    float rawDepth = SceneDepth.Load(int3(ip, 0)).r;
    float4 prevPassColor = ScenePrevPassColor.Load(int3(ip, 0));
    float3 fogReferenceWorldPos = SceneWorldPos.Load(int3(ip, 0)).xyz;
    if (rawDepth >= 1.0f)
    {
        // Empty pixels keep cleared world position, so reconstruct a far-plane point on the view ray.
        fogReferenceWorldPos = ReconstructWorldFarPosition(ip);
    }

    const float MinTransmittance = 1e-4f;
    float totalOpticalDepth = 0.0f;
    float3 weightedFogColor = float3(0.0f, 0.0f, 0.0f);

    uint activeFogCount = min(FogLayerCount, (uint)MAX_FOG_LAYER_COUNT);
    [loop]
    for (uint fogIndex = 0; fogIndex < activeFogCount; ++fogIndex)
    {
        FogLayerData fog = FogLayers[fogIndex];
        float layerTransmittance = ComputeFogTransmittance(fog, rawDepth, fogReferenceWorldPos);
        if (layerTransmittance >= 1.0f)
            continue;

        float layerOpticalDepth = -log(max(layerTransmittance, MinTransmittance));
        totalOpticalDepth += layerOpticalDepth;
        weightedFogColor += fog.FogColor.rgb * layerOpticalDepth;
    }

    if (totalOpticalDepth <= 0.0f)
        return prevPassColor;

    float totalTransmittance = exp(-totalOpticalDepth);
    float3 fogColor = weightedFogColor / totalOpticalDepth;
    float3 outRgb = prevPassColor.rgb * totalTransmittance + fogColor * (1.0f - totalTransmittance);
    return float4(outRgb, prevPassColor.a);
}
