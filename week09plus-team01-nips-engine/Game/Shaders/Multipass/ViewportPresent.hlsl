#include "../Common.hlsl"

Texture2D ViewportFinalColor : register(t0);
SamplerState SampleState : register(s0);

struct VSOutput
{
    float4 ClipPos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput mainVS(uint vertexID : SV_VertexID)
{
    VSOutput output;

    float2 pos;
    float2 uv;

    if (vertexID == 0)
    {
        pos = float2(-1.0f, -1.0f);
        uv = float2(0.0f, 1.0f);
    }
    else if (vertexID == 1)
    {
        pos = float2(-1.0f, 3.0f);
        uv = float2(0.0f, -1.0f);
    }
    else
    {
        pos = float2(3.0f, -1.0f);
        uv = float2(2.0f, 1.0f);
    }

    output.ClipPos = float4(pos, 0.0f, 1.0f);
    output.UV = uv;
    return output;
}

float4 mainPS(VSOutput input) : SV_TARGET
{
    return ViewportFinalColor.Sample(SampleState, input.UV);
}
