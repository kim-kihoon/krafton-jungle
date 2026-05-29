cbuffer ParticleParamBuffer : register(b2)
{
    float SubUVCols;
    float SubUVRows;
    float ScreenAlignment;
    float _Pad;
}


float2 RotateParticleCorner(float2 Corner, float Rotation)
{
    float S, C;
    sincos(Rotation, S, C);
    return float2(Corner.x * C - Corner.y * S,
                  Corner.x * S + Corner.y * C);
}