#include "UberSurface.hlsli"
#include "ShadowSample.hlsli"

#if !defined(MATERIAL_DOMAIN_DECAL) && !defined(LIGHTING_MODEL_GOURAUD) && !defined(LIGHTING_MODEL_LAMBERT) && !defined(LIGHTING_MODEL_PHONG) && !defined(LIGHTING_MODEL_TOON)
#define LIGHTING_MODEL_PHONG 1
#endif

cbuffer UberLighting : register(b3)
{
    uint SceneGlobalLightCount;
    float3 _UberLightingPad0;
}

struct FGPULight
{
    uint Type;
    float Intensity;
    float Radius;
    float FalloffExponent;

    float3 Color;
    float SpotInnerCos;

    float3 Position;
    float SpotOuterCos;

    float3 Direction;
    uint bCastShadows;

    int ShadowMapIndex;
    float ShadowBias;  
    float Padding0;
    float Padding1;
};

StructuredBuffer<FGPULight> GlobalLights : register(t3);

cbuffer VisibleLightInfo : register(b4)
{
    uint TileCountX;
    uint TileCountY;
    uint TileSize;
    uint MaxLightsPerTile;
    uint VisibleLightCount;
    float3 _VisibleLightInfoPad0;
}

struct FVisibleLightData
{
    float3 WorldPos;
    float Radius;
    
    float3 Color;
    float Intensity;
    
    float RadiusFalloff;
    uint Type;
    float SpotInnerCos;
    float SpotOuterCos;

    float3 Direction;
    uint bCastShadows;

    int ShadowMapIndex;
    float ShadowBias;
    float Padding0;
    float Padding1;
};

StructuredBuffer<FVisibleLightData> VisibleLights : register(t8);
StructuredBuffer<uint> TileVisibleLightCount : register(t9);
StructuredBuffer<uint> TileVisibleLightIndices : register(t10);

// ─────────────────── Shadows ───────────────────

struct FSpotShadowConstants
{
    row_major float4x4 LightViewProj;
    float4 AtlasRect; // xy = offset, zw = scale
    float ShadowSlopeBias;
    float ShadowBias;
    float SpotShadowSharpen;
    float ShadowFarPlane;
};

cbuffer SpotShadowInfo : register(b6)
{
    uint SpotShadowCount;
    uint SpotShadowFilterType;
    float2 _SpotShadowInfoPad0;
}

struct FPointShadowConstants
{
    row_major float4x4 LightViewProj[6];
    float3 LightPosition;
    float FarPlane;

    float4 FaceAtlasRects[6];
    
    float ShadowBias;
    float ShadowResolution;
    float ShadowSharpen;
    float ShadowSlopeBias;
    uint bHasShadowMap;
    float3 Padding;
};

#define MAX_CASCADE_COUNT 4
static const uint SHADOW_MODE_CSM = 0u;
static const uint SHADOW_MODE_PSM = 1u;
static const float PSM_SHADOW_BIAS_SCALE = 500.0f;

cbuffer DirectionalShadowInfo : register(b7)
{
    row_major float4x4 LightViewProj[MAX_CASCADE_COUNT];
    float4 SplitDistances;
    float4 CascadeRadius;
    
    float ShadowBias;
    float ShadowSlopeBias;
    float ShadowSharpen;
    uint bCascadeDebug;
    
    uint bHasShadowMap;
    uint ShadowFilterType;
    uint ShadowMode;
    float Padding0;
}

cbuffer PointShadowInfo : register(b8)
{
    uint PointShadowCount;
    uint PointAtlasResolution;
    uint PointShadowFilterType;
    float _PointShadowPad;
};

StructuredBuffer<FSpotShadowConstants> SpotShadowData : register(t11);
Texture2D<float> SpotShadowMap : register(t12);
Texture2D<float> DirectionalShadowMap : register(t13);

Texture2D<float2> SpotShadowVSMMap : register(t15);
Texture2D<float2> DirectionalShadowVSMMap : register(t16);

StructuredBuffer<FPointShadowConstants> PointShadowData  : register(t14);
Texture2D<float> PointShadowMap : register(t17);
Texture2D<float2> PointShadowVSMMap : register(t18);

static const int kCascadeShadowResoultion = 2048; // ShadowPass::CascadeShadowResolution과 일치
static const int kDirectionalAtlasResolution = 4096;

// ─────────────────── Lights ───────────────────

static const uint LIGHT_TYPE_DIRECTIONAL = 0u;
static const uint LIGHT_TYPE_POINT = 1u;
static const uint LIGHT_TYPE_SPOT = 2u;
static const uint LIGHT_TYPE_AMBIENT = 3u;
static const float3 DEFAULT_AMBIENT_COLOR = float3(0.02f, 0.02f, 0.02f);

static const uint SHADOW_FILTER_TYPE_PCF = 0u;
static const uint SHADOW_FILTER_TYPE_VSM = 1u;
static const uint SHADOW_FILTER_TYPE_ESM = 2u;
static const float POINT_SHADOW_FACE_EXTENT = 1.008765f; // tan(radians(90.5 * 0.5))

static const float3 kPointShadowFaceForward[6] = {
    float3( 1.0f,  0.0f,  0.0f),
    float3(-1.0f,  0.0f,  0.0f),
    float3( 0.0f,  1.0f,  0.0f),
    float3( 0.0f, -1.0f,  0.0f),
    float3( 0.0f,  0.0f,  1.0f),
    float3( 0.0f,  0.0f, -1.0f),
};

