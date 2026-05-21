#include "../Common/Common.hlsli"
#include "../Common/SkeletalSkinning.hlsli"

struct VSInput
{
    float3 Position : POSITION;
};

struct SkeletalVSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float4 Tangent : TANGENT;
    float4 Color : COLOR;
    uint4 BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

struct SkinCacheVSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float4 Tangent : TANGENT;
    float4 Color : COLOR;
};

float4 ShadowVS(VSInput input) : SV_POSITION
{
    float4 worldPos = mul(float4(input.Position, 1.0f), Model);
    float4 post = worldPos;

#ifdef SHADOW_MAP_PSM
    float4 camClip = mul(post, VirtualViewProj);
    if (abs(camClip.w) > 1e-5f)
    {
        post = float4(camClip.xyz / camClip.w, 1.0f);
    }
#endif

    float4 shadowPos = mul(post, ShadowViewProj);
    return shadowPos;
}

float4 SkeletalShadowVS(SkeletalVSInput input) : SV_POSITION
{
    FSkeletalSkinningInput SkinInput;
    SkinInput.Position = input.Position;
    SkinInput.Normal = input.Normal;
    SkinInput.Tangent = input.Tangent;
    SkinInput.BoneIndices = input.BoneIndices;
    SkinInput.BoneWeights = input.BoneWeights;

    VSInput SkinnedInput;
    SkinnedInput.Position = ApplySkeletalSkinning(SkinInput).Position;
    return ShadowVS(SkinnedInput);
}

float4 SkinCacheShadowVS(SkinCacheVSInput input) : SV_POSITION
{
    VSInput DeformedInput;
    DeformedInput.Position = input.Position;
    return ShadowVS(DeformedInput);
}

void ShadowPS()
{
}
