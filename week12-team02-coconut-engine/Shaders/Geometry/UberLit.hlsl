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
    float3 _pad;
};

// 머티리얼 확장 파라미터 — 팀원 A CB 시스템 완성 후 b2 확장 예정
static const float4 g_DefaultEmissive = float4(0, 0, 0, 0);
static const float g_DefaultShininess = 32.0f;

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
    float boneWeightHeat : TEXCOORD4;
#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 litDiffuse  : TEXCOORD2;
    float3 litSpecular : TEXCOORD3;
#endif
};

float3 ComputeBoneWeightHeatmapBaseColor(float3 N)
{
    float3 normal = normalize(N);
    float3 lightDir = normalize(float3(-0.35f, 0.70f, -0.45f));
    float light = 0.92f + 0.08f * saturate(dot(normal, lightDir));

    float3 basePink = float3(1.00f, 0.12f, 0.92f);
    float3 baseLavender = float3(0.92f, 0.10f, 1.00f);
    float tint = 0.5f + 0.5f * saturate(normal.z * 0.5f + 0.5f);

    return lerp(baseLavender, basePink, tint) * light;
}

float3 ComputeRainbowBoneWeightColor(float weight)
{
    float t = smoothstep(0.0f, 1.0f, saturate(weight));

    float3 c0 = float3(0.48f, 0.00f, 1.00f); // violet
    float3 c1 = float3(0.05f, 0.00f, 0.86f); // indigo
    float3 c2 = float3(0.00f, 0.18f, 1.00f); // blue
    float3 c3 = float3(0.00f, 0.92f, 0.08f); // green
    float3 c4 = float3(1.00f, 0.95f, 0.00f); // yellow
    float3 c5 = float3(1.00f, 0.42f, 0.00f); // orange
    float3 c6 = float3(1.00f, 0.00f, 0.00f); // red

    if (t < (1.0f / 6.0f))
    {
        float s = smoothstep(0.0f, 1.0f / 6.0f, t);
        return lerp(c0, c1, s);
    }
    else if (t < (2.0f / 6.0f))
    {
        float s = smoothstep(1.0f / 6.0f, 2.0f / 6.0f, t);
        return lerp(c1, c2, s);
    }
    else if (t < (3.0f / 6.0f))
    {
        float s = smoothstep(2.0f / 6.0f, 3.0f / 6.0f, t);
        return lerp(c2, c3, s);
    }
    else if (t < (4.0f / 6.0f))
    {
        float s = smoothstep(3.0f / 6.0f, 4.0f / 6.0f, t);
        return lerp(c3, c4, s);
    }
    else if (t < (5.0f / 6.0f))
    {
        float s = smoothstep(4.0f / 6.0f, 5.0f / 6.0f, t);
        return lerp(c4, c5, s);
    }

    float s = smoothstep(5.0f / 6.0f, 1.0f, t);
    return lerp(c5, c6, s);
}

float3 ApplyBoneWeightHeatmap(float weight, float3 baseColor, float3 N)
{
    float w = saturate(weight);
    float aa = max(fwidth(w), 0.012f);
    float influence = smoothstep(0.01f - aa, 0.08f + aa, w);
    float3 heatColor = ComputeRainbowBoneWeightColor(w);

    float3 normal = normalize(N);
    float3 lightDir = normalize(float3(-0.35f, 0.70f, -0.45f));
    float light = 0.96f + 0.04f * saturate(dot(normal, lightDir));

    return lerp(baseColor, heatColor * light, influence);
}


// =============================================================================
// Vertex Shader
// =============================================================================
UberVS_Output BuildUberVSOutput(
    float3 position,
    float3 normal,
    float4 color,
    float2 texcoord,
    float4 tangent,
    float boneWeightHeat)
{
    UberVS_Output output;
    
    float3x3 M = (float3x3) Model;

    float4 worldPos4 = mul(float4(position, 1.0f), Model);
    output.worldPos = worldPos4.xyz;
    output.position = mul(mul(worldPos4, View), Projection);
    output.normal = normalize(mul(normal, (float3x3) NormalMatrix));
    output.color = color * SectionColor;
    output.texcoord = texcoord;
    output.boneWeightHeat = boneWeightHeat;

    float3 T = normalize(mul(tangent.xyz, M));
    T = normalize(T - output.normal * dot(output.normal, T));
    output.tangent = float4(T, tangent.w);

#if defined(LIGHTING_MODEL_GOURAUD) && LIGHTING_MODEL_GOURAUD
    float3 N =  output.normal;

    if (HasNormalMap > 0.5f)
    {
        float3 B = normalize(cross(N, T) * tangent.w);
        float3x3 TBN = float3x3(T, B, N);

        float3 tangentNormal = NormalTexture.SampleLevel(LinearWrapSampler, texcoord, 0).xyz * 2.0f - 1.0f;

        N = normalize(mul(tangentNormal, TBN));
    }

    float3 V = normalize(CameraWorldPos - output.worldPos);
    output.litDiffuse = AccumulateDiffuseVS(output.worldPos, N);
    output.litSpecular = AccumulateSpecularVS(output.worldPos, N, V, g_DefaultShininess);

#endif

    return output;
}