static const float3 kPointShadowFaceUp[6] = {
    float3( 0.0f,  0.0f,  1.0f),
    float3( 0.0f,  0.0f,  1.0f),
    float3( 0.0f,  0.0f,  1.0f),
    float3( 0.0f,  0.0f,  1.0f),
    float3(-1.0f,  0.0f,  0.0f),
    float3( 1.0f,  0.0f,  0.0f),
};


// ─────────────────── GameJam ───────────────────
Texture2D DecalMaskMap : register(t19);


float3 GetPointShadowFaceForward(uint FaceIndex)
{
    return kPointShadowFaceForward[FaceIndex];
}

float3 GetPointShadowFaceUp(uint FaceIndex)
{
    return kPointShadowFaceUp[FaceIndex];
}

uint SelectPointShadowFace(float3 DirectionFromLight)
{
    const float3 AbsDir = abs(DirectionFromLight);
    if (AbsDir.x >= AbsDir.y && AbsDir.x >= AbsDir.z)
    {
        return (DirectionFromLight.x >= 0.0f) ? 0u : 1u;
    }
    if (AbsDir.y >= AbsDir.x && AbsDir.y >= AbsDir.z)
    {
        return (DirectionFromLight.y >= 0.0f) ? 2u : 3u;
    }
    return (DirectionFromLight.z >= 0.0f) ? 4u : 5u;
}

float2 ProjectPointShadowDirectionToFaceUV(float3 DirectionFromLight, uint FaceIndex)
{
    const float3 Forward = GetPointShadowFaceForward(FaceIndex);
    const float3 Up = GetPointShadowFaceUp(FaceIndex);
    const float3 Right = normalize(cross(Up, Forward));
    const float ForwardDistance = max(dot(DirectionFromLight, Forward), 1.0e-5f);
    const float2 NDC = float2(
        dot(DirectionFromLight, Right) / (ForwardDistance * POINT_SHADOW_FACE_EXTENT),
        dot(DirectionFromLight, Up) / (ForwardDistance * POINT_SHADOW_FACE_EXTENT));
    return float2(NDC.x * 0.5f + 0.5f, 0.5f - NDC.y * 0.5f);
}

void BuildPointShadowDirectionBasis(float3 DirectionFromLight, out float3 Tangent, out float3 Bitangent)
{
    const float3 UpCandidate = abs(DirectionFromLight.z) < 0.999f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(0.0f, 1.0f, 0.0f);
    Tangent = normalize(cross(UpCandidate, DirectionFromLight));
    Bitangent = normalize(cross(DirectionFromLight, Tangent));
}

float2 InsetPointShadowLocalUV(float2 LocalUV, float ShadowResolution)
{
    const float Inset = 0.5f / max(ShadowResolution, 1.0f);
    return clamp(LocalUV, Inset.xx, (1.0f - Inset).xx);
}

float2 ClampPointShadowFilteredAtlasUV(float2 AtlasUV, float4 AtlasRect, int2 AtlasSize)
{
    const float2 Border = 6.0f / (float2)AtlasSize;
    const float2 MinUV = AtlasRect.xy + Border;
    const float2 MaxUV = AtlasRect.xy + AtlasRect.zw - Border;
    const float2 CenterUV = AtlasRect.xy + AtlasRect.zw * 0.5f;
    return (MaxUV.x <= MinUV.x || MaxUV.y <= MinUV.y)
        ? CenterUV
        : clamp(AtlasUV, MinUV, MaxUV);
}

float3 OffsetPointShadowReceiver(
    FPointShadowConstants Shadow,
    float3 WorldPos,
    float3 N,
    float3 DirectionFromLight,
    float LightShadowBias)
{
    const float3 ToLight = -DirectionFromLight;
    float CosTheta = saturate(dot(normalize(N), ToLight));
    const float TexelWorldSize = Shadow.FarPlane / max(Shadow.ShadowResolution, 1.0f);
    const float BiasWorldSize = max(LightShadowBias, Shadow.ShadowBias) * Shadow.FarPlane;
    const float NormalOffset = min(max(BiasWorldSize, TexelWorldSize * 0.25f), TexelWorldSize);

    return WorldPos + normalize(N) * NormalOffset * (1.0f - CosTheta);
}

float3 PointShadowLocalUVToDirection(uint FaceIndex, float2 LocalUV)
{
    const float3 Forward = GetPointShadowFaceForward(FaceIndex);
    const float3 Up = GetPointShadowFaceUp(FaceIndex);
    const float3 Right = normalize(cross(Up, Forward));
    const float NDCx = LocalUV.x * 2.0f - 1.0f;
    const float NDCy = 1.0f - LocalUV.y * 2.0f;
    return normalize(Forward + Right * (NDCx * POINT_SHADOW_FACE_EXTENT) + Up * (NDCy * POINT_SHADOW_FACE_EXTENT));
}

float2 LoadPointShadowVSMMomentDirectional(
    FPointShadowConstants Shadow,
    float3 SampleDirection,
    int AtlasSize)
{
    const uint Face = SelectPointShadowFace(SampleDirection);
    const float2 SampleLocalUV = InsetPointShadowLocalUV(
        ProjectPointShadowDirectionToFaceUV(SampleDirection, Face),
        Shadow.ShadowResolution);
    const float4 SampleAtlasRect = Shadow.FaceAtlasRects[Face];
    const int2 SampleTileBase = (int2)(SampleAtlasRect.xy * (float)AtlasSize);
    const int2 SampleTileSpan = (int2)(SampleAtlasRect.zw * (float)AtlasSize);
    const int2 SampleTileMax = SampleTileBase + SampleTileSpan - int2(1, 1);
    const float2 SampleAtlasUV = SampleAtlasRect.xy + SampleLocalUV * SampleAtlasRect.zw;
    const int2 SampleTexel = clamp((int2)floor(SampleAtlasUV * (float)AtlasSize), SampleTileBase, SampleTileMax);
    return PointShadowVSMMap.Load(int3(SampleTexel, 0)).xy;
}

