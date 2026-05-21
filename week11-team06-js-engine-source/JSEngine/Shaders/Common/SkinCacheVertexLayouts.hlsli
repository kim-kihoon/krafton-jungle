// FSkeletalMeshVertex와 같은 byte layout으로 읽는 Skin Cache source vertex
// BoneIndices는 C++의 uint8[4]를 uint 하나로 읽고 byte 단위로 해제함
struct FSkeletalSourceVertex
{
    float3 Position;
    float4 Color;
    float3 Normal;
    float2 UVs;
    float4 Tangent;
    uint BoneIndicesPacked;
    float4 BoneWeights;
};

// FDeformedVertex와 같은 layout의 Skin Cache output vertex
// 이미 변형된 stream이라 BLENDINDICES / BLENDWEIGHT를 포함하지 않음
struct FDeformedVertex
{
    float3 Position;
    float4 Color;
    float3 Normal;
    float2 UVs;
    float4 Tangent;
};

uint4 UnpackBoneIndices(uint Packed)
{
    return uint4(
        Packed & 0xFF,
        (Packed >> 8) & 0xFF,
        (Packed >> 16) & 0xFF,
        (Packed >> 24) & 0xFF);
}

static const uint FSkeletalSourceVertexStride = 84;
static const uint FSkeletalSourcePositionOffset = 0;
static const uint FSkeletalSourceColorOffset = 12;
static const uint FSkeletalSourceNormalOffset = 28;
static const uint FSkeletalSourceUVOffset = 40;
static const uint FSkeletalSourceTangentOffset = 48;
static const uint FSkeletalSourceBoneIndicesOffset = 64;
static const uint FSkeletalSourceBoneWeightsOffset = 68;

static const uint FDeformedVertexStride = 64;
static const uint FDeformedPositionOffset = 0;
static const uint FDeformedColorOffset = 12;
static const uint FDeformedNormalOffset = 28;
static const uint FDeformedUVOffset = 40;
static const uint FDeformedTangentOffset = 48;

FSkeletalSourceVertex LoadSkeletalSourceVertex(ByteAddressBuffer SourceBuffer, uint VertexIndex)
{
    const uint Base = VertexIndex * FSkeletalSourceVertexStride;

    FSkeletalSourceVertex Vertex;
    Vertex.Position = asfloat(SourceBuffer.Load3(Base + FSkeletalSourcePositionOffset));
    Vertex.Color = asfloat(SourceBuffer.Load4(Base + FSkeletalSourceColorOffset));
    Vertex.Normal = asfloat(SourceBuffer.Load3(Base + FSkeletalSourceNormalOffset));
    Vertex.UVs = asfloat(SourceBuffer.Load2(Base + FSkeletalSourceUVOffset));
    Vertex.Tangent = asfloat(SourceBuffer.Load4(Base + FSkeletalSourceTangentOffset));
    Vertex.BoneIndicesPacked = SourceBuffer.Load(Base + FSkeletalSourceBoneIndicesOffset);
    Vertex.BoneWeights = asfloat(SourceBuffer.Load4(Base + FSkeletalSourceBoneWeightsOffset));
    return Vertex;
}

void StoreDeformedVertex(RWByteAddressBuffer OutputBuffer, uint VertexIndex, FDeformedVertex Vertex)
{
    const uint Base = VertexIndex * FDeformedVertexStride;

    OutputBuffer.Store3(Base + FDeformedPositionOffset, asuint(Vertex.Position));
    OutputBuffer.Store4(Base + FDeformedColorOffset, asuint(Vertex.Color));
    OutputBuffer.Store3(Base + FDeformedNormalOffset, asuint(Vertex.Normal));
    OutputBuffer.Store2(Base + FDeformedUVOffset, asuint(Vertex.UVs));
    OutputBuffer.Store4(Base + FDeformedTangentOffset, asuint(Vertex.Tangent));
}
