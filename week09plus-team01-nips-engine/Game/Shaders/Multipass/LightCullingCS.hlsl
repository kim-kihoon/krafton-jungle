#define LIGHT_CULLING_TILE_SIZE 16
#define LIGHT_CULLING_MAX_LIGHTS_PER_TILE 512
#define THREADS_PER_TILE (LIGHT_CULLING_TILE_SIZE * LIGHT_CULLING_TILE_SIZE) // 256

struct FLightDataCS
{
    float3 WorldPos;
    float Radius;
    float3 Color;
    float Intensity;
    float RadiusFalloff;
    uint Type;
    float SpotInnerCos;
    float SpotOuterCos;
    float3 Direction;
    uint bCastShadows;
    int ShadowMapIndex;
    float ShadowBias;
    float Padding0;
    float Padding1;
};

cbuffer LightCullingConstants : register(b0)
{
    row_major float4x4 View;
    row_major float4x4 Projection;
    uint LightCount;
    uint TileCountX;
    uint TileCountY;
    uint TileSize;
    float ViewportWidth;
    float ViewportHeight;
    uint IsOrthographic;
    float Padding;
};

StructuredBuffer<FLightDataCS> SceneLights : register(t0);
RWStructuredBuffer<uint> TileVisibleLightCount : register(u0);
RWStructuredBuffer<uint> TileVisibleLightIndices : register(u1);

// GroupShared: 타일에서 살아남은 라이트 인덱스
groupshared uint gs_VisibleLightCount;
groupshared uint gs_VisibleLightIndices[LIGHT_CULLING_MAX_LIGHTS_PER_TILE];

// GroupShared: 배치 로드용 라이트 캐시
groupshared float4 gs_LightPosRadius[THREADS_PER_TILE]; // xyz=WorldPos, w=Radius

[numthreads(LIGHT_CULLING_TILE_SIZE, LIGHT_CULLING_TILE_SIZE, 1)]
void mainCS(
    uint3 GroupID : SV_GroupID,
    uint3 GroupThreadID : SV_GroupThreadID,
    uint GroupIndex : SV_GroupIndex)       // 0..255, flat thread index
{
    // -------------------------------------------------------
    // 0. 타일 메타데이터
    // -------------------------------------------------------
    const uint TileIndex = GroupID.y * TileCountX + GroupID.x;
    const uint TileStartOffset = TileIndex * LIGHT_CULLING_MAX_LIGHTS_PER_TILE;

    const float2 TileMin = float2(GroupID.xy) * float(TileSize);
    const float2 TileMax = TileMin + float(TileSize);

    // -------------------------------------------------------
    // 1. GroupShared 초기화 (thread 0만)
    // -------------------------------------------------------
    if (GroupIndex == 0)
    {
        gs_VisibleLightCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // -------------------------------------------------------
    // 2. 라이트를 THREADS_PER_TILE 배치로 나눠 협력 컬링
    //    각 스레드가 라이트 1개씩 로드 → 256개씩 처리
    // -------------------------------------------------------
    const uint BatchSize = THREADS_PER_TILE; // 256

    for (uint BatchStart = 0; BatchStart < LightCount; BatchStart += BatchSize)
    {
        // --- 2a. 협력 로드: 스레드i → 라이트(BatchStart+i) ---
        const uint LoadIndex = BatchStart + GroupIndex;
        if (LoadIndex < LightCount)
        {
            const FLightDataCS L = SceneLights[LoadIndex];
            gs_LightPosRadius[GroupIndex] = float4(L.WorldPos, L.Radius);
        }
        else
        {
            gs_LightPosRadius[GroupIndex] = float4(0, 0, 0, -1); // 더미 (Radius<0 → 컬링)
        }
        GroupMemoryBarrierWithGroupSync();

        // --- 2b. 배치 내 라이트를 모든 스레드가 나눠서 컬링 ---
        //  thread i → 라이트 i (배치 내)
        //  256스레드 * 1라이트 = 배치 전체 병렬 처리
        const uint LightInBatch = GroupIndex;
        if (LightInBatch < min(BatchSize, LightCount - BatchStart))
        {
            const float4 PosR = gs_LightPosRadius[LightInBatch];
            const float3 WorldPos = PosR.xyz;
            const float Radius = PosR.w;
            const uint GlobalIdx = BatchStart + LightInBatch;

            const float4 ViewPosition = mul(float4(WorldPos, 1.0f), View);
            const float EyeDepth = ViewPosition.x;
            
            if (EyeDepth + Radius <= 1e-4f)
            {
                // 카메라 뒤
                // → 컬링
            }
            // 카메라 구 내부
            else if (EyeDepth < Radius)
            {
                uint Slot;
                InterlockedAdd(gs_VisibleLightCount, 1, Slot);
                if (Slot < LIGHT_CULLING_MAX_LIGHTS_PER_TILE)
                    gs_VisibleLightIndices[Slot] = GlobalIdx;
            }
            else
            {
                const float4 ClipPosition = mul(ViewPosition, Projection);
                if (ClipPosition.w > 0.0f)
                {
                    const float2 Ndc = ClipPosition.xy / ClipPosition.w;
                    const float2 ScreenPos = float2(
                        (Ndc.x * 0.5f + 0.5f) * ViewportWidth,
                        (-Ndc.y * 0.5f + 0.5f) * ViewportHeight);

                    // Orthographic 일 때 Radius 크기 조정 필요 X
                    const float EffectiveDepth = IsOrthographic ? 1 : max(EyeDepth - Radius, 1e-4f);
                    const float YScale = Projection[2][1];
                    const float ProjectedRadius = (Radius / EffectiveDepth) * YScale * (ViewportHeight * 0.5f);
                    
                    if (ProjectedRadius > 0.0f)
                    {
                        const float2 Closest = clamp(ScreenPos, TileMin, TileMax);
                        const float2 Delta = ScreenPos - Closest;
                        const bool bIntersects = dot(Delta, Delta) <= (ProjectedRadius * ProjectedRadius);
                        
                        if (bIntersects)
                        {
                            uint Slot;
                            InterlockedAdd(gs_VisibleLightCount, 1, Slot);
                            if (Slot < LIGHT_CULLING_MAX_LIGHTS_PER_TILE)
                                gs_VisibleLightIndices[Slot] = GlobalIdx;
                        }
                    }
                }
            }
        }
        GroupMemoryBarrierWithGroupSync(); // 배치 완료 후 동기화
    }

    // -------------------------------------------------------
    // 3. 결과를 Global 버퍼에 기록
    //    256스레드가 협력해서 씀 (thread i → slot i)
    // -------------------------------------------------------
    GroupMemoryBarrierWithGroupSync();

    if (GroupIndex == 0)
    {
        TileVisibleLightCount[TileIndex] = min(gs_VisibleLightCount, LIGHT_CULLING_MAX_LIGHTS_PER_TILE);
    }

    const uint FinalCount = min(gs_VisibleLightCount, LIGHT_CULLING_MAX_LIGHTS_PER_TILE);
    for (uint WriteIdx = GroupIndex; WriteIdx < FinalCount; WriteIdx += BatchSize)
    {
        TileVisibleLightIndices[TileStartOffset + WriteIdx] = gs_VisibleLightIndices[WriteIdx];
    }
}