float2 SamplePointShadowMomentsBilinearCubeAware(
    FPointShadowConstants Shadow,
    float3 DirectionFromLight,
    int AtlasSize)
{
    const uint CenterFace = SelectPointShadowFace(DirectionFromLight);
    const float2 CenterLocalUV = ProjectPointShadowDirectionToFaceUV(DirectionFromLight, CenterFace);
    const float Resolution = max(Shadow.ShadowResolution, 1.0f);
    const float2 PixelPos = CenterLocalUV * Resolution - 0.5f.xx;
    const float2 BasePixel = floor(PixelPos);
    const float2 Frac = saturate(PixelPos - BasePixel);

    float2 Corners[4];
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const float2 CornerOffset = float2((i & 1) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f);
        const float2 CornerLocalUV = (BasePixel + CornerOffset + 0.5f.xx) / Resolution;
        const float3 SampleDirection = PointShadowLocalUVToDirection(CenterFace, CornerLocalUV);
        Corners[i] = LoadPointShadowVSMMomentDirectional(Shadow, SampleDirection, AtlasSize);
    }
    const float2 M0 = lerp(Corners[0], Corners[1], Frac.x);
    const float2 M1 = lerp(Corners[2], Corners[3], Frac.x);
    return lerp(M0, M1, Frac.y);
}

float4 GetDirectionalCascadeAtlasRect(int CascadeIndex)
{
    if (CascadeIndex == 0) return float4(0.0f, 0.0f, 0.5f, 0.5f);
    if (CascadeIndex == 1) return float4(0.5f, 0.0f, 0.5f, 0.5f);
    if (CascadeIndex == 2) return float4(0.0f, 0.5f, 0.5f, 0.5f);
    return float4(0.5f, 0.5f, 0.5f, 0.5f);
}

int GetCascadeIndex(float3 WorldPos)
{
    float ViewDepth = mul(float4(WorldPos, 1.0f), View).x;

    int CascadeIndex = (int)(step(SplitDistances.x, ViewDepth));
    CascadeIndex += step(SplitDistances.y, ViewDepth);
    CascadeIndex += step(SplitDistances.z, ViewDepth);
    return min(CascadeIndex, MAX_CASCADE_COUNT - 1);
}

// 뷰 공간 깊이로 Cascade Index를 결정한다.
float SampleDirectionalShadowAtIndex(float3 WorldPos, float3 N, float3 L, int ShadowIndex)
{
    const float BiasScale = (ShadowMode == SHADOW_MODE_PSM) ? PSM_SHADOW_BIAS_SCALE : max(SplitDistances.w, 1.0e-4f);
    const float NormalizedBias = ShadowBias / BiasScale;
    const float NormalizedSlopeBias = ShadowSlopeBias / BiasScale;
    
    float CosTheta = saturate(dot(N, L));
    CosTheta = max(CosTheta, 1e-4f);
    float TanTheta = sqrt(1.0 - CosTheta * CosTheta) / CosTheta;
    TanTheta = min(TanTheta, 2.0f);
    
    const float Bias = NormalizedBias + (NormalizedSlopeBias * TanTheta);
    const float NormalOffsetScale = 0.3f; // Noraml Offset Bias
    const float3 OffsetWorldPos = WorldPos + N * NormalOffsetScale * (1.0f - CosTheta);
    const float4 ShadowClip = mul(float4(OffsetWorldPos, 1.0f), LightViewProj[ShadowIndex]);
    
    const float W = (ShadowClip.w <= 1.0e-5f) ? 1.0f : ShadowClip.w;
    const float3 ShadowNDC = ShadowClip.xyz / W;
    const float InBounds =
        step(abs(ShadowNDC.x), 1.0f) *
        step(abs(ShadowNDC.y), 1.0f) *
        step(0.0f, ShadowNDC.z) *
        step(ShadowNDC.z, 1.0f) *
        step(1.0e-5f, ShadowClip.w);
    if (InBounds <= 0.0f)
    {
        return 1.0f;
    }

    int CascadeIndex = GetCascadeIndex(WorldPos);
    
    float2 LocalUV = float2(ShadowNDC.x * 0.5f + 0.5f, ShadowNDC.y * -0.5f + 0.5f);
    float4 AtlasRect = GetDirectionalCascadeAtlasRect(ShadowIndex);
    float2 AtlasUV = AtlasRect.xy + LocalUV * AtlasRect.zw;
    
    int2 AtlasSize = int2(kDirectionalAtlasResolution, kDirectionalAtlasResolution);
    
    const float CurrentDepth = ShadowNDC.z;
    
    if (ShadowFilterType == SHADOW_FILTER_TYPE_PCF)
    {
        return SampleShadowPoissonDisk(AtlasUV, CurrentDepth - Bias, DirectionalShadowMap, AtlasSize, ShadowSharpen);
    }
    else if (ShadowFilterType == SHADOW_FILTER_TYPE_ESM)
    {
        const float2 ClampedAtlasUV = ClampShadowUVToAtlasRect(AtlasUV, AtlasRect, AtlasSize);
        return SampleShadowESM(ClampedAtlasUV, CurrentDepth - Bias, DirectionalShadowVSMMap, AtlasSize, ShadowSharpen);
    }
    else
    {
        const float2 ClampedAtlasUV = ClampShadowUVToAtlasRect(AtlasUV, AtlasRect, AtlasSize);
        return SampleShadowVSM(ClampedAtlasUV, CurrentDepth - Bias, DirectionalShadowVSMMap, AtlasSize, ShadowSharpen);
    }
}

