#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"
#include "Common/ConstantBuffers.hlsl"

Texture2D g_txColor : register(t0);
SamplerState g_Sample : register(s0);

PS_Input_Full VS(VS_Input_PNCT input)
{
    PS_Input_Full output;
    output.position = ApplyMVP(input.position);
    output.normal = normalize(mul(input.normal, (float3x3) Model));
    output.color = input.color * SectionColor;

    float2 texcoord = input.texcoord;
    output.texcoord = texcoord;

    return output;
}

float4 PS(PS_Input_Full input) : SV_TARGET
{
    float4 texColor = g_txColor.Sample(g_Sample, input.texcoord);

    if (texColor.a < 0.001f)
        discard;

    float4 finalColor = texColor * input.color;
    if (bFade)
    {
        if (DecalOpacity <= 0.001f)
            discard;
        finalColor.a = texColor.a * input.color.a * DecalOpacity;
    }

    if (finalColor.a < 0.001f)
        discard;

    return float4(ApplyWireframe(finalColor.rgb), finalColor.a);
}
