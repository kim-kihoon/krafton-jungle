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

float4 DepthPrepassVS(VSInput input) : SV_POSITION
{
    return ApplyMVP(input.Position);
}

float4 SkeletalDepthPrepassVS(SkeletalVSInput input) : SV_POSITION
{
    FSkeletalSkinningInput SkinInput;
    SkinInput.Position = input.Position;
    SkinInput.Normal = input.Normal;
    SkinInput.Tangent = input.Tangent;
    SkinInput.BoneIndices = input.BoneIndices;
    SkinInput.BoneWeights = input.BoneWeights;
    return ApplyMVP(ApplySkeletalSkinning(SkinInput).Position);
}

float4 SkinCacheDepthPrepassVS(SkinCacheVSInput input) : SV_POSITION
{
    return ApplyMVP(input.Position);
}

void DepthPrepassPS() {}
