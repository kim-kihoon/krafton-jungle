#include "Common/Functions.hlsli"
#include "Common/VertexLayouts.hlsli"
#include "Common/SystemSamplers.hlsli"
#include "Particle/ParticleCommon.hlsli"

Texture2D DiffuseTexture : register(t0);

// Rotate a 2D corner offset (radians, CCW).
float2 RotateParticleCorner(float2 Corner, float Rotation)
{
    float S, C;
    sincos(Rotation, S, C);
    return float2(Corner.x * C - Corner.y * S,
                  Corner.x * S + Corner.y * C);
}

float2 SafeNormalize2(float2 Value, float2 Fallback)
{
    float LenSq = dot(Value, Value);
    return (LenSq > 1e-6f) ? Value * rsqrt(LenSq) : Fallback;
}

float3 SafeNormalize3(float3 Value, float3 Fallback)
{
    float LenSq = dot(Value, Value);
    return (LenSq > 1e-6f) ? Value * rsqrt(LenSq) : Fallback;
}

float2 RightAxisFromUpAxis(float2 AxisY)
{
    return float2(AxisY.y, -AxisY.x);
}

float4 ProjectCameraPlaneBillboard(float4 ViewPos, float2 Corner, float2 Size, float Rotation)
{
    float2 ViewCorner = RotateParticleCorner(Corner, Rotation) * Size;
    ViewPos.xy += ViewCorner;
    return mul(ViewPos, Projection);
}

float4 ProjectViewAxisBillboard(float4 ViewPos, float2 Corner, float2 AxisY, float2 Size)
{
    float2 AxisX = RightAxisFromUpAxis(AxisY);
    ViewPos.xy += AxisX * (Corner.x * Size.x) + AxisY * (Corner.y * Size.y);
    return mul(ViewPos, Projection);
}

float4 ProjectCameraPositionBillboard(float3 WorldPosition, float2 Corner, float2 Size, float Rotation)
{
    float3 Forward = SafeNormalize3(CameraWorldPos - WorldPosition, float3(0.0f, 0.0f, 1.0f));
    float3 WorldUp = (abs(Forward.z) > 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
    float3 Right = SafeNormalize3(cross(Forward, WorldUp), float3(1.0f, 0.0f, 0.0f));
    float3 Up = cross(Right, Forward);
    float2 RotatedCorner = RotateParticleCorner(Corner, Rotation);
    float3 WorldOffset = Right * (RotatedCorner.x * Size.x) + Up * (RotatedCorner.y * Size.y);
    return mul(mul(float4(WorldPosition + WorldOffset, 1.0f), View), Projection);
}

float2 ComputeSubUVTexcoord(float2 LocalUV, float SubImage)
{
    uint Cols = max(SubUVCols, 1);
    uint Rows = max(SubUVRows, 1);

    if (Cols == 1 && Rows == 1)
    {
        return LocalUV;
    }

    uint FrameCount = Cols * Rows;
    uint FrameIndex = (uint)clamp(floor(SubImage), 0.0f, (float)(FrameCount - 1));
    
    uint FrameCol = FrameIndex % Cols;
    uint FrameRow = FrameIndex / Cols;
    
    float2 CellSize = 1.0f / float2((float)Cols, (float)Rows);
    float2 Offset = float2((float)FrameCol, (float)FrameRow) * CellSize;
    
    return LocalUV * CellSize + Offset;
}

PS_Input_Particle VS(VS_Input_ParticleSprite Input)
{
    PS_Input_Particle Out;

    float2 Corner = Input.uv * 2.0f - 1.0f;
    uint Alignment = GetParticleScreenAlignment();
    float4 ViewPos = mul(float4(Input.position, 1.0f), View);

    if (Alignment == PARTICLE_SCREEN_ALIGNMENT_SQUARE)
    {
        float UniformSize = Input.size.x;
        Out.position = ProjectCameraPlaneBillboard(ViewPos, Corner, float2(UniformSize, UniformSize), Input.rotation);
    }
    else if (Alignment == PARTICLE_SCREEN_ALIGNMENT_RECTANGLE)
    {
        Out.position = ProjectCameraPlaneBillboard(ViewPos, Corner, Input.size.xy, Input.rotation);
    }
    else if (Alignment == PARTICLE_SCREEN_ALIGNMENT_VELOCITY)
    {
        float2 ViewVelocity = mul(float4(Input.velocity, 0.0f), View).xy;
        float2 AxisY = SafeNormalize2(ViewVelocity, float2(0.0f, 1.0f));
        Out.position = ProjectViewAxisBillboard(ViewPos, Corner, AxisY, Input.size.xy);
    }
    else if (Alignment == PARTICLE_SCREEN_ALIGNMENT_AWAY_FROM_CENTER)
    {
        float2 ViewAwayFromCenter = mul(float4(Input.position - EmitterOrigin, 0.0f), View).xy;
        float2 AxisY = SafeNormalize2(ViewAwayFromCenter, float2(0.0f, 1.0f));
        Out.position = ProjectViewAxisBillboard(ViewPos, Corner, AxisY, Input.size.xy);
    }
    else if (Alignment == PARTICLE_SCREEN_ALIGNMENT_TYPE_SPECIFIC)
    {
        float3 WorldCorner = float3(Corner * Input.size.xy, 0.0f);
        float4 WorldPos = float4(Input.position + WorldCorner, 1.0f);
        Out.position = mul(mul(WorldPos, View), Projection);
    }
    else if (Alignment == PARTICLE_SCREEN_ALIGNMENT_FACING_CAMERA_POSITION)
    {
        Out.position = ProjectCameraPositionBillboard(Input.position, Corner, Input.size.xy, Input.rotation);
    }
    else
    {
        // Non-billboard fallback until type-specific sprite bases are provided.
        float3 WorldCorner = float3(Corner * Input.size.xy, 0.0f);
        float4 WorldPos = float4(Input.position + WorldCorner, 1.0f);
        Out.position = mul(mul(WorldPos, View), Projection);
    }

    // 셰이더 내부의 불안정한 랜덤 해시를 제거하고, 
    // C++(CPU)에서 이미 계산되어 넘어온 고정된 Input.subImage 값을 그대로 사용합니다.
    Out.texcoord = ComputeSubUVTexcoord(float2(Input.uv.x, 1.0f - Input.uv.y), Input.subImage);
    Out.color    = Input.color;
    return Out;
}

float4 PS(PS_Input_Particle Input) : SV_Target
{
    float4 Col = DiffuseTexture.Sample(LinearClampSampler, Input.texcoord);
    float LuminanceAlpha = max(max(Col.r, Col.g), Col.b);
    float AlphaBase = (AlphaSource == 1) ? LuminanceAlpha : Col.a;

    float AlphaRange = max(1.0f - AlphaThreshold, 0.0001f);
    float Alpha = saturate((AlphaBase - AlphaThreshold) / AlphaRange);
    Alpha = pow(Alpha, max(AlphaPower, 0.001f)) * Input.color.a;
    clip(Alpha - 0.01f);

    return float4(ApplyWireframe(Col.rgb * ColorIntensity) * Input.color.rgb,
                  bIsWireframe ? 1.0f : Alpha);
}
