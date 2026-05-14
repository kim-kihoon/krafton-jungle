#include "Common.hlsl"

Texture2D DiffuseMap : register(t0);
SamplerState BillboardSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

PSInput VS(VSInput Input)
{
    PSInput Output;
    Output.Position = ApplyMVP(Input.Position);
    Output.TexCoord = Input.TexCoord;
    return Output;
}

float4 PS(PSInput Input) : SV_TARGET
{
    float4 DiffuseColor = DiffuseMap.Sample(BillboardSampler, Input.TexCoord);
    if (DiffuseColor.a < 0.01f)
        discard;
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
