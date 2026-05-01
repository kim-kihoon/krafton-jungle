#ifndef SHADOW_FUNCTIONS_HLSLI
#define SHADOW_FUNCTIONS_HLSLI

#include "Common/ForwardLightData.hlsli"
#include "Common/ConstantBuffers.hlsli"
#include "Common/SystemSamplers.hlsli"

#ifndef SHADOW_METHOD_STANDARD
#define SHADOW_METHOD_STANDARD 0
#endif

#ifndef SHADOW_METHOD_PSM
#define SHADOW_METHOD_PSM 1
#endif

#ifndef SHADOW_METHOD_CSM
#define SHADOW_METHOD_CSM 2
#endif

#define SHADOW_BIAS 0.01f
#define SHADOW_SLOPE_BIAS 0.04f

// =========================================================================
// Shadow 계산 헬퍼 함수(Bias + Bleeding)
// =========================================================================

// FShadowInfo.ShadowParams.x에 저장된 constant bias를 셰이더 내부 배율과 함께 꺼낸다.
float GetShadowConstantBias(FShadowInfo info)
{
    return max(info.ShadowParams.x, 0.0f) * SHADOW_BIAS;
}

// FShadowInfo.ShadowParams.y에 저장된 slope bias를 셰이더 내부 배율과 함께 꺼낸다.
float GetShadowSlopeBias(FShadowInfo info)
{
    return max(info.ShadowParams.y, 0.0f) * SHADOW_SLOPE_BIAS;
}

// ShadowParams.z를 VSM light bleeding reduction 값으로 해석한다.
// 1.0에 가까우면 remap 분모가 너무 작아지므로 안전 범위로 clamp한다.
float GetShadowBleedReduction(FShadowInfo info)
{
    return clamp(info.ShadowParams.z, 0.0f, 0.5f);
}

// ShadowParams.w에 저장된 near plane 값을 안전한 최소값과 함께 반환한다.
float GetShadowNearZ(FShadowInfo info)
{
    return max(info.ShadowParams.w, 0.0001f);
}

// 길이가 너무 작은 벡터를 정규화할 때 NaN이 생기지 않도록 fallback 방향을 사용한다.
float3 SafeNormalize(float3 value, float3 fallback)
{
    float lenSq = dot(value, value);
    if (lenSq <= 1e-8f)
    {
        return fallback;
    }

    return value * rsqrt(lenSq);
}

float3 ApplyPSMNormalOffset(FShadowInfo info, float3 worldPos, float3 normal)
{
    if (info.bIsPSM)
    {
        const float PSMNormalBias = 0.01f;
        return worldPos + SafeNormalize(normal, float3(0.0f, 1.0f, 0.0f)) * PSMNormalBias;
    }
    return worldPos;
}

// 표면 법선과 광원 방향의 각도를 이용해 receiver bias를 만든다.
// 정면에서는 constant bias 위주로, grazing angle에서는 slope bias가 더 많이 붙는다.
float GetReceiverShadowBias(FShadowInfo info, float3 normal, float3 lightVector)
{
    float3 N = SafeNormalize(normal, float3(0.0f, 1.0f, 0.0f));
    float3 L = SafeNormalize(lightVector, float3(0.0f, 0.0f, 1.0f));
    float angleFactor = 1.0f - saturate(dot(N, L));

    // ShadowParams.x/y는 셰이더 샘플링 단계에서 쓰는 constant/slope bias 계약이다.
    // 렌더러 rasterizer depth bias와는 단위/적용 위치가 다르므로 여기서도 유지한다.
    float constantBias = GetShadowConstantBias(info);
    float slopeBias = GetShadowSlopeBias(info) * angleFactor;
    if (info.bIsPSM)
    {
        return max((constantBias + slopeBias) * 0.35f, 0.000005f);
    }
    return max(constantBias + slopeBias, 0.00001f);
}

