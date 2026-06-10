#ifndef FOG_HLSLI
#define FOG_HLSLI

// =============================================================================
// Fog.hlsli — 지수 Height Fog 라인적분 (공용)
// =============================================================================
// HeightFog.hlsl(풀스크린 post-process)과 UberTransparent forward fog가 공유.
// worldPos/camPos 기반이라 깊이 복원과 무관 — UberLit 은 input.worldPos 를 그대로 넘기면 됨.
//
//   fogFactor [0, maxOpacity] 반환 → 호출부에서 lerp(sceneColor, FogColor, fogFactor) 또는
//   AlphaBlend 알파로 사용.
// =============================================================================
float ComputeHeightFogFactor(
    float3 worldPos, float3 camPos,
    float density, float heightFalloff, float baseHeight,
    float startDistance, float cutoffDistance, float maxOpacity)
{
    float3 rayDir = worldPos - camPos;
    float rayLength = length(rayDir);

    // 적분 유효 구간 (StartDistance 안쪽은 0 → fog 자연 소멸)
    float effectiveLength = max(rayLength - startDistance, 0.0);
    if (cutoffDistance > 0.0)
        effectiveLength = min(effectiveLength, cutoffDistance - startDistance);

    // 높이 h 에서 밀도: density * exp(-falloff * (h - baseHeight))
    // 카메라가 baseHeight 보다 매우 높아도 정밀도 손실 없도록 양 끝점에서 exp 분리.
    float rayDirZ = rayDir.z / max(rayLength, 0.001);
    float falloff = max(heightFalloff, 0.001);

    float startHeight = camPos.z + rayDirZ * startDistance - baseHeight;
    float endHeight = startHeight + rayDirZ * effectiveLength;

    float dz = rayDirZ * effectiveLength;
    float lineIntegral;
    if (abs(dz * falloff) > 0.001)
        lineIntegral = density * (exp(-falloff * startHeight) - exp(-falloff * endHeight)) / (falloff * rayDirZ);
    else
        lineIntegral = density * exp(-falloff * startHeight) * effectiveLength; // 거의 수평 ray: 밀도 근사 일정

    lineIntegral = max(lineIntegral, 0.0);
    float fogFactor = 1.0 - exp(-lineIntegral);
    return clamp(fogFactor, 0.0, maxOpacity);
}

#endif // FOG_HLSLI
