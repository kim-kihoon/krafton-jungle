#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

// Particle Sprite — GPU 인스턴싱 빌보드.
//   slot 0: 정적 unit quad (POSITION=cornerSign ±0.5, TEXTCOORD=base uv) — 모든 입자 공유.
//   slot 1: per-instance (INSTANCE_*) — center/velocity/size/rotation/color/subimage/alignment.
//   VS가 ScreenAlignment에 따라 빌보드 축을 만들어 4정점으로 확장 (DrawIndexedInstanced(6, N)).
//   카메라 정보(View/Projection/CameraWorldPos)는 b0 FrameBuffer에서 직접 사용.

// t0: Material.CachedSRVs[Diffuse] (Atlas 텍스처)
Texture2D DiffuseTexture : register(t0);

// b2 (PerShader0): .mat의 Parameters 키에서 자동 매핑 (Mesh.hlsl과 동일 레이아웃)
cbuffer ParticleSpriteParams : register(b2)
{
    float4 BaseColor;    // 추가 tint
    float  Opacity;      // [0,1]
    float  UseTexture;   // 0=텍스처 무시(base color만), 1=텍스처 사용
    float  SubImagesH;   // atlas 가로 분할 (1 = SubUV 비활성)
    float  SubImagesV;   // atlas 세로 분할
}

// ScreenAlignment 상수 — C++ EParticleSpriteReplayAlignment 와 1:1 순서.
#define ALIGN_SQUARE     0
#define ALIGN_RECTANGLE  1
#define ALIGN_VELOCITY   2
#define ALIGN_FACING     3

struct VS_Input_SpriteParticle
{
    // ---- slot 0 (per-vertex) — unit quad ----
    float3 cornerSign : POSITION;   // (±0.5, ±0.5, 0)
    float2 baseUV     : TEXTCOORD;  // (0..1) tile 내부 UV
    // ---- slot 1 (per-instance) — INSTANCE_ prefix로 reflection이 slot 1에 자동 분리 ----
    float3 center     : INSTANCE_CENTER;
    float3 velocity   : INSTANCE_VELOCITY;
    float2 size       : INSTANCE_SIZE;     // (X=width, Y=height) world units
    float  rotation   : INSTANCE_ROTATION; // radians
    float4 instColor  : INSTANCE_COLOR;
    int    subImage   : INSTANCE_SUBIMAGE;
    int    alignment  : INSTANCE_ALIGNMENT;
};

struct PS_Input_Sprite
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 texcoord : TEXCOORD0;
    nointerpolation int subImage : TEXCOORD1; // per-instance — interpolation 안 함
};

PS_Input_Sprite VS(VS_Input_SpriteParticle input)
{
    // 입자 회전을 corner 부호(±0.5 평면)에 먼저 적용.
    float2 c  = input.cornerSign.xy;
    float  si = sin(input.rotation);
    float  co = cos(input.rotation);
    float2 rc = float2(c.x * co - c.y * si, c.x * si + c.y * co);

    // Square는 폭/높이를 Size.X로 통일, 나머지는 Size.X×Size.Y.
    float w = input.size.x;
    float h = (input.alignment == ALIGN_SQUARE) ? input.size.x : input.size.y;

    float4 clip;
    if (input.alignment == ALIGN_FACING)
    {
        // world-space point-facing: 각 입자가 카메라 위치를 정확히 바라본다.
        // 카메라 up(world) = View(row_major, row-vector 관례)의 col 1.
        float3 camUp   = float3(View._m01, View._m11, View._m21);
        float3 forward = normalize(CameraWorldPos - input.center);
        float3 right   = normalize(cross(camUp, forward));
        float3 up      = cross(forward, right);
        float3 worldCorner = input.center + (rc.x * w) * right + (rc.y * h) * up;
        clip = ApplyVP(worldCorner);
    }
    else
    {
        // view-space 빌보드: view 공간에서 x=화면 right, y=화면 up.
        float4 viewCenter = mul(float4(input.center, 1.0), View);
        float2 axisR = float2(1, 0);
        float2 axisU = float2(0, 1);
        if (input.alignment == ALIGN_VELOCITY)
        {
            // world velocity를 view 공간에 투영 → 화면상 진행 방향을 세로축으로 정렬.
            float3 viewVel = mul(float4(input.velocity, 0.0), View).xyz;
            float2 vdir = viewVel.xy;
            if (dot(vdir, vdir) > 1e-8)
            {
                axisU = normalize(vdir);
                axisR = float2(axisU.y, -axisU.x);
            }
        }
        viewCenter.xy += (rc.x * w) * axisR + (rc.y * h) * axisU;
        clip = mul(viewCenter, Projection);
    }

    PS_Input_Sprite output;
    output.position = clip;
    output.color    = input.instColor;
    output.texcoord = input.baseUV;
    output.subImage = input.subImage;
    return output;
}

float4 PS(PS_Input_Sprite input) : SV_TARGET
{
    // SubImage → atlas tile UV 변환 (Mesh.hlsl과 동일). H/V == 1이면 tile 1개 = 텍스처 전체.
    const int SubH = max(1, (int)SubImagesH);
    const int SubV = max(1, (int)SubImagesV);
    const float TileW = 1.0 / (float)SubH;
    const float TileH = 1.0 / (float)SubV;
    const int Col = input.subImage % SubH;
    const int Row = (input.subImage / SubH) % SubV;
    const float2 AtlasUV = float2(
        ((float)Col + input.texcoord.x) * TileW,
        ((float)Row + input.texcoord.y) * TileH
    );

    float4 sampled = DiffuseTexture.Sample(LinearClampSampler, AtlasUV);
    // UseTexture=0이면 texRgb=1, texA=1 — base color만 적용. =1이면 sample 그대로.
    float3 texRgb = lerp(float3(1,1,1), sampled.rgb, UseTexture);
    float  texA   = lerp(1.0,            sampled.a,   UseTexture);

    float3 rgb = input.color.rgb * BaseColor.rgb * texRgb;
    float  a   = input.color.a   * BaseColor.a   * Opacity * texA;
    return float4(ApplyWireframe(rgb), a);
}
