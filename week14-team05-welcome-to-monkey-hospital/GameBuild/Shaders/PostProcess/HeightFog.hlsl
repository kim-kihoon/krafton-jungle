// HeightFog.hlsl
// Fullscreen Triangle VS (SV_VertexID) + Exponential Height Fog PS

#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/Fog.hlsli"

// b2 (PerShader0): Fog parameters
cbuffer FogBuffer : register(b2)
{
    float4 FogInscatteringColor;
    float FogDensity;
    float FogHeightFalloff;
    float FogBaseHeight;
    float FogStartDistance;
    float FogCutoffDistance;
    float FogMaxOpacity;
    float2 _fogPad;
};

// SceneDepthTexture (t16) is declared in Common/SystemResources.hlsli

// ── VS: Fullscreen Triangle ──
PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

// ── PS: Exponential Height Fog ──
float4 PS(PS_Input_UV input) : SV_TARGET
{
    int2 coord = int2(input.position.xy);

    // Sample hardware depth (Reversed-Z: 1=near, 0=far)
    float depth = SceneDepthTexture.Load(int3(coord, 0));
    if (depth <= 0.0)
    {
        // Sky/background: no geometry to reconstruct world position
        if (FogCutoffDistance > 0.0)
            discard; // CutoffDistance 설정 시 sky는 안개 범위 밖으로 취급
        return float4(FogInscatteringColor.rgb, FogMaxOpacity);
    }

    // Reconstruct world position from depth
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldH = mul(clipPos, InvViewProj);
    float3 worldPos = worldH.xyz / worldH.w;

    // 공용 height-fog 라인적분 (Common/Fog.hlsli) — UberTransparent forward fog 와 동일 수식
    float fogFactor = ComputeHeightFogFactor(
        worldPos, CameraWorldPos,
        FogDensity, FogHeightFalloff, FogBaseHeight,
        FogStartDistance, FogCutoffDistance, FogMaxOpacity);

    return float4(FogInscatteringColor.rgb, fogFactor);
}
