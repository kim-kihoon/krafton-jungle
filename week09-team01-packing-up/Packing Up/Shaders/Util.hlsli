float4 ApplyMVP(float4 Position, float4x4 WorldMatrix, float4x4 ViewMatrix, float4x4 ProjMatrix)
{
    float4 world = mul(Position, WorldMatrix);
    float4 view = mul(world, ViewMatrix);
    
    return mul(view, ProjMatrix);
}

float4 ApplyMVP(float4 Position, float4x4 WorldMatrix, float4x4 VPMatrix)
{
    float4 world = mul(Position, WorldMatrix);

    return mul(world, VPMatrix);
}

float4 ApplyMVP(float4 Position, float4x4 MVP)
{
    return mul(Position, MVP);
}

// 가능하면 성능을 위해 역행렬 연산은 CPU에서 해서 넘기도록 한다.
float3x3 Inverse3x3(float3x3 m)
{
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
              - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
              + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

    float invDet = 1.0 / det;

    float3x3 result;
    result[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * invDet;
    result[0][1] = -(m[0][1] * m[2][2] - m[0][2] * m[2][1]) * invDet;
    result[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invDet;
    result[1][0] = -(m[1][0] * m[2][2] - m[1][2] * m[2][0]) * invDet;
    result[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invDet;
    result[1][2] = -(m[0][0] * m[1][2] - m[0][2] * m[1][0]) * invDet;
    result[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * invDet;
    result[2][1] = -(m[0][0] * m[2][1] - m[0][1] * m[2][0]) * invDet;
    result[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * invDet;
    return result;
}
