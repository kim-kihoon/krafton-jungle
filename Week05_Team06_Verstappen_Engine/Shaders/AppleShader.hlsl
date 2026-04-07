cbuffer PerObjectBuffer : register(b0)
{
    float4x3 WorldMatrix; // 3x4 Packed Matrix (HLSL에서는 4x3으로 선언하여 4x4처럼 사용)
};

cbuffer PerFrameBuffer : register(b1)
{
    float4x4 ViewProjection;
};

struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
};

PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    
    // 3x4 행렬을 4x4로 확장하여 변환
    float4x4 world = float4x4(
        float4(WorldMatrix[0], 0),
        float4(WorldMatrix[1], 0),
        float4(WorldMatrix[2], 0),
        float4(0, 0, 0, 1)
    );
    
    float4 worldPos = mul(float4(input.Position, 1.0f), world);
    output.Position = mul(worldPos, ViewProjection);
    output.Normal = input.Normal; // 단순화를 위해 노멀 변환 생략
    
    return output;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    // 레드불(베르스타펜) 감성의 빨간 사과 색상 + 아주 단순한 라이팅
    float3 lightDir = normalize(float3(1, 1, -1));
    float diff = max(dot(input.Normal, lightDir), 0.2f);
    return float4(float3(1.0f, 0.1f, 0.1f) * diff, 1.0f);
}