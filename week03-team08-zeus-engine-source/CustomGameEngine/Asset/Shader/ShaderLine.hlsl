cbuffer FrameConstant : register(b0)
{
    row_major matrix View;
    row_major matrix Projection;
};

cbuffer FGridConstant : register(b2)
{
    float3 CameraPos;
    float GridRadius;
    float GridSize;
    float3 Padding;
};

struct VS_INPUT
{
    float3 Pos : POSITION;
    float4 Color : COLOR;
};

struct VS_OUTPUT
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR;
    float3 WorldPos : TEXCOORD0;
};

// Vertex Shader
VS_OUTPUT mainVS(VS_INPUT input)
{
    VS_OUTPUT output;
    output.WorldPos = input.Pos;
    output.Pos = mul(float4(input.Pos, 1.0f), mul(View, Projection));
    output.Color = input.Color;
    return output;
}

// Pixel Shader
float4 mainPS(VS_OUTPUT input) : SV_Target
{
    // 카메라와의 거리에 비례하여 서서히 투명해지도록 처리
    float dist = distance(input.WorldPos, CameraPos);
    
    // 외곽으로 갈수록 투명도가 0이 됨
    float alphaFade = 1.0f - saturate(dist / GridRadius);

    
    if (alphaFade < 0.01f)
        discard;

    return float4(input.Color.rgb, alphaFade);
}