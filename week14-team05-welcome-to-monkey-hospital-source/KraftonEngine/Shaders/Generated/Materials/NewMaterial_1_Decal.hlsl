// Generated from C:/Projects/Jungle_Week13_Team2/KraftonEngine/Content/Material/NewMaterial_1.uasset
// Domain: Decal

#include "Common/ConstantBuffers.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

struct FMaterialPixelInput
{
    float2 UV0;
    float2 UV1;
    float2 UV2;
    float4 ParticleColor;
    float4 VertexColor;
    float  Time;
    float  SubImageIndex;
    float4 DynamicParam;
};

struct FMaterialResult
{
    float3 BaseColor;
    float3 Normal;
    float Roughness;
    float Metallic;
    float3 Emissive;
    float Opacity;
};

Texture2D Tex_Diffuse : register(t0);

FMaterialResult EvaluateMaterial(FMaterialPixelInput Input)
{
    float n_61 = 0.500000f;
    float4 n_5 = Tex_Diffuse.Sample(LinearWrapSampler, Input.UV0);
    float4 n_63 = (float4(n_61, n_61, n_61, n_61) * n_5);
    float n_48 = 1.000000f;
    FMaterialResult Result;
    Result.BaseColor = (n_63).xyz;
    Result.Normal = float3(0, 0, 1);
    Result.Roughness = 0.5f;
    Result.Metallic = 0.0f;
    Result.Emissive = float3(0, 0, 0);
    Result.Opacity = n_48;
    return Result;
}


cbuffer DecalBuffer : register(b2)
{
    float4x4 DecalWorldToLocal;
    float4 DecalColor;
}

PS_Input_Decal VS(VS_Input_PNCT input)
{
    PS_Input_Decal output;
    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.position = mul(mul(worldPos, View), Projection);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul(input.normal, (float3x3)NormalMatrix));
    return output;
}

float4 PS(PS_Input_Decal input) : SV_TARGET
{
    float3 decalLocalPos = mul(float4(input.worldPos, 1.0f), DecalWorldToLocal).xyz;

    if (abs(decalLocalPos.x) > 0.5f || abs(decalLocalPos.y) > 0.5f || abs(decalLocalPos.z) > 0.5f)
    {
        discard;
    }

    float2 decalUV;
    decalUV.x = decalLocalPos.y + 0.5f;
    decalUV.y = 0.5f - decalLocalPos.z;

    FMaterialPixelInput MaterialInput;
    MaterialInput.UV0           = decalUV;
    MaterialInput.UV1           = float2(0, 0);
    MaterialInput.UV2           = float2(0, 0);
    MaterialInput.ParticleColor = float4(1, 1, 1, 1);
    MaterialInput.VertexColor   = float4(1, 1, 1, 1);
    MaterialInput.Time          = Time;
    MaterialInput.SubImageIndex = 0.0f;
    MaterialInput.DynamicParam  = float4(0, 0, 0, 0);

    FMaterialResult Result = EvaluateMaterial(MaterialInput);
    float OutOpacity = saturate(Result.Opacity);
    clip(OutOpacity - 0.001f);

    float3 N = normalize(input.normal);

    float3 finalRgb = Result.BaseColor + Result.Emissive;

    return float4(ApplyWireframe(finalRgb) * DecalColor.rgb, OutOpacity * DecalColor.a);
}
