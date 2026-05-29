#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Particle/ParticleCommon.hlsli"

Texture2D DiffuseTexture : register(t0);

cbuffer ParticleMaterialBuffer : register(b3)
{
    float4 ParticleMaterialColor;
    uint HasDiffuseTexture;
    float3 _ParticleMaterialPad;
}

PS_Input_Particle VS(VS_Input_RibbonParticle Input)
{
    PS_Input_Particle Out;
    Out.position = mul(mul(float4(Input.position, 1.0f), View), Projection);
    Out.texcoord = Input.uv;
    Out.color = Input.color;
    return Out;
}

float4 PS(PS_Input_Particle Input) : SV_Target
{
    float4 Col = HasDiffuseTexture != 0
        ? DiffuseTexture.Sample(LinearWrapSampler, Input.texcoord)
        : float4(1.0f, 1.0f, 1.0f, 1.0f);
    Col *= ParticleMaterialColor;
    clip(Col.a * Input.color.a - 0.01f);

    return float4(ApplyWireframe(Col.rgb) * Input.color.rgb,
                  bIsWireframe ? 1.0f : (Col.a * Input.color.a));
}
