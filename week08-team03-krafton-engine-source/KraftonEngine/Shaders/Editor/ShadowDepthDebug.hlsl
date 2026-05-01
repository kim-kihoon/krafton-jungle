Texture2DArray gShadowAtlas : register(t0);
SamplerState gPointClampSampler : register(s0);

cbuffer DebugShadowCB : register(b2)
{
    float4 SrcUVRect;
    uint SrcSlice;
    uint bReversedZ;
    float Contrast;
    float Padding;
};

struct VSOut
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOut VS(uint VertexID : SV_VertexID)
{
    VSOut Out;

    float2 Pos[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    float2 UV[3] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f)
    };

    Out.Pos = float4(Pos[VertexID], 0.0f, 1.0f);
    Out.UV = UV[VertexID];
    return Out;
}

float4 PS(VSOut In) : SV_TARGET
{
    float2 AtlasUV = lerp(SrcUVRect.xy, SrcUVRect.zw, In.UV);
    float2 d = gShadowAtlas.SampleLevel(gPointClampSampler, float3(AtlasUV, SrcSlice), 0).rg;

    float2 v = (bReversedZ != 0)
        ? saturate(d * Contrast)
        : saturate((1.0f - d) * Contrast);
    
    return float4(v, 0.f, 1.0f);
}