// VSM 확률값을 remap해서 밝은 누수(light bleeding)를 줄인다.
// ShadowSharpen 프로퍼티를 reduction amount로 재해석한다.
float ReduceLightBleed(float probability, FShadowInfo info)
{
    float bleedReduction = GetShadowBleedReduction(info);
    return saturate((probability - bleedReduction) / (1.0f - bleedReduction));
}

// Point light cube shadow 샘플링에 필요한 방향, 비교 depth, tier를 한 번에 계산한다.
// 유효 범위를 벗어나면 false를 반환해서 호출부가 바로 lit 처리할 수 있게 한다.
bool BuildCubeShadowLookup(FShadowInfo info, float3 worldPos, out float3 outDir, out float outDepth, out uint outCubeTier)
{
    float3 lightPos = info.SampleData.xyz;
    float nearZ = GetShadowNearZ(info);
    float farZ = max(info.SampleData.w, nearZ + 0.0001f);

    float3 toPixel = worldPos - lightPos;
    float3 absToPixel = abs(toPixel);
    float faceDepth = max(max(absToPixel.x, absToPixel.y), absToPixel.z);
    if (faceDepth < nearZ || faceDepth > farZ)
    {
        outDir = float3(0.0f, 0.0f, 1.0f);
        outDepth = 0.0f;
        outCubeTier = 0u;
        return false;
    }

    outDir = toPixel / max(faceDepth, 0.0001f);
    outDepth = nearZ * (farZ / faceDepth - 1.0f) / (farZ - nearZ);
    outCubeTier = min(info.CubeTierIndex, 3u);
    return true;
}

// =========================================================================
// Hard Shadow(No Filter) Function
// =========================================================================

// 2D atlas shadow map을 한 번만 비교 샘플링하는 hard shadow 경로다.
// Directional/Spot shadow의 기본 비교 경로로 사용된다.
float SampleAtlasShadow(FShadowInfo info, float3 worldPos, float4x4 lightVP, float receiverBias)
{
    float4 shadowPos;
    if (info.bIsPSM)
    {
        shadowPos = mul(float4(worldPos, 1.0f), lightVP);
    }
    else
    {
        shadowPos = mul(float4(worldPos, 1.0f), lightVP);
    }

    if (abs(shadowPos.w) < 1e-5f)
    {
        return 1.0f;
    }

    float3 ndc = shadowPos.xyz / shadowPos.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = ndc.z + receiverBias;

    if (any(uv < 0.0f) || any(uv > 1.0f) || depth < 0.0f || depth > 1.0f)
    {
        return 1.0f;
    }

    float2 atlasMin = info.SampleData.xy;
    float2 atlasMax = info.SampleData.zw;
    float2 atlasUV = lerp(atlasMin, atlasMax, uv);

    return gShadowAtlasArray.SampleCmpLevelZero(
        ShadowCmpSampler,
        float3(atlasUV, info.ArrayIndex),
        depth);
}

// Cube shadow map을 한 번만 비교 샘플링하는 hard shadow 경로다.
// Point light shadow의 기본 비교 경로로 사용된다.
float SampleCubeShadow(FShadowInfo info, float3 worldPos, float receiverBias)
{
    float3 lightPos = info.SampleData.xyz;
    float nearZ = GetShadowNearZ(info);
    float farZ = max(info.SampleData.w, nearZ + 0.0001f);

    float3 toPixel = worldPos - lightPos;
    float3 absToPixel = abs(toPixel);
    float faceDepth = max(max(absToPixel.x, absToPixel.y), absToPixel.z);
    if (faceDepth < nearZ || faceDepth > farZ)
    {
        return 1.0f;
    }

    float3 dir = toPixel / max(faceDepth, 0.0001f);
    float depth = (nearZ * (farZ / faceDepth - 1.0f) / (farZ - nearZ)) + receiverBias;
    uint cubeTier = min(info.CubeTierIndex, 3u);


    if (cubeTier == 0u)
    {
        return gShadowCubeArrayTier0.SampleCmpLevelZero(
            ShadowCmpSampler,
            float4(dir, info.ArrayIndex),
            depth);
    }
    if (cubeTier == 1u)
    {
        return gShadowCubeArrayTier1.SampleCmpLevelZero(
            ShadowCmpSampler,
            float4(dir, info.ArrayIndex),
            depth);
    }
    if (cubeTier == 2u)
    {
        return gShadowCubeArrayTier2.SampleCmpLevelZero(
            ShadowCmpSampler,
            float4(dir, info.ArrayIndex),
            depth);
    }

    return gShadowCubeArrayTier3.SampleCmpLevelZero(
        ShadowCmpSampler,
        float4(dir, info.ArrayIndex),
        depth);
}

