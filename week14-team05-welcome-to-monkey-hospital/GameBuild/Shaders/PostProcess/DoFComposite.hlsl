// DoFComposite.hlsl
// Composite sharp scene, background blur, and foreground blur from signed CoC.

#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/DoFCommon.hlsli"

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float2 uv = input.uv;
    float4 sharp = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0);
    float coc = CoCTexture.SampleLevel(LinearClampSampler, uv, 0);

    float backgroundAmount = saturate(coc);
    float foregroundAmount = saturate(-coc);
    float focusProtect = 1.0f - saturate(abs(coc) * DoFFocusProtectScale);

    float4 background = DoFBackgroundTexture.SampleLevel(LinearClampSampler, uv, 0);
    float4 foreground = DoFForegroundTexture.SampleLevel(LinearClampSampler, uv, 0);

    float4 color = lerp(sharp, background, backgroundAmount);
    color = lerp(color, sharp, focusProtect);

    float foregroundAlpha = saturate(max(foreground.a, foregroundAmount));
    color.rgb = lerp(color.rgb, foreground.rgb, foregroundAlpha);

    float4 bokeh = DoFBokehTexture.SampleLevel(LinearClampSampler, uv, 0);
    float bokehAlpha = saturate(bokeh.a * BokehIntensity);
    color.rgb = lerp(color.rgb, max(color.rgb, bokeh.rgb), bokehAlpha);

    color.a = sharp.a;
    return color;
}
