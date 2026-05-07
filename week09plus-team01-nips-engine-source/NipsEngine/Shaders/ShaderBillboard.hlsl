#include "Common.hlsl"

Texture2D DiffuseMap : register(t0);
SamplerState BillboardSampler : register(s0);

struct VSInput
{
    float3 Position  : POSITION;
    float2 TexCoord  : TEXCOORD;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

PSInput mainVS(VSInput Input)
{
    PSInput Output;
    Output.Position = mul(mul(float4(Input.Position, 1.0f), Model), mul(View, Projection));
    Output.TexCoord = Input.TexCoord;
    return Output;
}

float4 mainPS(PSInput Input) : SV_TARGET
{
    float4 Color = DiffuseMap.Sample(BillboardSampler, Input.TexCoord);
    if (Color.a < 0.01f)
    {
        discard;
    }
    return Color * PrimitiveColor;
}