float ComputeDirectionalShadowFactor(float3 WorldPos, float3 N, float3 L)
{
    if (bHasShadowMap == 0u)
    {
        return 1.0f;
    }

    const int ShadowIndex = (ShadowMode == SHADOW_MODE_PSM) ? 0 : GetCascadeIndex(WorldPos);
    return SampleDirectionalShadowAtIndex(WorldPos, N, L, ShadowIndex);
}

float ComputePointShadowFactor(float3 WorldPos, float3 N, uint bCastShadows, int ShadowMapIndex, float LightShadowBias)
{
    if (bCastShadows == 0u || ShadowMapIndex < 0)
        return 1.0f;

    const uint Slice = (uint)ShadowMapIndex;
    if (Slice >= PointShadowCount)
        return 1.0f;

    const FPointShadowConstants Shadow = PointShadowData[Slice];
    if (Shadow.bHasShadowMap == 0u)
        return 1.0f;

    const float3 InitialToFromLight = WorldPos - Shadow.LightPosition;
    const float InitialDist = length(InitialToFromLight);

    if (InitialDist <= 1.0e-5f || InitialDist >= Shadow.FarPlane)
        return 1.0f;

    const float3 InitialDirectionFromLight = InitialToFromLight / InitialDist;
    const float3 ShadowWorldPos = OffsetPointShadowReceiver(Shadow, WorldPos, N, InitialDirectionFromLight, LightShadowBias);
    const float3 ToFromLight = ShadowWorldPos - Shadow.LightPosition;
    const float Dist = length(ToFromLight);

    if (Dist <= 1.0e-5f || Dist >= Shadow.FarPlane)
        return 1.0f;

    const float CurrentDepth = Dist / Shadow.FarPlane;
    const float3 L = -ToFromLight / Dist;

    float CosTheta = saturate(dot(N, L));
    CosTheta = max(CosTheta, 1.0e-4f);
    float TanTheta = sqrt(1.0f - CosTheta * CosTheta) / CosTheta;
    TanTheta = min(TanTheta, 2.0f);

    const float NormalizedSlopeBias = Shadow.ShadowSlopeBias / max(Shadow.FarPlane, 1.0e-4f);
    const float Bias = max(LightShadowBias, Shadow.ShadowBias) + NormalizedSlopeBias * TanTheta;

    const float3 DirectionFromLight = ToFromLight / Dist;
    uint FaceIndex = SelectPointShadowFace(DirectionFromLight);

    const float4 ShadowClip = mul(float4(ShadowWorldPos, 1.0f), Shadow.LightViewProj[FaceIndex]);
    if (ShadowClip.w <= 1.0e-5f)
        return 1.0f;

    const float3 ShadowNDC = ShadowClip.xyz / ShadowClip.w;
    if (ShadowNDC.x < -1.0f || ShadowNDC.x > 1.0f ||
        ShadowNDC.y < -1.0f || ShadowNDC.y > 1.0f ||
        ShadowNDC.z < 0.0f || ShadowNDC.z > 1.0f)
    {
        return 1.0f;
    }

    const float2 LocalUV = InsetPointShadowLocalUV(float2(
        ShadowNDC.x * 0.5f + 0.5f,
        0.5f - ShadowNDC.y * 0.5f), Shadow.ShadowResolution);

    const float4 AtlasRect = Shadow.FaceAtlasRects[FaceIndex];

    // SampleLevel은 인접 face 타일과 bilinear blend가 일어남 → cube 경계에 누설.
    // 명시적으로 타일 픽셀 범위에 clamp한 뒤 Load(point fetch)로 읽어와 누설 차단.
    const int AtlasSize = (int)PointAtlasResolution;
    const int2 TileBase  = (int2)(AtlasRect.xy * (float)AtlasSize);
    const int2 TileSpan  = (int2)(AtlasRect.zw * (float)AtlasSize);
    const int2 TileMax   = TileBase + TileSpan - int2(1, 1);

    const float2 AtlasUV   = AtlasRect.xy + LocalUV * AtlasRect.zw;
    const int2   GuardTileBase = min(TileBase + int2(1, 1), TileMax);
    const int2   GuardTileMax = max(TileMax - int2(1, 1), TileBase);

    if (PointShadowFilterType == SHADOW_FILTER_TYPE_PCF)
    {
        float ShadowFactor = 0.0f;
        const float Spread = (1.0f - Shadow.ShadowSharpen) * 2.0f;
        const float ShadowResolution = max(Shadow.ShadowResolution, 1.0f);
        const float DirectionTexelSize = 2.0f / max(Shadow.ShadowResolution, 1.0f);
        const float2 LocalTexelSize = 1.0f.xx / ShadowResolution;
        const float FaceBorder = (Spread + 2.0f) / ShadowResolution;
        const bool bNeedsCubeAwarePCF =
            LocalUV.x < FaceBorder || LocalUV.x > 1.0f - FaceBorder ||
            LocalUV.y < FaceBorder || LocalUV.y > 1.0f - FaceBorder;
        const float Angle = frac(sin(dot(DirectionFromLight, float3(12.9898, 78.233, 37.719))) * 43758.5453) * 6.283185f;
        const float2x2 RotationMatrix = float2x2(cos(Angle), -sin(Angle), sin(Angle), cos(Angle));

        if (!bNeedsCubeAwarePCF)
        {
            [loop]
            for (int SampleIndex = 0; SampleIndex < 16; ++SampleIndex)
            {
                const float2 SampleLocalUV = LocalUV + mul(PoissonDisk[SampleIndex], RotationMatrix) * LocalTexelSize * Spread;
                const float2 SampleAtlasUV = AtlasRect.xy + SampleLocalUV * AtlasRect.zw;
                const int2 SampleTexel = clamp((int2)floor(SampleAtlasUV * (float)AtlasSize), GuardTileBase, GuardTileMax);
                const float StoredDepth = PointShadowMap.Load(int3(SampleTexel, 0));
                ShadowFactor += ((CurrentDepth - Bias) <= StoredDepth) ? 1.0f : 0.0f;
            }
        }
        else
        {
            float3 DirectionTangent = 0.0f.xxx;
            float3 DirectionBitangent = 0.0f.xxx;
            BuildPointShadowDirectionBasis(DirectionFromLight, DirectionTangent, DirectionBitangent);

            [loop]
            for (int SampleIndex = 0; SampleIndex < 16; ++SampleIndex)
            {
                const float2 Offset = mul(PoissonDisk[SampleIndex], RotationMatrix) * DirectionTexelSize * Spread;
                const float3 SampleDirection = normalize(DirectionFromLight + DirectionTangent * Offset.x + DirectionBitangent * Offset.y);
                const uint SampleFaceIndex = SelectPointShadowFace(SampleDirection);
                const float2 SampleLocalUV = InsetPointShadowLocalUV(
                    ProjectPointShadowDirectionToFaceUV(SampleDirection, SampleFaceIndex),
                    Shadow.ShadowResolution);
                const float4 SampleAtlasRect = Shadow.FaceAtlasRects[SampleFaceIndex];
                const int2 SampleTileBase = (int2)(SampleAtlasRect.xy * (float)AtlasSize);
                const int2 SampleTileSpan = (int2)(SampleAtlasRect.zw * (float)AtlasSize);
                const int2 SampleTileMax = SampleTileBase + SampleTileSpan - int2(1, 1);
                const int2 SampleGuardTileBase = min(SampleTileBase + int2(1, 1), SampleTileMax);
                const int2 SampleGuardTileMax = max(SampleTileMax - int2(1, 1), SampleTileBase);
                const float2 SampleAtlasUV = SampleAtlasRect.xy + SampleLocalUV * SampleAtlasRect.zw;
                const int2 SampleTexel = clamp((int2)floor(SampleAtlasUV * (float)AtlasSize), SampleGuardTileBase, SampleGuardTileMax);
                const float StoredDepth = PointShadowMap.Load(int3(SampleTexel, 0));
                ShadowFactor += ((CurrentDepth - Bias) <= StoredDepth) ? 1.0f : 0.0f;
            }
        }

        return ShadowFactor / 16.0f;
    }
    else if (PointShadowFilterType == SHADOW_FILTER_TYPE_ESM)
    {
        const float2 Moments = SamplePointShadowMomentsBilinearCubeAware(Shadow, DirectionFromLight, AtlasSize);
        return SampleShadowESMFromStored(Moments.x, CurrentDepth - Bias, Shadow.ShadowSharpen);
    }
    else
    {
        const float2 Moments = SamplePointShadowMomentsBilinearCubeAware(Shadow, DirectionFromLight, AtlasSize);
        return SampleShadowVSMFromMoments(Moments, CurrentDepth - Bias, Shadow.ShadowSharpen);
    }
}

