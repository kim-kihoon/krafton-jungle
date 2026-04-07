#pragma once
#include <cstdint>

namespace Core
{
    enum class ESpatialStructure : uint32_t
    {
        UniformGrid,
        SceneBVH
    };

    /**
     * 프레임 단위 성능 계측 결과를 저장하는 구조체.
     */
    struct FFramePerformanceMetrics
    {
        float DeltaTimeSeconds = 0.0f;
        float FramesPerSecond = 0.0f;
        float AverageFPS = 0.0f; // 추가된 멤버
        
        // Picking 관련 정밀 계측 (Cycles 단위)
        uint64_t LastPickingCycles = 0;
        uint64_t TotalPickingCycles = 0;
        uint64_t TotalPickCount = 0;

        // Grid 전용 Picking 통계
        uint64_t GridLastPickingCycles = 0;
        uint64_t GridTotalPickingCycles = 0;
        uint64_t GridTotalPickCount = 0;

        // BVH 전용 Picking 통계
        uint64_t BVHLastPickingCycles = 0;
        uint64_t BVHTotalPickingCycles = 0;
        uint64_t BVHTotalPickCount = 0;

        uint64_t FrameIndex = 0;

        // BVH 및 충돌 검사 통계 (매 프레임 리셋)
        uint32_t BVHNodeTestCount = 0;
        uint32_t ObjectAABBTestCount = 0;

        // Uniform Grid 통계
        uint32_t GridCellTestCount = 0;
        uint32_t GridObjectAABBTestCount = 0;

        ESpatialStructure CurrentStructure = ESpatialStructure::UniformGrid;
        uint64_t RenderedObjectCount = 0;

        // 렌더링 단계별 소요 시간 (ms)
        float SplitTime = 0.0f;
        float PrepassTime = 0.0f;
        float HiZTime = 0.0f;
        float CullTime = 0.0f;
        float DrawTime = 0.0f;

        // 가시성 및 드로우 콜 통계
        uint32_t DrawCount = 0;
        uint32_t PrevVisible = 0;
        uint32_t PrevInvisible = 0;
        uint32_t TotalObjectsCount = 0;
    };

    // 전역 성능 지표
    extern FFramePerformanceMetrics GPerformanceMetrics;
}
