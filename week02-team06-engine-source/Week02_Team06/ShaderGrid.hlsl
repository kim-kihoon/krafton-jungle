cbuffer TransformData : register(b0)
{
    row_major float4x4 MVP;
    float4 CamPos;
};

struct VS_INPUT
{
    float4 position : POSITION;
    float4 color : COLOR;
};

struct PS_INPUT
{
    float4 clipPos : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 localPos : TEXCOORD1;
};

PS_INPUT VS_GRID(VS_INPUT input)
{
    PS_INPUT output;
    
    output.worldPos.x = input.position.x + CamPos.x;
    output.worldPos.y = input.position.y;
    output.worldPos.z = input.position.z + CamPos.z;
    
    output.localPos = input.position.xyz;
    output.clipPos = mul(input.position, MVP);
    return output;
}

float4 PS_GRID(PS_INPUT input) : SV_Target
{
    float2 WorldXZ = input.worldPos.xz;
    
    float dist = length(input.localPos.xz);
    float fade = 1.0 - saturate((dist - 80.0) / 120.0);
    
    // small grid every 5 units
    float2 coordSmall = WorldXZ / 5.0;
    float2 fS = frac(coordSmall - 0.5) - 0.5;
    
    // coord의 값을 이용하여 다음 픽셀까지 얼마나 몇 픽셀인지를 계산
    float2 pixelDistSmall = abs(fS) / fwidth(coordSmall);
    //smallGrid가 1이면선임
    float smallGrid = 1.0 - saturate(min(pixelDistSmall.x, pixelDistSmall.y));

    // big grid every 25 units
    float2 coordBig = WorldXZ / 25.0;
    float2 fB = frac(coordBig - 0.5) - 0.5;
    float2 distBig = abs(fB) / fwidth(coordBig);
    float bigGrid = 1.0 - saturate(min(distBig.x, distBig.y) - 0.4); // minus bias to make big grid thinner
    
    //최종적인 그리드의 알파값을 정하기
    float alpha = max(smallGrid * 0.3, bigGrid) * fade;
    
    float4 color = float4(0.38, 0.38, 0.38, alpha);
    
    float xAxisWidth = fwidth(input.worldPos.z) * 1.5f;
    float zAxisWidth = fwidth(input.worldPos.x) * 1.5f;
    
    if (abs(input.worldPos.x) <= zAxisWidth)
    {
        return float4(0.0, 0.0, 1.0, alpha);
    }
    if (abs(input.worldPos.z) <= xAxisWidth)
    {
        return float4(1.0, 0.0, 0.0, alpha);
    }
    
    // Fillrate 최적화 및 모아레 방지
    if (alpha < 0.005)
    {
        discard;
    }
    
    return color;
}