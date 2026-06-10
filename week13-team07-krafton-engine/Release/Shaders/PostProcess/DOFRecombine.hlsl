#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

cbuffer DOFCB : register(b2)
{
    float FocalLength;
    float Aperture;
    float FocusDistance;
    float NearClip;
    float FarClip;
    float ViewportWidth;
    float ViewportHeight;
    float _DOFPad0;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float4 sharpColor = SceneColorTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float4 blurColor = DOFBlurTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float coc = saturate(abs(DOFCoCTexture.SampleLevel(PointClampSampler, input.uv, 0)));
    return lerp(sharpColor, blurColor, coc);
}
