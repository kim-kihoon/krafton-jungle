cbuffer cbSubUV : register(b3)
{
    float2 UVOffset;
    float2 UVScale;
};

Texture2D SpriteTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer FrameConstant : register(b0)
{
    row_major matrix View;
    row_major matrix Projection;
};

cbuffer ObjectConstant : register(b1)
{
    row_major matrix Model;
    float4 Color;
    int bIsSelected;
    float3 Padding;
};

struct VS_INPUT
{
    float4 Pos : POSITION;
    float2 TexCoord : TEXCOORD;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    matrix worldViewProj = mul(mul(Model, View), Projection);
    float4 pos = mul(input.Pos, worldViewProj);
    output.Pos = pos;
    output.TexCoord = input.TexCoord;
    return output;
};

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float2 uv = UVOffset + input.TexCoord * UVScale;
    float4 finalColor = SpriteTexture.Sample(LinearSampler, uv) * Color;
    
    if (all(finalColor.rgb) <= 0.01)
        discard;
    
    return finalColor;
};