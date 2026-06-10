#ifndef NORMAL_MAPPING_HLSLI
#define NORMAL_MAPPING_HLSLI

float3 BuildOrthonormalTangent(float3 normal, float3 tangent)
{
    float3 T = normalize(tangent);
    return normalize(T - normal * dot(normal, T));
}

float3 ApplyTangentSpaceNormal(float3 normal, float3 tangent, float tangentSign, float3 tangentNormal)
{
    float3 N = normalize(normal);
    float3 T = BuildOrthonormalTangent(N, tangent);
    float3 B = normalize(cross(N, T) * tangentSign);
    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(tangentNormal, TBN));
}

float3 SampleTangentSpaceNormal(Texture2D normalTexture, SamplerState samplerState, float2 uv)
{
    return normalTexture.Sample(samplerState, uv).xyz * 2.0f - 1.0f;
}

float3 SampleTangentSpaceNormalLevel(Texture2D normalTexture, SamplerState samplerState, float2 uv, float mipLevel)
{
    return normalTexture.SampleLevel(samplerState, uv, mipLevel).xyz * 2.0f - 1.0f;
}

#endif // NORMAL_MAPPING_HLSLI
