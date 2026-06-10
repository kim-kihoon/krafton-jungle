// DoFCoCDebug.hlsl
// Signed CoC debug: background/far is blue, foreground/near is orange.

#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float coc = CoCTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float background = saturate(coc);
    float foreground = saturate(-coc);

    float3 bgColor = float3(0.10f, 0.35f, 1.00f) * background;
    float3 fgColor = float3(1.00f, 0.45f, 0.05f) * foreground;
    return float4(bgColor + fgColor, 1.0f);
}