// =========================================================================
// PCF(Percentage-Closer Filtering) Functions
// =========================================================================

// Atlas shadow map에서 3x3 weighted PCF를 수행한다.
// 타일 경계를 넘어가면 옆 shadow map을 읽지 않도록 atlas UV를 clamp한다.
float SampleAtlasShadowPCF(FShadowInfo info, float3 worldPos, float4x4 lightVP, float receiverBias)
{
    float result = 1.0f;
    float4 shadowPos = mul(float4(worldPos, 1.0f), lightVP);
    bool validClip = true;

    if (info.bIsPSM != 0u)
    {
        shadowPos = mul(float4(worldPos, 1.0f), lightVP);
    }

    if (validClip && abs(shadowPos.w) >= 1e-5f)
    {
        float3 ndc = shadowPos.xyz / shadowPos.w;

        // NDC [-1, 1] -> UV [0, 1]
        float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;

        // 기존 Hard Shadow 함수와 동일한 depth convention 유지
        float depth = ndc.z + receiverBias;

        if (!any(uv < 0.0f) && !any(uv > 1.0f) && depth >= 0.0f && depth <= 1.0f)
        {
            float2 atlasMin = info.SampleData.xy;
            float2 atlasMax = info.SampleData.zw;
            float2 atlasUV = lerp(atlasMin, atlasMax, uv);

            uint atlasWidth;
            uint atlasHeight;
            uint atlasSlices;
            gShadowAtlasArray.GetDimensions(atlasWidth, atlasHeight, atlasSlices);

            float2 texelSize = 1.0f / float2(atlasWidth, atlasHeight);

            // atlas tile 경계를 넘어가면 옆 shadow map을 샘플링할 수 있으니 tile 내부로 제한
            float2 clampMin = atlasMin + texelSize * 0.5f;
            float2 clampMax = atlasMax - texelSize * 0.5f;

            float shadow = 0.0f;
            float weightSum = 0.0f;

            // 3x3 weighted PCF
            // weight:
            // 1 2 1
            // 2 4 2
            // 1 2 1
            [unroll]
            for (int y = -1; y <= 1; ++y)
            {
                [unroll]
                for (int x = -1; x <= 1; ++x)
                {
                    float2 offset = float2((float) x, (float) y);

                    float weight = 1.0f;
                    if (x == 0 && y == 0)
                    {
                        weight = 4.0f;
                    }
                    else if (x == 0 || y == 0)
                    {
                        weight = 2.0f;
                    }

                    float2 sampleUV = atlasUV + offset * texelSize;
                    sampleUV = clamp(sampleUV, clampMin, clampMax);

                    shadow += weight * gShadowAtlasArray.SampleCmpLevelZero(
                        ShadowCmpSampler,
                        float3(sampleUV, info.ArrayIndex),
                        depth);

                    weightSum += weight;
                }
            }

            result = shadow / weightSum;
        }
    }

    return result;
}