// ─────────────────── Lights ───────────────────
struct FLightingResult
{
    float3 Diffuse;
    float3 Specular;
};

float ComputeDistanceAttenuation(float Distance, float Radius, float FalloffExponent)
{
    if (Radius <= 0.0f)
    {
        return 0.0f;
    }

    const float T = saturate(1.0f - (Distance / max(Radius, 1.0e-4f)));
    return pow(T, max(FalloffExponent, 1.0e-4f));
}

void AccumulateDirectLight(float3 WorldPos, float3 N, float3 V, float3 L, float3 LightContribution, inout FLightingResult Result)
{
#if defined(LIGHTING_MODEL_TOON)
    // --- Diffuse: 3-band 계단 처리 ---
    const float HalfLambert = dot(N, L) * 0.5f + 0.5f;    

    float ToonDiffuse;
    if (HalfLambert > 0.75f)
        ToonDiffuse = 1.0f;       // 밝은 영역
    else if (HalfLambert > 0.4f)
        ToonDiffuse = 0.6f;       // 중간 영역
    else
        ToonDiffuse = 0.15f;      // 그림자 영역

    Result.Diffuse += LightContribution * ToonDiffuse;

    /*
    const float3 H = normalize(L + V);
    const float NdotH = saturate(dot(N, H));
    const float ToonSpec = step(0.97f, pow(NdotH, max(Shininess, 1.0e-4f)));
    Result.Specular += SpecularColor * LightContribution * ToonSpec;
    
    const float RimDot = 1.0f - saturate(dot(N, V));
    // step으로 끊어서 툰 느낌 유지
    const float RimIntensity = step(0.6f, RimDot * saturate(dot(L, V) + 0.5f));
    Result.Diffuse += LightContribution * RimIntensity * 0.4f;
    */    
    
#else
    // disable half lambert
    // const float NdotL = saturate(dot(N, L) * 0.5f + 0.5f);
    const float NdotL = saturate(dot(N, L));
    Result.Diffuse += LightContribution * NdotL;

#if defined(LIGHTING_MODEL_GOURAUD) || defined(LIGHTING_MODEL_PHONG)
    const float3 H = normalize(L + V);
    const float SpecularPower = pow(saturate(dot(N, H)), max(Shininess, 1.0e-4f));
    Result.Specular += SpecularColor * LightContribution * SpecularPower;
#endif
#endif
}