UberVS_Output VS_Static(VS_Input_PNCTT input)
{
    return BuildUberVSOutput(
        input.position,
        input.normal,
        input.color,
        input.texcoord,
        input.tangent,
        0.0f);
}

UberVS_Output VS_Skeletal(VS_Input_PNCTBW input)
{
    float3 position = input.position;
    float3 normal = input.normal;
    float4 tangent = input.tangent;
    float boneWeightHeat = 0.0f;

    if (HeatmapMode != 0 && SelectedBoneIndex >= 0)
    {
        boneWeightHeat = GetBoneWeightByIndex(
            input.boneIndices,
            input.boneWeights,
            SelectedBoneIndex);
    }

    if (SkinningMode == 1 && GetSkinWeightSum(input.boneIndices, input.boneWeights) > 0.0f)
    {
        float4x4 skinMatrixT = BuildSkinMatrix(input.boneIndices, input.boneWeights);
        float4x4 skinMatrix = transpose(skinMatrixT);
        
        
        position = mul(float4(input.position, 1.0f), skinMatrix).xyz;
        normal = normalize(mul(float4(input.normal, 0.0f), skinMatrix).xyz);
        tangent.xyz = normalize(mul(float4(input.tangent.xyz, 0.0f), skinMatrix).xyz);
    }

    return BuildUberVSOutput(
        position,
        normal,
        input.color,
        input.texcoord,
        tangent,
        boneWeightHeat);
}

UberVS_Output VS(VS_Input_PNCTT input)
{
    return VS_Static(input);
}

// =============================================================================
// MRT 출력 구조체
// =============================================================================
struct UberPS_Output
{
    float4 Color : SV_TARGET0; // 최종 색상 (기존 프레임 버퍼)
    float4 Normal : SV_TARGET1; // World Normal (GBuffer Normal RT)
    float4 Culling : SV_TARGET2; // Tile Culling Heatmap
};

// =============================================================================
// Pixel Shader
// =============================================================================
UberPS_Output PS(UberVS_Output input)
{
    UberPS_Output output;

    float3 N = normalize(input.normal);

    // Bone Weight Heatmap
    if (HeatmapMode != 0)
    {
        float3 finalColor = ComputeBoneWeightHeatmapBaseColor(N);

        if (SelectedBoneIndex >= 0)
        {
            float weight = saturate(input.boneWeightHeat);
            finalColor = ApplyBoneWeightHeatmap(weight, finalColor, N);
        }

        output.Color = float4(ApplyWireframe(finalColor), 1.0f);
        output.Normal = float4(N, 1.0f);
        output.Culling = float4(0, 0, 0, 0);
        
        return output;
    }

    float4 texColor = DiffuseTexture.Sample(LinearWrapSampler, input.texcoord);
    if (texColor.a < 0.001f)
        texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

    float4 baseColor = texColor * input.color;

#if !defined(LIGHTING_MODEL_GOURAUD)
    if (HasNormalMap >= 0.5)
    {
        float3 T = normalize(input.tangent.xyz);
        T = normalize(T - N * dot(N, T));

        float3 B = normalize(cross(N, T) * input.tangent.w);
        float3x3 TBN = float3x3(T, B, N);

        float3 tangentNormal = NormalTexture.Sample(LinearWrapSampler, input.texcoord).xyz * 2.0f - 1.0f;
        N = normalize(mul(tangentNormal, TBN));
    }
#endif

    float3 V = normalize(CameraWorldPos - input.worldPos);

#if defined(LIGHTING_MODEL_UNLIT) && LIGHTING_MODEL_UNLIT
    // Unlit: 라이팅 없이 Albedo만 출력
    float3 finalColor = ApplyWireframe(baseColor.rgb);
    output.Culling = float4(0, 0, 0, 0);

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
    specular = AccumulateSpecular(input.worldPos, N, V, g_DefaultShininess, input.position);

#endif

    output.Culling = ComputeCullingHeatmap(input.position, input.worldPos);
    // Diffuse에만 albedo를 곱하고, Specular는 빛 색상 그대로 더한다
    // (비금속 표면: specular 반사 = 빛의 색, 물체 색이 아님)
    float3 finalColor = baseColor.rgb * diffuse + specular + g_DefaultEmissive.rgb;
    finalColor = ApplyWireframe(finalColor);
#endif

    output.Color = float4(finalColor, baseColor.a);
    output.Normal = float4(N, 1.0f); // alpha=1: 유효한 노말 마킹
    
    return output;
}