// Cube shadow map에서 방향 벡터 주변을 3x3으로 퍼뜨려 weighted PCF를 수행한다.
// 비교 depth는 cube shadow 전용 reversed-Z depth 규약을 그대로 따른다.
float SampleCubeShadowPCF(FShadowInfo info, float3 worldPos, float4x4 lightVP, float receiverBias)
{
    float3 lightPos = info.SampleData.xyz;
    float nearZ = GetShadowNearZ(info);
    float farZ = max(info.SampleData.w, nearZ + 0.0001f);

    float3 toPixel = worldPos - lightPos;
    float3 absToPixel = abs(toPixel);

    // Cube shadow에서는 perspective depth 기준으로 max axis 거리를 사용
    float faceDepth = max(max(absToPixel.x, absToPixel.y), absToPixel.z);

    if (faceDepth < nearZ || faceDepth > farZ)
    {
        return 1.0f;
    }

    // 기존 SampleCubeShadow와 같은 depth convention 유지
    float receiverDepth =
        (nearZ * (farZ / faceDepth - 1.0f) / (farZ - nearZ))
        + receiverBias;

    float3 dir = normalize(toPixel);

    uint cubeTier = min(info.CubeTierIndex, 3u);

    uint cubeWidth = 1;
    uint cubeHeight = 1;
    uint cubeElements = 1;
    uint cubeMips = 1;

    if (cubeTier == 0u)
    {
        gShadowCubeArrayTier0.GetDimensions(0, cubeWidth, cubeHeight, cubeElements, cubeMips);
    }
    else if (cubeTier == 1u)
    {
        gShadowCubeArrayTier1.GetDimensions(0, cubeWidth, cubeHeight, cubeElements, cubeMips);
    }
    else if (cubeTier == 2u)
    {
        gShadowCubeArrayTier2.GetDimensions(0, cubeWidth, cubeHeight, cubeElements, cubeMips);
    }
    else
    {
        gShadowCubeArrayTier3.GetDimensions(0, cubeWidth, cubeHeight, cubeElements, cubeMips);
    }

    // direction vector 주변으로 offset을 줄 기준 축 생성
    float3 up = abs(dir.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, dir));
    float3 bitangent = cross(dir, tangent);

    // 대략 cube texel 하나 정도의 angular offset
    float texelAngle = 1.5f / max((float) cubeWidth, 1.0f);

    float shadow = 0.0f;
    float weightSum = 0.0f;

    // 3x3 weighted PCF
    // weight:
    // 1 2 1
    // 2 4 2
    // 1 2 1
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float weight = 1.0f;

            if (x == 0 && y == 0)
            {
                weight = 4.0f;
            }
            else if (x == 0 || y == 0)
            {
                weight = 2.0f;
            }

            float3 sampleDir = normalize(
                dir
                + tangent * ((float) x * texelAngle)
                + bitangent * ((float) y * texelAngle)
            );

            float sampleShadow = 1.0f;

            if (cubeTier == 0u)
            {
                sampleShadow = gShadowCubeArrayTier0.SampleCmpLevelZero(
                    ShadowCmpSampler,
                    float4(sampleDir, info.ArrayIndex),
                    receiverDepth);
            }
            else if (cubeTier == 1u)
            {
                sampleShadow = gShadowCubeArrayTier1.SampleCmpLevelZero(
                    ShadowCmpSampler,
                    float4(sampleDir, info.ArrayIndex),
                    receiverDepth);
            }
            else if (cubeTier == 2u)
            {
                sampleShadow = gShadowCubeArrayTier2.SampleCmpLevelZero(
                    ShadowCmpSampler,
                    float4(sampleDir, info.ArrayIndex),
                    receiverDepth);
            }
            else
            {
                sampleShadow = gShadowCubeArrayTier3.SampleCmpLevelZero(
                    ShadowCmpSampler,
                    float4(sampleDir, info.ArrayIndex),
                    receiverDepth);
            }

            shadow += sampleShadow * weight;
            weightSum += weight;
        }
    }

    return shadow / weightSum;
}

// =========================================================================
// VSM(Variance Shadow Map) Functions
// =========================================================================