float ComputeSpotShadowFactor(float3 WorldPos, float3 N, float3 L, uint bCastShadows, int ShadowMapIndex, float LightShadowBias)
{
    if (bCastShadows == 0u || ShadowMapIndex < 0)
    {
        return 1.0f;
    }

    const uint ShadowSlice = (uint)ShadowMapIndex;
    if (ShadowSlice >= SpotShadowCount)
    {
        return 1.0f;
    }

    const FSpotShadowConstants Shadow = SpotShadowData[ShadowSlice];

    const float4 ShadowClip = mul(float4(WorldPos, 1.0f), Shadow.LightViewProj);
    if (ShadowClip.w <= 1.0e-5f)
    {
        return 1.0f;
    }

    const float3 ShadowNDC = ShadowClip.xyz / ShadowClip.w;
    if (ShadowNDC.x < -1.0f || ShadowNDC.x > 1.0f ||
        ShadowNDC.y < -1.0f || ShadowNDC.y > 1.0f ||
        ShadowNDC.z < 0.0f || ShadowNDC.z > 1.0f)
    {
        return 1.0f;
    }

    const float2 LocalUV = float2(
        ShadowNDC.x * 0.5f + 0.5f,
        0.5f - ShadowNDC.y * 0.5f);

    const float2 AtlasUV = Shadow.AtlasRect.xy + LocalUV * Shadow.AtlasRect.zw;
    const int2 AtlasSize = int2(4096, 4096);
    
    const float CurrentDepth = ShadowNDC.z;
    const float LinearDepth = saturate(ShadowClip.w / max(Shadow.ShadowFarPlane, 1.0e-4f));

    float CosTheta = saturate(dot(N, L));
    CosTheta = max(CosTheta, 1.0e-4f);
    float TanTheta = sqrt(1.0f - CosTheta * CosTheta) / CosTheta;
    TanTheta = min(TanTheta, 2.0f);

    const float NormalizedSlopeBias = Shadow.ShadowSlopeBias / max(Shadow.ShadowFarPlane, 1.0e-4f);
    const float Bias = max(LightShadowBias, Shadow.ShadowBias) + NormalizedSlopeBias * TanTheta;
    
    if (SpotShadowFilterType == SHADOW_FILTER_TYPE_PCF)
    {
        return SampleShadowPoissonDisk(AtlasUV, CurrentDepth - Bias, SpotShadowMap, AtlasSize, Shadow.SpotShadowSharpen);
    }
    else if (SpotShadowFilterType == SHADOW_FILTER_TYPE_ESM)
    {
        const float2 ClampedAtlasUV = ClampShadowUVToAtlasRect(AtlasUV, Shadow.AtlasRect, AtlasSize);
        return SampleShadowESM(ClampedAtlasUV, LinearDepth - Bias, SpotShadowVSMMap, AtlasSize, Shadow.SpotShadowSharpen);
    }
    else
    {
        const float2 ClampedAtlasUV = ClampShadowUVToAtlasRect(AtlasUV, Shadow.AtlasRect, AtlasSize);
        return SampleShadowVSM(ClampedAtlasUV, LinearDepth - Bias, SpotShadowVSMMap, AtlasSize, Shadow.SpotShadowSharpen);
    }
}

void AccumulateVisiblePointLights(float3 WorldPos, float3 N, float3 V, float2 ScreenPos, inout FLightingResult Result)
{
    if (VisibleLightCount == 0u || TileCountX == 0u || TileCountY == 0u || TileSize == 0u || MaxLightsPerTile == 0u)
    {
        return;
    }

    const uint TileX = min((uint)ScreenPos.x / TileSize, TileCountX - 1u);
    const uint TileY = min((uint)ScreenPos.y / TileSize, TileCountY - 1u);
    const uint TileIndex = TileY * TileCountX + TileX;

    const uint LocalCount = min(TileVisibleLightCount[TileIndex], MaxLightsPerTile);
    const uint TileOffset = TileIndex * MaxLightsPerTile;

    [loop]
    for (uint VisIdx = 0u; VisIdx < LocalCount; ++VisIdx)
    {
        const uint LightIndex = TileVisibleLightIndices[TileOffset + VisIdx];
        if (LightIndex >= VisibleLightCount)
        {
            continue;
        }

        const FVisibleLightData Light = VisibleLights[LightIndex];
        const float3 ToLight = Light.WorldPos - WorldPos;
        const float Distance = length(ToLight);
        if (Distance <= 1.0e-4f || Distance >= Light.Radius)
        {
            continue;
        }

        const float3 L = ToLight / Distance;
        float Att = ComputeDistanceAttenuation(Distance, Light.Radius, Light.RadiusFalloff);
        if (Att <= 0.0f)
        {
            continue;
        }

        if (Light.Type == LIGHT_TYPE_SPOT)
        {
            const float3 SpotDir = normalize(Light.Direction);
            const float CosAngle = dot(SpotDir, -L);
            const float ConeRange = max(Light.SpotInnerCos - Light.SpotOuterCos, 1.0e-4f);
            Att *= saturate((CosAngle - Light.SpotOuterCos) / ConeRange);
            if (Att <= 0.0f)
            {
                continue;
            }

            Att *= ComputeSpotShadowFactor(WorldPos, N, L, Light.bCastShadows, Light.ShadowMapIndex, Light.ShadowBias);
            if (Att <= 0.0f)
            {
                continue;
            }
        }
        else if (Light.Type == LIGHT_TYPE_POINT)
        {
            Att *= ComputePointShadowFactor(WorldPos, N, Light.bCastShadows, Light.ShadowMapIndex, Light.ShadowBias);
            if (Att <= 0.0f)
            {
                continue;
            }
        }

        AccumulateDirectLight(WorldPos, N, V, L, Light.Color * Light.Intensity * Att, Result);
    }
}

