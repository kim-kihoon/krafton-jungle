#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"

// 컬러 PNG/TGA 텍스처를 단일 quad에 그리는 빌보드 전용 셰이더.
// SubUV 와 다르게 R 채널이 아닌 알파 채널만으로 컷오프 판정한다.
Texture2D BillboardTex : register(t0);

struct PS_Input_Billboard
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
};

PS_Input_Billboard VS(VS_Input_PNCT input)
{
    PS_Input_Billboard output;
    output.position = ApplyMVP(input.position);
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}

float4 PS(PS_Input_Billboard input) : SV_TARGET
{
    float4 col = BillboardTex.Sample(LinearClampSampler, input.texcoord);

    // 알파 컷오프 (straight alpha PNG의 보간 헤일로 차단)
    if (!bIsWireframe && col.a < 0.5f)
        discard;

    col *= input.color;
    return float4(ApplyWireframe(col.rgb), bIsWireframe ? 1.0f : col.a);
}
