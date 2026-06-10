#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D PhotoColorTex : register(t0);

cbuffer PhotoDevelopBuffer : register(b2)
{
    float DevelopAlpha;
    float3 Padding;
}

struct PS_Input_PhotoDevelop
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
};

PS_Input_PhotoDevelop VS(VS_Input_PNCT input)
{
    PS_Input_PhotoDevelop output;
    output.position = ApplyMVP(input.position);
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}

float4 PS(PS_Input_PhotoDevelop input) : SV_TARGET
{
    float4 photoColor = PhotoColorTex.Sample(LinearClampSampler, input.texcoord) * input.color;
    float alpha = saturate(DevelopAlpha);
    float3 rgb = lerp(float3(0.05f, 0.05f, 0.05f), photoColor.rgb, alpha);
    return float4(ApplyWireframe(rgb), bIsWireframe ? 1.0f : input.color.a);
}