FLightingResult EvaluateLightingFromWorld(float3 WorldPos, float3 WorldNormal, float2 ScreenPos)
{
    FLightingResult Result;
    Result.Diffuse = 0.0f.xxx;
    Result.Specular = 0.0f.xxx;

    const float3 N = normalize(WorldNormal);
    const float3 V = normalize(CameraPosition - WorldPos);

    float3 AmbientContribution = DEFAULT_AMBIENT_COLOR;
    uint bHasAmbientLight = 0u;

    [loop]
    for (uint LightIndex = 0u; LightIndex < SceneGlobalLightCount; ++LightIndex)
    {
        const FGPULight Light = GlobalLights[LightIndex];
        const float3 LightColor = Light.Color * Light.Intensity;

        if (Light.Type == LIGHT_TYPE_AMBIENT)
        {
            if (bHasAmbientLight == 0u)
            {
                AmbientContribution = 0.0f.xxx;
                bHasAmbientLight = 1u;
            }

            AmbientContribution += LightColor;
            continue;
        }

        if (Light.Type == LIGHT_TYPE_DIRECTIONAL)
        {
            float ShadowFactor = 1.0f;
            if (Light.bCastShadows != 0u)
            {
                ShadowFactor = ComputeDirectionalShadowFactor(WorldPos, N, normalize(Light.Direction));
            }

            AccumulateDirectLight(WorldPos, N, V, normalize(Light.Direction), LightColor * ShadowFactor, Result);

            if (bCascadeDebug != 0u && ShadowMode == SHADOW_MODE_CSM)
            {
                int CascadeIndex = GetCascadeIndex(WorldPos);
                float3 DebugColors[4] = {
                    float3(1.0, 0.2, 0.2), // Reddish
                    float3(0.2, 1.0, 0.2), // Greenish
                    float3(0.2, 0.2, 1.0), // Blueish
                    float3(1.0, 1.0, 0.2)  // Yellowish
                };
                Result.Diffuse = lerp(Result.Diffuse, DebugColors[CascadeIndex], 0.5f);
            }
        }
    }

    Result.Diffuse += AmbientContribution;
    AccumulateVisiblePointLights(WorldPos, N, V, ScreenPos, Result);

    return Result;
}
FLightingResult EvaluateLightingFromWorldVertex(float3 WorldPos, float3 WorldNormal)
{
    FLightingResult Result;
    Result.Diffuse = DEFAULT_AMBIENT_COLOR;
    Result.Specular = 0.0f.xxx;

    const float3 N = normalize(WorldNormal);
    const float3 V = normalize(CameraPosition - WorldPos);

    float3 AmbientAccum = 0.0f.xxx;
    uint HasAmbient = 0u;

    // =========================
    // 1. Scene Global Lights (Directional + Ambient)
    // =========================
    [loop]
    for (uint i = 0u; i < SceneGlobalLightCount; ++i)
    {
        const FGPULight Light = GlobalLights[i];
        const float3 LightColor = Light.Color * Light.Intensity;

        if (Light.Type == LIGHT_TYPE_AMBIENT)
        {
            if (HasAmbient == 0u)
            {
                AmbientAccum = 0.0f.xxx;
                HasAmbient = 1u;
            }

            AmbientAccum += LightColor;
            continue;
        }

        if (Light.Type == LIGHT_TYPE_DIRECTIONAL)
        {
            const float3 L = normalize(Light.Direction);
            
            float ShadowFactor = 1.0f;
            if (Light.bCastShadows != 0u)
            {
                ShadowFactor = ComputeDirectionalShadowFactor(WorldPos, N, normalize(Light.Direction));
            }
            AccumulateDirectLight(WorldPos, N, V, L, LightColor * ShadowFactor, Result);

            if (bCascadeDebug != 0u && ShadowMode == SHADOW_MODE_CSM)
            {
                int CascadeIndex = GetCascadeIndex(WorldPos);
                float3 DebugColors[4] = {
                    float3(1.0, 0.2, 0.2), // Reddish
                    float3(0.2, 1.0, 0.2), // Greenish
                    float3(0.2, 0.2, 1.0), // Blueish
                    float3(1.0, 1.0, 0.2)  // Yellowish
                };
                Result.Diffuse = lerp(Result.Diffuse, DebugColors[CascadeIndex], 0.5f);
            }
        }
    }

    Result.Diffuse += AmbientAccum;

    // =========================
    // 2. Local Lights (Point / Spot) - brute force
    // 해당 함수는 구로 셰이딩 모드에서만 들어오고, 픽셀 단위 컬링을 쓰지 않기 때문에 전체 순회
    // =========================
    [loop]
    for (uint j = 0u; j < VisibleLightCount; ++j)
    {
        const FVisibleLightData Light = VisibleLights[j];

        const float3 ToLight = Light.WorldPos - WorldPos;
        const float Dist = length(ToLight);

        if (Dist <= 1.0e-4f || Dist >= Light.Radius)
            continue;

        const float3 L = ToLight / Dist;

        float Att = ComputeDistanceAttenuation(Dist, Light.Radius, Light.RadiusFalloff);
        
        if (Att <= 0.0f)
            continue;

        if (Light.Type == LIGHT_TYPE_SPOT)
        {
            const float3 SpotDir = normalize(Light.Direction);
            const float CosAngle = dot(SpotDir, -L);
            const float ConeRange = max(Light.SpotInnerCos - Light.SpotOuterCos, 1.0e-4f);

            Att *= saturate((CosAngle - Light.SpotOuterCos) / ConeRange);
            if (Att <= 0.0f)
                continue;

            Att *= ComputeSpotShadowFactor(WorldPos, N, L, Light.bCastShadows, Light.ShadowMapIndex, Light.ShadowBias);
            if (Att <= 0.0f)
                continue;
        }
        else if (Light.Type == LIGHT_TYPE_POINT)
        {
            Att *= ComputePointShadowFactor(WorldPos, N, Light.bCastShadows, Light.ShadowMapIndex, Light.ShadowBias);
            if (Att <= 0.0f) continue;
        }

        AccumulateDirectLight(WorldPos, N, V, L, Light.Color * Light.Intensity * Att, Result);
    }

    return Result;
}

