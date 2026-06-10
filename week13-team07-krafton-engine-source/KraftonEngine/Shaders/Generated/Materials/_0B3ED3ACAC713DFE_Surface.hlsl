// Generated from Content/Material/Auto/MI_LASHES_05.mat
// Domain: Surface

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"
#define USE_FOG 1
#include "Common/Fog.hlsli"
#include "Common/ForwardLighting.hlsli"
#include "Common/GeneratedSurfacePass.hlsli"

Texture2D Tex_Diffuse : register(t0);

FMaterialResult EvaluateMaterial(FMaterialPixelInput Input)
{
    float2 n_3 = Input.UV0;
    float4 n_5 = Tex_Diffuse.Sample(LinearWrapSampler, n_3);
    float4 n_14 = Input.VertexColor;
    float3 n_21 = ((n_5).rgb * (n_14).rgb);
    float3 n_25 = (float4(n_21, 0.0f)).rgb;
    FMaterialResult Result;
    Result.BaseColor = n_25;
    Result.Normal = float3(0, 0, 1);
    Result.Roughness = 0.5f;
    Result.Metallic = 0.0f;
    Result.Emissive = float3(0, 0, 0);
    Result.Opacity = (n_5).x;
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
