// =============================================================================
// UberLit.hlsl — Uber Shader for Forward Shading
// =============================================================================
// Preprocessor Definitions (C++ 에서 D3D_SHADER_MACRO 로 전달):
//   LIGHTING_MODEL_GOURAUD  1  — 정점 단계 라이팅 (Gouraud Shading)
//   LIGHTING_MODEL_LAMBERT  1  — 픽셀 단계 Diffuse only (Lambert)
//   LIGHTING_MODEL_PHONG    1  — 픽셀 단계 Diffuse + Specular (Blinn-Phong)
//
// 아무 라이팅 모델 매크로도 없으면 기본값 = Blinn-Phong
//   LIGHTING_MODEL_UNLIT   1  — 라이팅 없음 (Albedo + Wireframe)
// =============================================================================

#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Common/Skinning.hlsli"
#include "Common/NormalMapping.hlsli"

#if !defined(LIGHTING_MODEL_UNLIT)
#include "Common/ForwardLighting.hlsli"
#endif

// ── 기본값 설정 ──
#if !defined(LIGHTING_MODEL_GOURAUD) && !defined(LIGHTING_MODEL_LAMBERT) && !defined(LIGHTING_MODEL_PHONG) && !defined(LIGHTING_MODEL_UNLIT)
#define LIGHTING_MODEL_PHONG 1
#endif

// =============================================================================
// 텍스처
// =============================================================================
Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);

// ── Per-Object Material (b2) — 기존 StaticMesh 와 레이아웃 동일 (호환성) ──
cbuffer PerShader1 : register(b2)
{
    float4 SectionColor;
    float HasNormalMap;
    float Opacity;   // 머티리얼 불투명도 [0,1]. 기본 1, Transparent 블렌드에서만 가시 효과.
    float bEmissive;
    float EmissiveIntensity;
    float SpecularIntensity;
    float Shininess;
    float Metallic;
    float AlphaClip; // 0=off. blood 데칼 등 알파 컷아웃 전용(>0 일 때만 별도 경로).
};


// 머티리얼 확장 파라미터 — 팀원 A CB 시스템 완성 후 b2 확장 예정
static const float4 g_DefaultEmissive = float4(0, 0, 0, 0);
static const float g_DefaultShininess = 8.0f;

float GetMaterialShininess()
{
    return Shininess > 0.0f ? max(2.0f, Shininess) : g_DefaultShininess;
}

float GetMaterialSpecularIntensity()
{
    return SpecularIntensity > 0.0f ? SpecularIntensity : 1.0f;
}

// =============================================================================
// VS ↔ PS 인터페이스
// =============================================================================
struct UberVS_Output
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float4 tangent : TANGENT;
#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 litDiffuse  : TEXCOORD2;
    float3 litSpecular : TEXCOORD3;
#endif
};

// =============================================================================
// Vertex Shader
// =============================================================================
UberVS_Output VS_StaticMesh(VS_Input_PNCTT input)
{
    UberVS_Output output;
    
    float3x3 M = (float3x3) Model;

    float4 worldPos4 = mul(float4(input.position, 1.0f), Model);
    output.worldPos = worldPos4.xyz;
    output.position = mul(mul(worldPos4, View), Projection);
    output.normal = normalize(mul(input.normal, (float3x3) NormalMatrix));
    output.color = input.color * SectionColor;
    output.texcoord = input.texcoord;

    float3 T = BuildOrthonormalTangent(output.normal, mul(input.tangent.xyz, M));
    output.tangent = float4(T, input.tangent.w);

#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 N =  output.normal;

    if (HasNormalMap > 0.5f)
    {
        float3 tangentNormal = SampleTangentSpaceNormalLevel(NormalTexture, LinearWrapSampler, input.texcoord, 0);
        N = ApplyTangentSpaceNormal(N, T, input.tangent.w, tangentNormal);
    }

    float3 V = normalize(CameraWorldPos - output.worldPos);
    output.litDiffuse = AccumulateDiffuseVS(output.worldPos, N);
    output.litSpecular = AccumulateSpecularVS(output.worldPos, N, V, GetMaterialShininess());

#endif

    return output;
}

