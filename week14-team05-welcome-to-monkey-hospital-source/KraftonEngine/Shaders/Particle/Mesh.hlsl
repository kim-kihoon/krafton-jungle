#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

// Particle Mesh — 정적 mesh + per-instance transform/color로 DrawIndexedInstanced.
// slot 0: 정적 mesh VB (FVertexPNCT layout, 기존 StaticMesh와 동일)
// slot 1: per-instance VB (FParticleMeshInstanceVertex) — INSTANCE_ prefix로 reflection에서 자동 분리

// t0: Material.CachedSRVs[Diffuse] (Atlas 텍스처)
Texture2D DiffuseTexture : register(t0);

// b2 (PerShader0): .mat의 Parameters 키에서 자동 매핑
cbuffer ParticleMeshParams : register(b2)
{
    float4 BaseColor;      // 추가 tint
    float  Opacity;        // [0,1]
    float  UseTexture;     // 0=텍스처 무시, 1=사용
    float  SubImagesH;     // atlas 가로 분할 (1 = SubUV 비활성)
    float  SubImagesV;     // atlas 세로 분할
}

struct VS_Input_MeshParticle
{
    // ---- slot 0 (per-vertex) ----
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD;
    // ---- slot 1 (per-instance) ----
    // World matrix의 row 0/1/2/3 (translation은 row 3)
    float4 transform0 : INSTANCE_TRANSFORM0;
    float4 transform1 : INSTANCE_TRANSFORM1;
    float4 transform2 : INSTANCE_TRANSFORM2;
    float4 transform3 : INSTANCE_TRANSFORM3;
    float4 instColor  : INSTANCE_COLOR;
    int    subImage   : INSTANCE_SUBIMAGE;
};

struct PS_Input_MeshParticle
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD0;
    nointerpolation int subImage : TEXCOORD1;   // per-instance — interpolation 안 함
};

PS_Input_MeshParticle VS(VS_Input_MeshParticle input)
{
    // row-major world matrix 재구성.
    float4x4 worldMatrix = float4x4(
        input.transform0,
        input.transform1,
        input.transform2,
        input.transform3
    );
    float4 worldPos = mul(float4(input.position, 1.0), worldMatrix);

    PS_Input_MeshParticle output;
    output.position = ApplyVP(worldPos.xyz);
    output.color    = input.color * input.instColor;
    output.texcoord = input.texcoord;
    output.subImage = input.subImage;
    return output;
}

float4 PS(PS_Input_MeshParticle input) : SV_TARGET
{
    // SubImage → atlas tile UV 변환.
    // SubImagesH/V == 1이면 tile 1개 = 텍스처 전체 (SubUV 비활성).
    const int SubH = max(1, (int)SubImagesH);
    const int SubV = max(1, (int)SubImagesV);
    const float TileW = 1.0 / (float)SubH;
    const float TileH = 1.0 / (float)SubV;
    const int Col = input.subImage % SubH;
    const int Row = (input.subImage / SubH) % SubV;
    const float2 AtlasUV = float2(
        ((float)Col + input.texcoord.x) * TileW,
        ((float)Row + input.texcoord.y) * TileH
    );

    float4 sampled = DiffuseTexture.Sample(LinearClampSampler, AtlasUV);
    float3 texRgb = lerp(float3(1,1,1), sampled.rgb, UseTexture);
    float  texA   = lerp(1.0,            sampled.a,   UseTexture);

    float3 rgb = input.color.rgb * BaseColor.rgb * texRgb;
    float  a   = input.color.a   * BaseColor.a   * Opacity * texA;
    return float4(ApplyWireframe(rgb), a);
}
