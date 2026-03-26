cbuffer FrameConstant : register(b0)
{
    row_major matrix View;
    row_major matrix Projection;
    float3 CameraRight;
    float _pad0;
    float3 CameraUp;
    float _pad1;
};

cbuffer ObjectConstant : register(b1)
{
    row_major matrix Model;
    float4 Color;
    int bIsSelected;
    float3 Padding;
};

Texture2D FontAtlas : register(t0);
SamplerState FontSampler : register(s0);

struct VSInput
{
    float3 AnchorPos : POSITION;
    float2 Offset : TEXCOORD1;
    float2 TexCoord : TEXCOORD;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

PSInput mainVS(VSInput input)
{
    PSInput output;
    float3 worldPos = input.AnchorPos
                      + CameraRight * input.Offset.x
                      + CameraUp * input.Offset.y;
    output.Position = mul(float4(worldPos, 1.0f), mul(View, Projection));
    output.TexCoord = input.TexCoord;
    return output;
};

float4 mainPS(PSInput input) : SV_TARGET
{
    float alpha = FontAtlas.Sample(FontSampler, input.TexCoord).r;
    clip(alpha - 0.05f);
    return float4(Color.rgb, alpha * Color.a);
};
