#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"

cbuffer DOFCB : register(b2)
{
    float FocalLength;
    float Aperture;
    float FocusDistance;
    float NearClip;
    float FarClip;
    float ViewportWidth;
    float ViewportHeight;
    float _DOFPad0;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float LinearizeDepth(float d)
{
    return NearClip * FarClip / (NearClip - d * (NearClip - FarClip));
}

float ComputeSignedCoC(float sceneDepth)
{
    const float SensorHeight = 0.024f;
    const float MaxKernelRadius = 6.0f;

    float focalLengthMeters = FocalLength * 0.001f;
    float apertureDiameter = focalLengthMeters / Aperture;
    float imageDistance = focalLengthMeters * sceneDepth / (sceneDepth - focalLengthMeters);
    float focusImageDistance = focalLengthMeters * FocusDistance / (FocusDistance - focalLengthMeters);
    float cocOnSensor = apertureDiameter * ((imageDistance - focusImageDistance) / imageDistance);
    float cocPixels = cocOnSensor * (ViewportHeight / SensorHeight);
    return cocPixels / MaxKernelRadius;
}

float PS(PS_Input_UV input) : SV_TARGET
{
    int2 coord = int2(input.position.xy);
    float depth = SceneDepthTexture.Load(int3(coord, 0));
    float linearDepth = LinearizeDepth(depth);
    return ComputeSignedCoC(linearDepth);
}
