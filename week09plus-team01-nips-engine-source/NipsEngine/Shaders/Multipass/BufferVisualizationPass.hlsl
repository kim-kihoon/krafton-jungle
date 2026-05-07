#include "../Common.hlsl"

cbuffer BufferVisualization : register(b10)
{
    uint VisualizationMode;
    float3 BufferVisualizationPadding;
}

Texture2D SceneDepth : register(t0);
Texture2D SceneNormal : register(t1);

struct VSOutput
{
    float4 ClipPos : SV_POSITION;
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
    VSOutput Output;
    float2 Pos;
    if (VertexID == 0)
        Pos = float2(-1.0f, -1.0f);
    else if (VertexID == 1)
        Pos = float2(-1.0f, 3.0f);
    else
        Pos = float2(3.0f, -1.0f);

    Output.ClipPos = float4(Pos, 0.0f, 1.0f);
    return Output;
}

float4 mainPS(VSOutput Input) : SV_TARGET
{
    const int2 PixelPosition = int2(Input.ClipPos.xy);

    if (VisualizationMode == 1u)
    {
        const float Depth = saturate(1.0f - SceneDepth.Load(int3(PixelPosition, 0)).r);
        return float4(Depth.xxx, 1.0f);
    }

    const float3 WorldNormal = saturate(SceneNormal.Load(int3(PixelPosition, 0)).rgb);
    return float4(WorldNormal, 1.0f);
}