// Atlas에 저장된 VSM moments를 읽어 확률 기반 shadow 값을 계산한다.
// Raw reversed-Z를 저장하므로 receiver depth가 first moment보다 작을 때만 variance test를 수행한다.
// receiver depth가 first moment보다 뒤에 있을 때만 variance test를 수행한다.
float SampleAtlasShadowVSM(FShadowInfo info, float3 worldPos, float4x4 lightVP, float receiverBias)
{
    float4 lightClip;
    if (info.bIsPSM)
    {
        lightClip = mul(float4(worldPos, 1.0f), info.LightVP);
    }
    else
    {
        lightClip = mul(float4(worldPos, 1.0f), info.LightVP);
    }

    if (abs(lightClip.w) < 1e-5f)
    {
        return 1.0f;
    }

    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = ndc.z + receiverBias;

    if (any(uv < 0.0f) || any(uv > 1.0f) || depth < 0.0f || depth > 1.0f)
    {
        return 1.0f;
    }

    float2 atlasMin = info.SampleData.xy;
    float2 atlasMax = info.SampleData.zw;
    float2 atlasUV = lerp(atlasMin, atlasMax, uv);
    float2 moments = gShadowAtlasArray.SampleLevel(
        LinearClampSampler,
        float3(atlasUV, info.ArrayIndex),
        0.0f).xy;

    if (depth >= moments.x)
    {
        return 1.0f;
    }

    float variance = max(moments.y - moments.x * moments.x, 0.00002f);
    float delta = depth - moments.x;
    float probability = variance / (variance + delta * delta);
    return ReduceLightBleed(probability, info);
}

// Cube shadow용 VSM moments를 읽어 point light shadow 값을 계산한다.
// Raw reversed-Z를 저장하므로 receiver depth가 first moment보다 작을 때만 variance test를 적용한다.
// cube shadow depth를 VSM 저장 규약에 맞게 변환한 뒤 variance test를 적용한다.
float SampleCubeShadowVSM(FShadowInfo info, float3 worldPos, float4x4 lightVP, float receiverBias)
{
    float result = 1.0f;

    float3 lightPos = info.SampleData.xyz;
    float nearZ = GetShadowNearZ(info);
    float farZ = max(info.SampleData.w, nearZ + 0.0001f);

    float3 toPixel = worldPos - lightPos;
    float3 absToPixel = abs(toPixel);

    float faceDepth = max(max(absToPixel.x, absToPixel.y), absToPixel.z);

    if (faceDepth < nearZ || faceDepth > farZ)
    {
        return 1.0f;
    }

    float3 dir = normalize(toPixel);

    // 기존 SampleCubeShadow의 comparison depth와 같은 base depth
    float hardwareDepth =
        nearZ * (farZ / faceDepth - 1.0f) / (farZ - nearZ);
    
    float receiverDepth = saturate(hardwareDepth + receiverBias);

    // ShadowDepth.hlsl의 VSM 저장 방식:
    // depth = 1.0f - input.Position.z

    uint cubeTier = min(info.CubeTierIndex, 3u);

    float2 moments = float2(1.0f, 1.0f);

    if (cubeTier == 0u)
    {
        moments = gShadowCubeArrayTier0.SampleLevel(
            LinearClampSampler,
            float4(dir, info.ArrayIndex),
            0.0f).xy;
    }
    else if (cubeTier == 1u)
    {
        moments = gShadowCubeArrayTier1.SampleLevel(
            LinearClampSampler,
            float4(dir, info.ArrayIndex),
            0.0f).xy;
    }
    else if (cubeTier == 2u)
    {
        moments = gShadowCubeArrayTier2.SampleLevel(
            LinearClampSampler,
            float4(dir, info.ArrayIndex),
            0.0f).xy;
    }
    else
    {
        moments = gShadowCubeArrayTier3.SampleLevel(
            LinearClampSampler,
            float4(dir, info.ArrayIndex),
            0.0f).xy;
    }

    if (receiverDepth < moments.x)
    {
        float variance = max(moments.y - moments.x * moments.x, 0.00002f);
        float delta = receiverDepth - moments.x;
        float probability = variance / (variance + delta * delta);

        result = ReduceLightBleed(probability, info);
    }

    return result;

}

// =========================================================================
// Shadow 분기 처리 함수
// =========================================================================

