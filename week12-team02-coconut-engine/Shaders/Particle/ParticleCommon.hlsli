#ifndef PARTICLE_COMMON_HLSLI
#define PARTICLE_COMMON_HLSLI

// b2 (PerShader0): common per-emitter particle parameters.
cbuffer ParticleParamBuffer : register(b2)
{
    uint SubUVCols;
    uint SubUVRows;
    uint ScreenAlignment;
    float _Pad;
    float3 EmitterOrigin;
    float _Pad1;
    uint AlphaSource;
    float AlphaThreshold;
    float AlphaPower;
    float ColorIntensity;
}

static const uint PARTICLE_SCREEN_ALIGNMENT_SQUARE = 0;
static const uint PARTICLE_SCREEN_ALIGNMENT_RECTANGLE = 1;
static const uint PARTICLE_SCREEN_ALIGNMENT_VELOCITY = 2;
static const uint PARTICLE_SCREEN_ALIGNMENT_AWAY_FROM_CENTER = 3;
static const uint PARTICLE_SCREEN_ALIGNMENT_TYPE_SPECIFIC = 4;
static const uint PARTICLE_SCREEN_ALIGNMENT_FACING_CAMERA_POSITION = 5;

uint GetParticleScreenAlignment()
{
    return ScreenAlignment;
}

// NOTE: Beam taper / width / sub-segment math is computed CPU-side now and
// baked into the per-vertex stream (VS_Input_BeamParticle). No b3 cbuffer.

#endif
