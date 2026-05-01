#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

// main::FRenderer::RenderVSMBlurPass 기준 블러 셰이더 규약:
// - t0에는 아틀라스 전체를 가리키는 Texture2DArray SRV가 바인딩된다.
// - 출력 RTV는 이미 단일 array slice로 분리되어 있다.
// - 블러 상수에는 원본 rect, 원본 slice, 블러 방향이 들어와야 한다.
Texture2DArray BlurInputTexture : register(t0);

cbuffer GaussianBlurCB : register(b2)
{
    float4 SourceUVRect;
    float2 InvTextureSize;
    float2 BlurDirection;
    uint SourceSlice;
    float3 _pad;
}

static const float g_GaussianWeights[5] =
{
    0.2270270270f,
    0.1945945946f,
    0.1216216216f,
    0.0540540541f,
    0.0162162162f
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float2 ClampToSourceRect(float2 uv)
{
    float2 clampMin = SourceUVRect.xy + InvTextureSize * 0.5f;
    float2 clampMax = SourceUVRect.zw - InvTextureSize * 0.5f;
    return clamp(uv, clampMin, clampMax);
}

float2 GetAtlasUV(float2 pixelPos, uint width, uint height)
{
    float2 uv = (pixelPos + 0.5f) / float2((float) width, (float) height);
    return ClampToSourceRect(uv);
}

float2 SampleMoments(float2 atlasUV)
{
    return BlurInputTexture.SampleLevel(
        PointClampSampler,
        float3(atlasUV, (float) SourceSlice),
        0.0f).rg;
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    uint width;
    uint height;
    uint slices;
    BlurInputTexture.GetDimensions(width, height, slices);

    float2 pixelPos = input.position.xy;
    float2 baseUV = GetAtlasUV(pixelPos, width, height);
    float2 blurStep = BlurDirection * InvTextureSize;

    float2 blurredMoments = SampleMoments(baseUV) * g_GaussianWeights[0];

    [unroll]
    for (int i = 1; i < 5; ++i)
    {
        float2 offset = blurStep * (float) i;
        float2 uvPositive = ClampToSourceRect(baseUV + offset);
        float2 uvNegative = ClampToSourceRect(baseUV - offset);

        blurredMoments += SampleMoments(uvPositive) * g_GaussianWeights[i];
        blurredMoments += SampleMoments(uvNegative) * g_GaussianWeights[i];
    }

    return float4(blurredMoments, 0.0f, 0.0f);
}
