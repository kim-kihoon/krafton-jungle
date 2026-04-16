// SceneDepth.hlsl
// Fullscreen post-process: SceneDepth visualization

#include "Common/ConstantBuffers.hlsl"

Texture2D<float> DepthTex : register(t0);

struct PS_Input
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

//VS: Fullscreen Triangle (SV_VertexID)
PS_Input VS(uint vertexID : SV_VertexID)
{
    PS_Input output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

//선형 깊이
float LinearizeDepth(float hwDepth)
{
    return Projection[3][2] / (hwDepth - Projection[2][2]);
}

float4 PS(PS_Input input) : SV_TARGET
{
    int2 coord = int2(input.position.xy);
    float depth = DepthTex.Load(int3(coord, 0)).r;

    // Sky is far, so white
    if (depth >= 1.0)
        return float4(1, 1, 1, 1);

    float linearZ = LinearizeDepth(depth);
    
    // Proj 행렬에서 near/far 추출
    float p22 = Projection[2][2];
    float p32 = Projection[3][2];
    
    float nearZ = 0.1;
    float farZ = 1000.0;
    if (abs(p22) > 0.00001)
    {
        nearZ = p32 / (-p22);
        farZ = p32 / (1.0 - p22);
    }
    if (farZ <= nearZ) farZ = nearZ + 1000.0;

    float normalized = saturate((linearZ - nearZ) / (farZ - nearZ));
    // 가까운 거리의 구분감을 높이기 위해 pow(0.5) (gamma-like) 적용
    float gray = pow(normalized, 0.5); 
    
    return float4(gray, gray, gray, 1);
}
