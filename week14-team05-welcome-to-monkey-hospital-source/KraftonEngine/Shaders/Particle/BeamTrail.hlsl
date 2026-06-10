#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

// Particle Beam / Ribbon — CPU에서 월드 좌표 strip으로 펼쳐진 정점을 받는다.
//   Beam:   SourcePoint→TargetPoint 사이를 InterpolationPoints로 분할한 카메라facing 띠.
//   Ribbon: 활성 입자를 age순으로 이은 카메라facing trail 띠.
// 정점 Color × BaseColor × Opacity × DiffuseTexture로 합성. (Sprite와 동일한 unlit 합성)

// t0: Material.CachedSRVs[Diffuse]
Texture2D DiffuseTexture : register(t0);

// b2 (PerShader0): .mat의 Parameters 키에서 자동 매핑
cbuffer ParticleBeamTrailParams : register(b2)
{
    float4 BaseColor;    // 추가 tint
    float  Opacity;      // [0,1]
    float  UseTexture;   // 0=텍스처 무시(base color만), 1=텍스처 사용
    float  ScrollSpeed;  // beam 길이방향(u) UV 스크롤 속도 (에너지 흐름, 0 = 정지)
    float  _pad;
}

struct VS_Input_BeamTrail
{
    float3 position : POSITION;   // 이미 월드 좌표 (CPU strip expansion 완료)
    float4 color    : COLOR;
    float2 texcoord : TEXTCOORD;
};

struct PS_Input_BeamTrail
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
};

PS_Input_BeamTrail VS(VS_Input_BeamTrail input)
{
    PS_Input_BeamTrail output;
    output.position = ApplyVP(input.position);
    output.color    = input.color;
    output.texcoord = input.texcoord;
    return output;
}

float4 PS(PS_Input_BeamTrail input) : SV_TARGET
{
    // beam 길이방향(u)으로 UV를 흘려 에너지가 흐르는 느낌. Time은 b0 FrameBuffer.
    // frac으로 0~1 wrap → Clamp 샘플러여도 끊김 없이 반복.
    float2 uv = float2(frac(input.texcoord.x - Time * ScrollSpeed), input.texcoord.y);
    float4 sampled = DiffuseTexture.Sample(LinearClampSampler, uv);
    float3 texRgb = lerp(float3(1,1,1), sampled.rgb, UseTexture);
    float  texA   = lerp(1.0,            sampled.a,   UseTexture);

    float3 rgb = input.color.rgb * BaseColor.rgb * texRgb;
    float  a   = input.color.a   * BaseColor.a   * Opacity * texA;
    return float4(ApplyWireframe(rgb), a);
}
