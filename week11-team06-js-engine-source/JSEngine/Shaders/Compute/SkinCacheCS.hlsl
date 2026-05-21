#include "../Common/SkinCacheVertexLayouts.hlsli"
#include "../Common/SkeletalSkinning.hlsli"

ByteAddressBuffer SourceVertices : register(t0);
RWByteAddressBuffer OutputVertices : register(u0);

FSkeletalSourceVertex ApplyMorphTargetDeltas(FSkeletalSourceVertex Source, uint VertexIndex)
{
    // Future morph target support reads morph delta/weight buffers here.
    // Deformation order is morph first, then skeletal skinning.
    return Source;
}

[numthreads(64, 1, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    const uint VertexIndex = DispatchThreadID.x;
    uint SourceByteSize = 0;
    SourceVertices.GetDimensions(SourceByteSize);
    const uint VertexCount = SourceByteSize / FSkeletalSourceVertexStride;
    if (VertexIndex >= VertexCount)
    {
        return;
    }

    FSkeletalSourceVertex Source = LoadSkeletalSourceVertex(SourceVertices, VertexIndex);
    Source = ApplyMorphTargetDeltas(Source, VertexIndex);

    FSkeletalSkinningInput SkinInput;
    SkinInput.Position = Source.Position;
    SkinInput.Normal = Source.Normal;
    SkinInput.Tangent = Source.Tangent;
    SkinInput.BoneIndices = UnpackBoneIndices(Source.BoneIndicesPacked);
    SkinInput.BoneWeights = Source.BoneWeights;

    FSkeletalSkinningOutput Skinned = ApplySkeletalSkinning(SkinInput);

    FDeformedVertex Output;
    Output.Position = Skinned.Position;
    Output.Color = Source.Color;
    Output.Normal = Skinned.Normal;
    Output.UVs = Source.UVs;
    Output.Tangent = Skinned.Tangent;
    StoreDeformedVertex(OutputVertices, VertexIndex, Output);
}