float3 ApplyLighting(FUberSurfaceData Surface, FLightingResult Lighting)
{
    return Surface.Albedo * Lighting.Diffuse + Lighting.Specular;
}

#if defined(MATERIAL_DOMAIN_DECAL)

cbuffer UberDecal : register(b5)
{
    row_major float4x4 InvDecalWorld;
    float DecalMaskInfluence;
    float DecalWhiteOverride;
    float2 DecalPadding;
}

struct FUberDecalPSOutput
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

FUberSurfaceData EvaluateProjectedDecal(FUberPSInput Input)
{
    FUberSurfaceData Surface;
    Surface.WorldPos = Input.WorldPos;
    Surface.WorldNormal = normalize(Input.WorldNormal);
    Surface.bIsEmissive = any(EmissiveColor > 0.0f) ? 1u : 0u;

    const float4 LocalPos = mul(float4(Input.WorldPos, 1.0f), InvDecalWorld);
    clip(0.5f - abs(LocalPos.xyz));

    Surface.UV = float2(LocalPos.y + 0.5f, 1.0f - (LocalPos.z + 0.5f));
    Surface.DiffuseSample = DiffuseMap.Sample(SampleState, Surface.UV);
    Surface.DiffuseSample.rgb = lerp(Surface.DiffuseSample.rgb, float3(1.0f, 1.0f, 1.0f), DecalWhiteOverride);
   
    //GameJam
    float MaskValue = lerp(1.0f, DecalMaskMap.Sample(SampleState, Surface.UV).r, DecalMaskInfluence);
    Surface.DiffuseSample.a *= MaskValue;

    Surface.WorldNormal = ResolveSurfaceWorldNormal(Input, Surface.UV, Surface.WorldNormal);
    Surface.Albedo = BaseColor * Surface.DiffuseSample.rgb;

    return Surface;
}

FUberPSInput mainVS(FUberVSInput Input)
{
    FUberPSInput Output = BuildSurfaceVertex(Input);
    Output.ClipPos.z -= 0.0001f;
    return Output;
}

FUberDecalPSOutput mainPS(FUberPSInput Input)
{
    FUberSurfaceData Surface = EvaluateProjectedDecal(Input);
    Surface.Albedo *= PrimitiveColor.rgb;

    const float Alpha = saturate(Opacity * PrimitiveColor.a * Surface.DiffuseSample.a);
    clip(Alpha - 0.001f);

    float3 FinalColor = Surface.Albedo;
    if (bLightingEnabled > 0.5f)
    {
        const FLightingResult Lighting = EvaluateLightingFromWorld(Surface.WorldPos, Surface.WorldNormal, Input.ClipPos.xy);
        FinalColor = ApplyLighting(Surface, Lighting);
    }

    if (Surface.bIsEmissive != 0u)
    {
        FinalColor += EmissiveColor * Surface.DiffuseSample.rgb * PrimitiveColor.rgb;
    }

    if (bIsWireframe > 0.5f)
    {
        FinalColor = WireframeRGB;
    }

    FUberDecalPSOutput Output;
    Output.Color = float4(FinalColor, Alpha);
    Output.Normal = float4(Surface.WorldNormal * 0.5f + 0.5f, Alpha);
    return Output;
}

#else

FUberPSInput mainVS(FUberVSInput Input)
{
    FUberPSInput Output = BuildSurfaceVertex(Input);

#if defined(LIGHTING_MODEL_GOURAUD)
    const FLightingResult Lighting = EvaluateLightingFromWorldVertex(Output.WorldPos, Output.WorldNormal);
    Output.VertexDiffuseLighting = Lighting.Diffuse;
    Output.VertexSpecularLighting = Lighting.Specular;
#endif

    return Output;
}

FUberPSOutput mainPS(FUberPSInput Input)
{
    const FUberSurfaceData Surface = EvaluateSurface(Input);
    FLightingResult Lighting;

#if defined(LIGHTING_MODEL_GOURAUD)
    Lighting.Diffuse = Input.VertexDiffuseLighting;
    Lighting.Specular = Input.VertexSpecularLighting;
    
#elif defined(LIGHTING_MODEL_LAMBERT)
    Lighting = EvaluateLightingFromWorld(Surface.WorldPos, Surface.WorldNormal, Input.ClipPos.xy);
    Lighting.Specular = 0.0f.xxx;
#else
    // TOON | PHONG (함수 내에서 내부적으로 계산 흐름 분리)
    Lighting = EvaluateLightingFromWorld(Surface.WorldPos, Surface.WorldNormal, Input.ClipPos.xy);

#endif
    return ComposeOutput(Surface, ApplyLighting(Surface, Lighting));
}

#endif
