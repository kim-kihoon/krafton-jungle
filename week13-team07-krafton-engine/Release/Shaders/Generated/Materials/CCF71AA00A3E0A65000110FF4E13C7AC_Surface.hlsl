// Generated from Content/Material/M_RedTranslucent.mat
// Domain: Surface

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"
#define USE_FOG 1
#include "Common/Fog.hlsli"
#include "Common/ForwardLighting.hlsli"
#include "Common/GeneratedSurfacePass.hlsli"

FMaterialResult EvaluateMaterial(FMaterialPixelInput Input)
{
    float3 n_22 = float3(1.000000f, 0.000000f, 0.000000f);
    float n_25 = 0.500000f;
    FMaterialResult Result;
    Result.BaseColor = n_22;
    Result.Normal = float3(0, 0, 1);
    Result.Roughness = 0.5f;
    Result.Metallic = 0.0f;
    Result.Emissive = float3(0, 0, 0);
    Result.Opacity = n_25;
    Result.OpacityMask = 1.0f;
    Result.NormalConnected = 0.0f;
    return Result;
}


MaterialSurfaceVSOutput VS_StaticMesh(VS_Input_PNCTT input)
{
    return BuildGeneratedSurfaceStaticMesh(input);
}

MaterialSurfaceVSOutput VS_SkeletalMesh(VS_Input_PNCTTBB input)
{
    return BuildGeneratedSurfaceSkeletalMesh(input);
}

// Legacy entry point. Kept so old cache paths that compile "VS" still render as StaticMesh.
MaterialSurfaceVSOutput VS(VS_Input_PNCTT input)
{
    return VS_StaticMesh(input);
}


float4 PS(MaterialSurfaceVSOutput input) : SV_TARGET
{
    FMaterialPixelInput MaterialInput = BuildGeneratedSurfaceMaterialInput(input);
    FMaterialResult Result = EvaluateMaterial(MaterialInput);

    const float3 N = ApplyGeneratedSurfaceNormal(input, Result);
    float4 FinalColor = float4(ComputeGeneratedSurfaceLighting(input.worldPos, input.position, N, Result), Result.Opacity);
    clip(FinalColor.a - 0.01f);
    return ApplyFogTranslucent(FinalColor, input.worldPos, CameraWorldPos);
}
