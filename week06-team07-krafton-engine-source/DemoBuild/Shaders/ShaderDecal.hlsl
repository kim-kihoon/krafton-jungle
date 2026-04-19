#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"

Texture2D SceneDepth : register(t0);        // Z-Buffer (SRV)
Texture2D DecalAlbedo : register(t1);       // Decal 텍스처
SamplerState PointSampler : register(s0);
SamplerState LinearSampler : register(s1);
// SamplerState AnisoSampler : register(s1);   // Renderer에서 슬롯 1에 Aniso 바인딩함 (충돌 방지)

// Decal OBB Local -> Screen MVP 변환
PS_Input_PosW VS(VS_Input_PC input)
{
    PS_Input_PosW output;
    output.position = ApplyMVP(input.position);
    output.positionW = output.position;
    return output;
}

// NDC → World Position 복원
float3 ReconstructWorldPos(float2 screenUV, float depth)
{
    // Screen UV (0~1) → NDC (-1~1)
    float2 ndc;
    ndc.x = screenUV.x * 2.0f - 1.0f;
    ndc.y = -(screenUV.y * 2.0f - 1.0f); // Y 반전 

    // NDC → World
    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, InvViewProj);
    worldPos /= worldPos.w;

    return worldPos.xyz;
}

// Z-Buffer를 이용해 Screen -> Decal Local로 역변환
float4 PS(PS_Input_PosW input, float4 screenPos : SV_Position) : SV_TARGET
{
    // 1. 현재 픽셀의 정확한 Screen UV 계산
    // SV_Position은 픽셀 센터 좌표(0.5, 1.5...)를 주므로 Viewport 크기로 나누어야 함
    float2 screenUV = screenPos.xy * InvViewportSize;
   
    // 2. Z-Buffer에서 Depth 샘플링
    float sceneDepth = SceneDepth.Sample(PointSampler, screenUV).r;
    
    // 3. Depth가 1.0이면 Sky/빈 공간 → 데칼 없음
    clip(sceneDepth < 0.9999f ? 1 : -1);
    
    // 4. Screen UV + Depth → World Position 복원
    // Bias를 최소화하여 지형과의 밀착도 향상 (일렁임 방지)
    float biasedDepth = sceneDepth - 0.000001f; 
    float3 worldPos = ReconstructWorldPos(screenUV, biasedDepth);

    // 5. World → Decal Local 좌표 변환
    float4 decalLocal = mul(float4(worldPos, 1.0f), WorldToDecal);
    
    // 6. Decal OBB 범위 체크 (-0.5 ~ 0.5)
    float3 absLocal = abs(decalLocal.xyz);
    
    // OBB 영역을 벗어나는 픽셀은 즉시 폐기하여 늘어짐/반복 현상 방지
    clip(0.5f - absLocal.x);
    clip(0.5f - absLocal.y);
    clip(0.5f - absLocal.z);
    
    // 7. UV 프로젝션 (YZ 평면 사용)
    float2 decalUV = decalLocal.yz + 0.5f;
    decalUV.y = 1.0f - decalUV.y; 
    
    // 8. Decal 텍스처 샘플링
    float4 decalColor = DecalAlbedo.Sample(LinearSampler, decalUV);
    
    // 9. 페이드 효과 (Spotlight 원형 감쇠 및 경계면 페이드)
    if (bUseFade != 0)
    {
        // 경계면 노이즈 방지
        float edgeFade = 1.0f;
        edgeFade *= smoothstep(0.5f, 0.4f, absLocal.x);
        edgeFade *= smoothstep(0.5f, 0.45f, absLocal.y);
        edgeFade *= smoothstep(0.5f, 0.45f, absLocal.z);
        
        float dist = length(decalLocal.yz) * 2.0f; 
        float spotFade = smoothstep(FadeOuter, FadeInner, dist);
        
        decalColor.a *= edgeFade * spotFade;
    }

    // 10. 최종 출력
    clip(decalColor.a - 0.01f);
    
    return decalColor;
}
