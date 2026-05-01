#ifndef FORWARD_LIGHT_DATA_HLSLI
#define FORWARD_LIGHT_DATA_HLSLI

// Shared forward-lighting GPU data. Must stay byte-compatible with
// C++ Source/Engine/Render/Pipeline/ForwardLightData.h.
// =============================================================================
// Forward Shading 라이팅 구조체 & 리소스 바인딩
// C++ ForwardLightData.h 와 바이트 단위로 1:1 대응
//
// 슬롯 배치:
//   b4        LightingBuffer (Ambient + Directional + 메타)
//   t8        StructuredBuffer<FLightInfo>  (Point/Spot 통합)
//   t9        StructuredBuffer<uint>        (TileLightIndices)
//   t10       StructuredBuffer<uint2>       (TileLightGrid)
// =============================================================================

#define LIGHT_TYPE_POINT 0
#define LIGHT_TYPE_SPOT 1
#define LIGHT_TYPE_DIRECTIONAL 2

#define SHADOW_INFO_TYPE_ATLAS2D 0
#define SHADOW_INFO_TYPE_CUBEMAP 1

#define TILE_SIZE 16
#define MAX_LIGHTS_PER_TILE 256

#define LIGHT_CULLING_OFF 0
#define LIGHT_CULLING_TILE 1
#define LIGHT_CULLING_CLUSTER 2

// =============================================================================
// 구조체 — C++ POD와 레이아웃 동일
// =============================================================================
struct FAABB
{
    float4 minPt;
    float4 maxPt;
};

struct FAmbientLightInfo
{
    float4 Color;
    float Intensity;
    float3 _padA;
};

struct FDirectionalLightInfo
{
    float4 Color;
    float3 Direction;
    float Intensity;
    int ShadowIndex;
    float3 _padD;
};

struct FShadowInfo
{
    uint Type;
    uint ArrayIndex;
    uint LightIndex;
    uint bIsPSM;

    uint CubeTierIndex;
    uint3 _padShadowInfo;

    row_major float4x4 LightVP;
    float4 SampleData;
    float4 ShadowParams; //x = Cosntant Bias, y = Slope Bias, z = Sharpen, w = nearZ
};

struct FLightInfo
{
    float4 Color;

    float3 Position;
    float Intensity;

    float AttenuationRadius;
    float FalloffExponent;
    uint LightType;
    float _pad0;

    float3 Direction;
    float InnerConeCos;

    float OuterConeCos;
    int ShadowIndex;
    float2 _pad1;
};

struct FClusterCullingState
{
    float NearZ;
    float FarZ;
    uint ClusterX;
    uint ClusterY;

    uint ClusterZ;
    uint ScreenWidth;
    uint ScreenHeight;
    uint MaxLightsPerCluster;
};

// =============================================================================
// 리소스 바인딩
// =============================================================================

// ── Lighting CB (b4) — Ambient + Directional + 메타데이터 ──
cbuffer LightingBuffer : register(b4)
{
    FAmbientLightInfo AmbientLight;
    FDirectionalLightInfo DirectionalLight;

    uint NumActivePointLights;
    uint NumActiveSpotLights;
    uint NumTilesX;
    uint NumTilesY;
    FClusterCullingState CullState;
    uint LightCullingMode;
    uint VisualizeLightCulling;
    float HeatMapMax;
    uint ShadowMethod;

    row_major float4x4 CascadeMatrices[4];
    float4 CascadeSplits;
    uint NumCascades;
    uint3 _pad2;
};

StructuredBuffer<FLightInfo> AllLights : register(t8);
StructuredBuffer<uint> TileLightIndices : register(t9);
StructuredBuffer<uint2> TileLightGrid : register(t10);
StructuredBuffer<uint> g_ClusterLightIndices : register(t11);
StructuredBuffer<uint2> g_ClusterLightGrid : register(t12);
StructuredBuffer<FShadowInfo> gShadowInfos : register(t21);
Texture2DArray gShadowAtlasArray : register(t22);
TextureCubeArray gShadowCubeArrayTier0 : register(t23);
TextureCubeArray gShadowCubeArrayTier1 : register(t24);
TextureCubeArray gShadowCubeArrayTier2 : register(t25);
TextureCubeArray gShadowCubeArrayTier3 : register(t26);

#endif // FORWARD_LIGHT_DATA_HLSLI
