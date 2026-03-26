// b0 슬롯: URenderer::CreateConstantBuffer() 에서 지정한 FrameConstant
cbuffer FFrameConstant : register(b0)
{
    row_major matrix View;
    row_major matrix Projection;
};

// b2 슬롯: FGridConstant
cbuffer FGridConstant : register(b2)
{
    float3 CameraPos;
    float GridRadius;
    float GridSize;
    float3 Padding;
};

struct VS_OUTPUT
{
    float4 Pos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
};

VS_OUTPUT mainVS(uint VertexID : SV_VertexID)
{
    VS_OUTPUT output;

    // 정점 6개로 카메라 중심에 거대한 바닥 평면(Z=0) 생성
    float2 gridPlane[6] =
    {
        float2(-1, -1), float2(1, -1), float2(-1, 1),
        float2(-1, 1), float2(1, -1), float2(1, 1)
    };

    // 평면의 크기 (GridRadius보다 충분히 커야 함)
    float2 pos = gridPlane[VertexID] * (GridRadius * 2.0f);

    output.WorldPos = float3(CameraPos.x + pos.x, CameraPos.y + pos.y, 0.0f);
    
    matrix ViewProjection = mul(View, Projection);
    output.Pos = mul(float4(output.WorldPos, 1.0f), ViewProjection);

    return output;
}

float4 mainPS(VS_OUTPUT input) : SV_TARGET
{
    // 월드 좌표를 그리드 사이즈로 나눔
    float2 minorCoord = input.WorldPos.xy / GridSize;
    float2 minorDerivative = fwidth(minorCoord);
    float2 minorGrid = abs(frac(minorCoord - 0.5f) - 0.5f) / minorDerivative;
    float minorLineDist = min(minorGrid.x, minorGrid.y);
    float minorLineAlpha = 1.0f - min(minorLineDist, 1.0f);
    
    float2 majorCoord = input.WorldPos.xy / (GridSize * 5.0f);
    float2 majorDerivative = fwidth(majorCoord);
    float2 majorGrid = abs(frac(majorCoord - 0.5f) - 0.5f) / majorDerivative;
    float majorLineDist = min(majorGrid.x, majorGrid.y);
    float majorLineAlpha = 1.0f - min(majorLineDist / 1.5f, 1.0f);
    
    float lineAlpha = max(minorLineAlpha * 0.3f, majorLineAlpha);

    // 카메라 기준 거리에 따른 부드러운 원형 페이드 아웃
    float distanceToCam = length(input.WorldPos.xy - CameraPos.xy);
    float fade = 1.0f - saturate(distanceToCam / GridRadius); // GridRadius 바깥이면 투명해짐

    // 최종 알파 계산 (기본 그리드 색상은 짙은 회색)
    float4 GridColor = float4(0.3f, 0.3f, 0.3f, 1.0f);
    float finalAlpha = lineAlpha * fade;

    if (finalAlpha <= 0.01f)
        discard;

    return float4(GridColor.rgb, finalAlpha);
}