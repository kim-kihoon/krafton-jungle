#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"

cbuffer SceneDepthCB : register(b2)
{
    float Exponent;
    float NearClip;
    float FarClip;
    uint Mode;
}

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    int2 coord = int2(input.position.xy);
    float d = SceneDepthTexture.Load(int3(coord, 0));

    float v = 0.0f;
    if (Mode == 1)
    {
        float linZ = NearClip * FarClip / (NearClip - d * (NearClip - FarClip));
        v = saturate((linZ - NearClip) / (FarClip - NearClip));
    }
    else
    {
        v = pow(saturate(1.0f - d), Exponent);
    }

    return float4(v, v, v, 1.0f);
}