// GPU Skinning
UberVS_Output VS_SkeletalMesh(VS_Input_PNCTTBB input)
{
    UberVS_Output output;
    
    FSkinningResult skinned = ApplyLinearBlendSkinning(
    input.position,
    input.normal,
    input.tangent.xyz,
    input.boneIndices,
    input.boneWeights);

    float4 WeightedPosition = skinned.position;
    float3 WeightedNormal = skinned.normal;
    float3 WeightedTangent = skinned.tangent;
    
    float3x3 M = (float3x3) Model;
    
    float4 worldPos4 = mul(WeightedPosition, Model);
    output.worldPos = worldPos4.xyz;
    output.position = mul(mul(worldPos4, View), Projection);
    output.normal = normalize(mul(WeightedNormal, (float3x3) NormalMatrix));
    output.color = input.color * SectionColor;
    output.texcoord = input.texcoord;

    float3 T = BuildOrthonormalTangent(output.normal, mul(WeightedTangent, M));
    output.tangent = float4(T, input.tangent.w);

#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 N =  output.normal;

    if (HasNormalMap > 0.5f)
    {
        float3 tangentNormal = SampleTangentSpaceNormalLevel(NormalTexture, LinearWrapSampler, input.texcoord, 0);
        N = ApplyTangentSpaceNormal(N, T, output.tangent.w, tangentNormal);
    }

    float3 V = normalize(CameraWorldPos - output.worldPos);
    output.litDiffuse = AccumulateDiffuseVS(output.worldPos, N);
    output.litSpecular = AccumulateSpecularVS(output.worldPos, N, V, GetMaterialShininess());

#endif

    return output;
}

// =============================================================================
// Pixel Shader
// =============================================================================
float4 PS(UberVS_Output input) : SV_TARGET
{
    float4 texColor = DiffuseTexture.Sample(LinearWrapSampler, input.texcoord);

    // AlphaClip>0 인 머티리얼만 데칼 전용 경로. 맵 기본 머티리얼(AlphaClip=0)은 아래 기존 로직 그대로.
    if (AlphaClip > 0.001f)
    {
        if (texColor.a < AlphaClip)
            discard;
        if (texColor.r > 0.85f && texColor.g > 0.85f && texColor.b > 0.85f)
            discard;
        float4 decalColor = texColor * input.color;
        return float4(ApplyWireframe(decalColor.rgb * EmissiveIntensity), 1.0f);
    }

    if (texColor.a < 0.001f)
        texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

    float4 baseColor = texColor * input.color;

    if (bEmissive >= 0.5f)
    {
        return float4(ApplyWireframe(baseColor.rgb * EmissiveIntensity), baseColor.a * Opacity);
    }

    float3 N = normalize(input.normal);

#if !defined(LIGHTING_MODEL_GOURAUD)
    if (HasNormalMap >= 0.5)
    {
        float3 tangentNormal = SampleTangentSpaceNormal(NormalTexture, LinearWrapSampler, input.texcoord);
        N = ApplyTangentSpaceNormal(N, input.tangent.xyz, input.tangent.w, tangentNormal);
    }
#endif

    float3 V = normalize(CameraWorldPos - input.worldPos);

#if defined(LIGHTING_MODEL_UNLIT) && LIGHTING_MODEL_UNLIT
    // Unlit: 라이팅 없이 Albedo만 출력
    float3 finalColor = ApplyWireframe(baseColor.rgb);

#else
    float3 diffuse = float3(0, 0, 0);
    float3 specular = float3(0, 0, 0);

#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    // Gouraud: VS에서 정점 단위로 계산 → PS에서 보간된 값 사용
    diffuse  = input.litDiffuse;
    specular = input.litSpecular;

#elif defined(LIGHTING_MODEL_LAMBERT) && LIGHTING_MODEL_LAMBERT
    diffuse = AccumulateDiffuse(input.worldPos, N, input.position);

#elif defined(LIGHTING_MODEL_PHONG) && LIGHTING_MODEL_PHONG
    diffuse = AccumulateDiffuse(input.worldPos, N, input.position);
    specular = AccumulateSpecular(input.worldPos, N, V, GetMaterialShininess(), input.position);

#endif

    // Diffuse에만 albedo를 곱하고, Specular는 빛 색상 그대로 더한다
    // (비금속 표면: specular 반사 = 빛의 색, 물체 색이 아님)
    float metallic = saturate(Metallic);
    float3 diffuseColor = baseColor.rgb;
    float3 specularColor = lerp(float3(1.0f, 1.0f, 1.0f), baseColor.rgb, metallic);
    specular *= GetMaterialSpecularIntensity() * specularColor;
    float3 finalColor = diffuseColor * diffuse + specular + g_DefaultEmissive.rgb;
    finalColor = ApplyWireframe(finalColor);
#endif

    return float4(finalColor, baseColor.a * Opacity); // Opacity는 alpha에만 (Transparent 블렌드에서 효과)
}
