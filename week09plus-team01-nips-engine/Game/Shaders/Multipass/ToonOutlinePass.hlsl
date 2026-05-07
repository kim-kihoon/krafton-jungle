// Back-Face Extrusion 방식

cbuffer FrameBuffer : register(b0)
{
    row_major float4x4 View;
    row_major float4x4 Projection;
}

cbuffer PerObjectBuffer : register(b1)
{
    row_major float4x4 Model;
    row_major float4x4 WorldInvTrans;
    float4 PrimitiveColor;
};

cbuffer ToonOutlineBuffer : register(b2)
{
    float OutlineThickness;
    float3 Padding;
}

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct PSInput
{
    float4 ClipPos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float2 UV : TEXCOORD2;
    float3 WorldTangent : TEXCOORD3;
    float3 WorldBitangent : TEXCOORD4;
    float3 VertexDiffuseLighting : TEXCOORD5;
    float3 VertexSpecularLighting : TEXCOORD6;
};

struct PSOutput
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 WorldPos : SV_TARGET2;
};

PSInput mainVS(VSInput input)
{
    PSInput output;
    
    float4 worldPos = mul(float4(input.Position, 1.0f), Model);
    float3 worldNormal = normalize(mul(input.Normal, (float3x3) Model));

    // 1. 먼저 클립 공간으로 변환
    float4 clipPos = mul(mul(worldPos, View), Projection);

    // 2. 클립 공간 노멀 방향 계산
    float4 clipNormal = mul(mul(float4(worldNormal, 0.0f), View), Projection);

    // 3. 클립 공간에서 w를 곱해서 오프셋 → 거리 무관한 일정 픽셀 두께
    float2 screenNormal = normalize(clipNormal.xy);
    clipPos.xy += screenNormal * OutlineThickness * clipPos.w;

    output.ClipPos = clipPos;
    output.WorldPos = worldPos.xyz;
    output.WorldNormal = worldNormal;
    output.UV = input.UV;

    output.WorldTangent = float3(0, 0, 0);
    output.WorldBitangent = float3(0, 0, 0);
    output.VertexDiffuseLighting = float3(0, 0, 0);
    output.VertexSpecularLighting = float3(0, 0, 0);
    
    return output;
}

PSOutput mainPS(PSInput input)
{
    PSOutput output;
    
    output.Color = float4(0, 0, 0, 1);  // 외곽선 색상
    return output;
}