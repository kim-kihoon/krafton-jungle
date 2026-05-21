#ifndef SKELETAL_SKINNING_H
#define SKELETAL_SKINNING_H

struct FSkinningBoneMatrix
{
    row_major float4x4 Matrix;
};

StructuredBuffer<FSkinningBoneMatrix> BoneMatrices : register(t16);

struct FSkeletalSkinningInput
{
    float3 Position;
    float3 Normal;
    float4 Tangent;
    uint4 BoneIndices;
    float4 BoneWeights;
};

struct FSkeletalSkinningOutput
{
    float3 Position;
    float3 Normal;
    float4 Tangent;
};

uint GetBoneMatrixCount()
{
    uint Count = 0;
    uint Stride = 0;
    BoneMatrices.GetDimensions(Count, Stride);
    return Count;
}

FSkeletalSkinningOutput ApplySkeletalSkinning(FSkeletalSkinningInput Input)
{
    FSkeletalSkinningOutput Output;
    Output.Position = Input.Position;
    Output.Normal = Input.Normal;
    Output.Tangent = Input.Tangent;

    const uint BoneCount = GetBoneMatrixCount();
    if (BoneCount == 0)
    {
        return Output;
    }

    float ValidWeightSum = 0.0f;
    [unroll]
    for (uint InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
    {
        const uint BoneIndex = Input.BoneIndices[InfluenceIndex];
        const float Weight = Input.BoneWeights[InfluenceIndex];
        if (BoneIndex < BoneCount && Weight > 0.0f)
        {
            ValidWeightSum += Weight;
        }
    }

    if (ValidWeightSum <= 1e-6f)
    {
        return Output;
    }

    float3 SkinnedPosition = float3(0.0f, 0.0f, 0.0f);
    float3 SkinnedNormal = float3(0.0f, 0.0f, 0.0f);
    float3 SkinnedTangent = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
    {
        const uint BoneIndex = Input.BoneIndices[InfluenceIndex];
        const float RawWeight = Input.BoneWeights[InfluenceIndex];
        if (BoneIndex >= BoneCount || RawWeight <= 0.0f)
        {
            continue;
        }

        const float Weight = RawWeight / ValidWeightSum;
        SkinnedPosition += mul(float4(Input.Position, 1.0f), BoneMatrices[BoneIndex].Matrix).xyz * Weight;
        SkinnedNormal += mul(Input.Normal, (float3x3)BoneMatrices[BoneIndex].Matrix) * Weight;
        SkinnedTangent += mul(Input.Tangent.xyz, (float3x3)BoneMatrices[BoneIndex].Matrix) * Weight;
    }

    const float NormalLengthSq = dot(SkinnedNormal, SkinnedNormal);
    const float TangentLengthSq = dot(SkinnedTangent, SkinnedTangent);

    Output.Position = SkinnedPosition;
    Output.Normal = NormalLengthSq > 1e-8f ? normalize(SkinnedNormal) : Input.Normal;
    Output.Tangent = float4(TangentLengthSq > 1e-8f ? normalize(SkinnedTangent) : Input.Tangent.xyz, Input.Tangent.w);
    return Output;
}

#endif
