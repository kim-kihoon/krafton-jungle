#include "../Common/Common.hlsli"
#include "../Common/SkeletalSkinning.hlsli"

cbuffer MeshOverlayBuffer : register(b12)
{
    uint OverlayMode;
    int SelectedBoneIndex;
    float Opacity;
    float MeshOverlayPadding;
}

struct VSInputSkeletalMesh
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float4 Tangent : TANGENT;
    float4 Color : COLOR;
    uint4 BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float Weight : TEXCOORD0;
};

float GetSelectedBoneWeight(uint4 BoneIndices, float4 BoneWeights)
{
    if (SelectedBoneIndex < 0)
    {
        return 0.0f;
    }

    const uint BoneIndex = (uint)SelectedBoneIndex;
    float Weight = 0.0f;

    [unroll]
    for (uint InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
    {
        if (BoneIndices[InfluenceIndex] == BoneIndex)
        {
            Weight += BoneWeights[InfluenceIndex];
        }
    }

    return saturate(Weight);
}

VSOutput VS(VSInputSkeletalMesh Input)
{
    FSkeletalSkinningInput SkinInput;
    SkinInput.Position = Input.Position;
    SkinInput.Normal = Input.Normal;
    SkinInput.Tangent = Input.Tangent;
    SkinInput.BoneIndices = Input.BoneIndices;
    SkinInput.BoneWeights = Input.BoneWeights;

    FSkeletalSkinningOutput Skinned = ApplySkeletalSkinning(SkinInput);

    VSOutput Output;
    Output.Position = ApplyMVP(Skinned.Position);
    Output.Weight = GetSelectedBoneWeight(Input.BoneIndices, Input.BoneWeights);
    return Output;
}

float3 EvaluateBoneWeightHeatmap(float Weight)
{
    Weight = saturate(Weight);

    const float3 Purple = float3(0.45f, 0.05f, 0.80f);
    const float3 Blue = float3(0.05f, 0.20f, 1.00f);
    const float3 Green = float3(0.05f, 0.85f, 0.20f);
    const float3 Yellow = float3(1.00f, 0.95f, 0.05f);
    const float3 Orange = float3(1.00f, 0.45f, 0.02f);
    const float3 Red = float3(1.00f, 0.05f, 0.02f);

    if (Weight >= 0.8f)
    {
        return lerp(Orange, Red, (Weight - 0.8f) / 0.2f);
    }
    if (Weight >= 0.6f)
    {
        return lerp(Yellow, Orange, (Weight - 0.6f) / 0.2f);
    }
    if (Weight >= 0.4f)
    {
        return lerp(Green, Yellow, (Weight - 0.4f) / 0.2f);
    }
    if (Weight >= 0.2f)
    {
        return lerp(Blue, Green, (Weight - 0.2f) / 0.2f);
    }

    return lerp(Purple, Blue, Weight / 0.2f);
}

float4 PS(VSOutput Input) : SV_TARGET
{
    if (OverlayMode != 1u)
    {
        discard;
    }

    const float Weight = saturate(Input.Weight);
    const float3 Color = EvaluateBoneWeightHeatmap(Weight);
    return float4(Color, saturate(Opacity));
}
