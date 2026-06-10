#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/ConstantBuffers.hlsli"
#include "Common/Skinning.hlsli"
#include "Common/NormalMapping.hlsli"
#include "Common/LightCullingDebug.hlsli"

Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);

cbuffer PerShader1 : register(b2)
{
    float4 SectionColor;
    float HasNormalMap;
    float Opacity;
    float2 _pad;
};

struct ViewModeMeshVS_Output
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent : TANGENT;
    float3 worldPos : TEXCOORD1;
    float overlayScalar : TEXCOORD2;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

ViewModeMeshVS_Output VS_StaticMesh(VS_Input_PNCTT input)
{
    ViewModeMeshVS_Output output;

    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.position = mul(mul(worldPos, View), Projection);
    output.normal = normalize(mul(input.normal, (float3x3)NormalMatrix));
    output.texcoord = input.texcoord;
    output.worldPos = worldPos.xyz;
    output.overlayScalar = input.color.w;

    float3x3 M = (float3x3)Model;
    float3 T = BuildOrthonormalTangent(output.normal, mul(input.tangent.xyz, M));
    output.tangent = float4(T, input.tangent.w);

    return output;
}

ViewModeMeshVS_Output VS_SkeletalMesh(VS_Input_PNCTTBB input)
{
    FSkinningResult skinned = ApplyLinearBlendSkinning(
        input.position,
        input.normal,
        input.tangent.xyz,
        input.boneIndices,
        input.boneWeights);

    ViewModeMeshVS_Output output;

    float4 worldPos = mul(skinned.position, Model);
    output.position = mul(mul(worldPos, View), Projection);
    output.normal = normalize(mul(skinned.normal, (float3x3)NormalMatrix));
    output.texcoord = input.texcoord;
    output.worldPos = worldPos.xyz;
    output.overlayScalar = GetBoneInfluenceWeight(input.boneIndices, input.boneWeights, SelectedBoneIndex);

    float3x3 M = (float3x3)Model;
    float3 T = BuildOrthonormalTangent(output.normal, mul(skinned.tangent, M));
    output.tangent = float4(T, input.tangent.w);

    return output;
}

float4 PS(ViewModeMeshVS_Output input) : SV_TARGET
{
    if (VisualizeLightCulling != 0)
    {
        return ComputeCullingHeatmap(input.position, input.worldPos);
    }

    if (MeshScalarOverlayMode != 0)
    {
        if (MeshScalarOverlayMode == 3)
        {
            return float4(0.0f, 0.0f, 0.0f, saturate(MeshScalarOverlayAlpha));
        }

        float heat = saturate(input.overlayScalar);

        float t0 = smoothstep(0.0f, 0.05f, heat);
        float t1 = smoothstep(0.05f, 0.2f, heat);
        float t2 = smoothstep(0.2f, 0.35f, heat);
        float t3 = smoothstep(0.35f, 0.5f, heat);
        float t4 = smoothstep(0.5f, 1.0f, heat);

        float3 heatColor = lerp(float3(1.0f, 0.0f, 1.0f), float3(0.0f, 0.0f, 1.0f), t0);
        heatColor = lerp(heatColor, float3(0.0f, 1.0f, 1.0f), t1);
        heatColor = lerp(heatColor, float3(0.0f, 0.9f, 0.15f), t2);
        heatColor = lerp(heatColor, float3(1.0f, 1.0f, 0.0f), t3);
        heatColor = lerp(heatColor, float3(1.0f, 0.05f, 0.0f), t4);

        return float4(heatColor, saturate(MeshScalarOverlayAlpha));
    }

    float3 N = normalize(input.normal);

    if (HasNormalMap >= 0.5f)
    {
        float3 tangentNormal = SampleTangentSpaceNormal(NormalTexture, LinearWrapSampler, input.texcoord);
        N = ApplyTangentSpaceNormal(N, input.tangent.xyz, input.tangent.w, tangentNormal);
    }

    return float4(N * 0.5f + 0.5f, 1.0f);
}
