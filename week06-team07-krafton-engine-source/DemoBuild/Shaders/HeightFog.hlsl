// HeightFog.hlsl
// Fullscreen post-process: Exponential Height Fog + SceneDepth visualization
// UE5 FogRendering.cpp / HeightFogCommon.ush 참고 구현
// UE5와 동일하게 exp2 (base-2) 사용

#include "Common/ConstantBuffers.hlsl"

Texture2D<float> DepthTex : register(t0);

struct PS_Input
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

//VS: Fullscreen Triangle (SV_VertexID)
PS_Input VS(uint vertexID : SV_VertexID)
{
    PS_Input output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

//하드웨어 깊이 → 월드 좌표 (CPU 계산 InvViewProj 사용)
float3 ReconstructWorldPos(float2 uv, float hwDepth)
{
    float4 ndc;
    ndc.x =  uv.x * 2.0 - 1.0;
    ndc.y = -(uv.y * 2.0 - 1.0);
    ndc.z = hwDepth;
    ndc.w = 1.0;

    float4 worldH = mul(ndc, InvViewProj);

    if (abs(worldH.w) < 1e-6)
        return CameraWorldPos;

    return worldH.xyz / worldH.w;
}

//선형 깊이
float LinearizeDepth(float hwDepth)
{
    return Projection[3][2] / (hwDepth - Projection[2][2]);
}

//UE5식 Exponential Height Fog (exp2 기반)
// UE5 HeightFogCommon.ush와 동일한 base-2 지수 사용
float4 GetExponentialHeightFog(float3 worldPos)
{
    float3 camToPixel = worldPos - CameraWorldPos;
    float rayLength = length(camToPixel);

    if (rayLength < 0.001 || isnan(rayLength) || isinf(rayLength))
        return float4(0, 0, 0, 0);

    // StartDistance / CutoffDistance 체크
    if (FogStartDistance > 0.0 && rayLength < FogStartDistance)
        return float4(0, 0, 0, 0);
    if (FogCutoffDistance > 0.0 && rayLength > FogCutoffDistance)
        return float4(0, 0, 0, 0);

    // 언리얼 엔진의 스케일 팩터 적용
    float falloff = max(FogHeightFalloff , 0.001);
    float density = FogDensity ;

    //densityAtCamera = FogDensity * exp2(-falloff * (cameraZ - fogHeight))
    float cameraRelHeight = CameraWorldPos.z - FogHeight;
    float densityExponent = clamp(-falloff * cameraRelHeight, -127.0, 127.0);
    float densityAtCamera = density * exp2(densityExponent);

    //광선 Z 방향에 따른 높이 적분 (exp2 기반)
    float rayDirZ = camToPixel.z;
    float exponent = falloff * rayDirZ;

    float lineIntegralFog;
    if (abs(exponent) > 0.01)
    {
        float clampedExp = clamp(-exponent, -127.0, 127.0);
        lineIntegralFog = densityAtCamera * (1.0 - exp2(clampedExp)) / exponent;
    }
    else
    {
        // 수평 근사 (exponent ≈ 0)
        lineIntegralFog = densityAtCamera;
    }

    float fogIntegral = max(lineIntegralFog * rayLength, 0.0);

    // StartDistance 보정
    if (FogStartDistance > 0.0)
    {
        float excludeRatio = saturate((rayLength - FogStartDistance) / max(rayLength, 0.001));
        fogIntegral *= excludeRatio;
    }

    // UE5: 가시도 = exp(-fogIntegral)
    fogIntegral = min(fogIntegral, 20.0);
    float visibility = exp(-fogIntegral);

    // MaxOpacity 제한
    visibility = max(visibility, 1.0 - FogMaxOpacity);

    float fogOpacity = saturate(1.0 - visibility);

    return float4(FogInscatteringColor.rgb, fogOpacity);
}

float4 PS(PS_Input input) : SV_TARGET
{
    int2 coord = int2(input.position.xy);
    float depth = DepthTex.Load(int3(coord, 0)).r;

    if (bSceneDepthMode)
    {
        if (depth >= 1.0)
            return float4(1, 1, 1, 1); 

        float linearZ = LinearizeDepth(depth);
        float nearZ = FogNearPlane;
        float farZ = FogFarPlane;
        if (farZ <= nearZ) farZ = nearZ + 1000.0;

        float normalized = saturate((linearZ - nearZ) / (farZ - nearZ));
        float gray = normalized; 
        return float4(gray, gray, gray, 1);
    }

    // Sky (depth=1)  완전 안개
    if (depth >= 1.0)
        return float4(FogInscatteringColor.rgb, FogMaxOpacity);

    float3 worldPos = ReconstructWorldPos(input.uv, depth);
    float4 fogResult = GetExponentialHeightFog(worldPos);

    // NaN/Inf 최종 방어
    if (any(isnan(fogResult)) || any(isinf(fogResult)))
        discard;

    if (fogResult.a < 0.001)
        discard;

    return fogResult;
}