// Point/Spot light shadow의 단일 진입점이다.
// shadow 타입(atlas/cube)과 현재 필터 모드(hard/PCF/VSM)에 맞는 샘플링 함수를 고른다.
float GetLightShadow(FLightInfo light, float3 worldPos, float3 normal)
{
    if (light.ShadowIndex < 0)
    {
        return 1.0f;
    }

    FShadowInfo info = gShadowInfos[light.ShadowIndex];
    float3 lightVector = light.Position - worldPos;
    float receiverBias = GetReceiverShadowBias(info, normal, lightVector);
    float shadow = 1.0f;
    if (info.Type == SHADOW_INFO_TYPE_ATLAS2D)
    {
#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
        shadow = SampleAtlasShadowVSM(info, worldPos, info.LightVP, receiverBias);
#elif defined(SHADOW_ENABLE_PCF) && SHADOW_ENABLE_PCF
        shadow = SampleAtlasShadowPCF(info, worldPos, info.LightVP, receiverBias);
#else
        shadow = SampleAtlasShadow(info, worldPos, info.LightVP, receiverBias);
#endif
    }
    else
    {
#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
        shadow = SampleCubeShadowVSM(info, worldPos, info.LightVP, receiverBias);
#elif defined(SHADOW_ENABLE_PCF) && SHADOW_ENABLE_PCF
        shadow = SampleCubeShadowPCF(info, worldPos, info.LightVP, receiverBias);
#else
        shadow = SampleCubeShadow(info, worldPos, receiverBias);
#endif
    }
    return shadow;
}

// =========================================================================
// Directional Light Shadow 분기 처리 함수
// =========================================================================

// Directional light shadow의 단일 진입점이다.
// CSM이면 cascade를 고르고, 아니면 단일 atlas shadow map을 사용한다.
float GetDirectionalShadow(float3 worldPos, float3 normal)
{
    if (DirectionalLight.ShadowIndex < 0)
    {
        return 1.0f;
    }

    float3 lightVector = -DirectionalLight.Direction;
    float shadow = 1.0f;
    if (ShadowMethod == SHADOW_METHOD_CSM)
    {
        float4 viewPos = mul(float4(worldPos, 1.0f), View);
        float depth = abs(viewPos.z);

        int cascadeIdx = 0;
        if (depth > CascadeSplits.x) cascadeIdx = 1;
        if (depth > CascadeSplits.y) cascadeIdx = 2;
        if (depth > CascadeSplits.z) cascadeIdx = 3;

        if (cascadeIdx >= (int) NumCascades)
        {
            return 1.0f;
        }

        FShadowInfo info = gShadowInfos[DirectionalLight.ShadowIndex + cascadeIdx];
        float receiverBias = GetReceiverShadowBias(info, normal, lightVector);

#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
        shadow = SampleAtlasShadowVSM(info, worldPos, CascadeMatrices[cascadeIdx], receiverBias);
#elif defined(SHADOW_ENABLE_PCF) && SHADOW_ENABLE_PCF
        shadow = SampleAtlasShadowPCF(info, worldPos, CascadeMatrices[cascadeIdx], receiverBias);
#else
        shadow = SampleAtlasShadow(info, worldPos, CascadeMatrices[cascadeIdx], receiverBias);
#endif
        return shadow;
    }

    FShadowInfo info = gShadowInfos[DirectionalLight.ShadowIndex];
    float3 shadowWorldPos = ApplyPSMNormalOffset(info, worldPos, normal);
    float receiverBias = GetReceiverShadowBias(info, normal, lightVector);
#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
    shadow = SampleAtlasShadowVSM(info, shadowWorldPos, info.LightVP, receiverBias);
#elif defined(SHADOW_ENABLE_PCF) && SHADOW_ENABLE_PCF
    shadow = SampleAtlasShadowPCF(info, shadowWorldPos, info.LightVP, receiverBias);
#else
    shadow = SampleAtlasShadow(info, shadowWorldPos, info.LightVP, receiverBias);
#endif
    return shadow;
}

#endif // SHADOW_FUNCTIONS_HLSLI
