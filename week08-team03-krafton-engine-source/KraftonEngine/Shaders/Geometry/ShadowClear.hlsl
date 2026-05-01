struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput VS(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PS(VSOutput input) : SV_TARGET
{
#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
    return float4(0.0, 0.0, 0.0, 0.0);
#else
    return float4(0.0, 0.0, 0.0, 0.0);
#endif
}
