// ShaderW0.hlsl
Texture2D tex : register(t0); // SRV (PNG 파일)
SamplerState samp : register(s0); // 위에서 만든 SamplerState
cbuffer constants : register(b0)
{
    matrix worldViewProj; // 위치, 회전, 크기가 모두 포함된 4x4 행렬
}

// ShaderW0.hlsl
struct VS_INPUT
{
    float4 position : POSITION; // Input position from vertex buffer
    float4 color : COLOR; // Input color from vertex buffer
    float2 uv : TEXCOORD;
};

struct PS_INPUT
{
    float4 position : SV_POSITION; // Transformed position to pass to the pixel shader
    float4 color : COLOR; // Color to pass to the pixel shader
    float2 uv : TEXCOORD;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    
// 행렬 곱셈을 통해 좌표 변환 (반드시 mul 함수 사용)
    output.position = mul(worldViewProj, input.position);
   
    // Pass the color to the pixel shader
    output.color = input.color;
    
    output.uv = input.uv;
    
    return output;
}

float4 mainPS(PS_INPUT input) : SV_Target
{
    // 2. 샘플링 (텍스처에서 색상을 읽어옴)
    float4 texColor = tex.Sample(samp, input.uv);
    
    // 정점 색상과 곱해서 최종 색상 결정
    // 만약 정점 색상이 흰색(1,1,1,1)이면 텍스처 색상만 나옵니다.
    return texColor * input.color;
}