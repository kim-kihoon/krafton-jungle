#ifndef LIGHT_CULLING_DEBUG_HLSLI
#define LIGHT_CULLING_DEBUG_HLSLI

#include "Common/ForwardLightData.hlsli"

float3 GetLightCullingHeatmapColor(float value)
{
    float3 color;
    color.r = saturate(min(4.0f * value - 1.5f, -4.0f * value + 4.5f));
    color.g = saturate(min(4.0f * value - 0.5f, -4.0f * value + 3.5f));
    color.b = saturate(min(4.0f * value + 0.5f, -4.0f * value + 2.5f));
    return color;
}

uint DepthToClusterSliceForDebug(float viewDepth)
{
    float safeDepth = clamp(viewDepth, CullState.NearZ, CullState.FarZ);
    float slice;

    if (CullState.bIsOrtho > 0)
    {
        slice = (safeDepth - CullState.NearZ) / (CullState.FarZ - CullState.NearZ);
    }
    else
    {
        slice = log(safeDepth / CullState.NearZ) / log(CullState.FarZ / CullState.NearZ);
    }

    return min((uint)floor(slice * CullState.ClusterZ), CullState.ClusterZ - 1);
}

uint ComputeClusterIndexForDebug(float4 screenPos, float3 worldPos)
{
    float4 viewPos = mul(float4(worldPos, 1.0f), View);
    uint tileX = min((uint)(screenPos.x / CullState.ScreenWidth * CullState.ClusterX), CullState.ClusterX - 1);
    uint tileY = min((uint)(screenPos.y / CullState.ScreenHeight * CullState.ClusterY), CullState.ClusterY - 1);
    uint sliceZ = DepthToClusterSliceForDebug(abs(viewPos.z));

    return sliceZ * CullState.ClusterX * CullState.ClusterY
        + tileY * CullState.ClusterX
        + tileX;
}

float4 ComputeCullingHeatmap(float4 screenPos, float3 worldPos)
{
    uint lightCount = NumActivePointLights + NumActiveSpotLights;

    if (LightCullingMode == LIGHT_CULLING_TILE && NumTilesX > 0 && NumTilesY > 0)
    {
        uint2 tileCoord = min(uint2(screenPos.xy) / TILE_SIZE, uint2(NumTilesX - 1, NumTilesY - 1));
        uint tileIdx = tileCoord.y * NumTilesX + tileCoord.x;
        lightCount = TileLightGrid[tileIdx].y;
    }
    else if (LightCullingMode == LIGHT_CULLING_CLUSTER)
    {
        uint clusterIdx = ComputeClusterIndexForDebug(screenPos, worldPos);
        lightCount = g_ClusterLightGrid[clusterIdx].y;
    }

    float ratio = saturate((float)lightCount / HeatMapMax);
    return float4(GetLightCullingHeatmapColor(ratio), 1.0f);
}

#endif // LIGHT_CULLING_DEBUG_HLSLI
