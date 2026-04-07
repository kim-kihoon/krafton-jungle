#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <Math/Frustum.h>
#include <Scene/SceneData.h>
#include <Core/AppTypes.h>

namespace Scene
{
    /**
     * [성능 극대화] 셀 내 객체 목록을 위한 선형 데이터 구조.
     */
    struct FGridCell
    {
        Math::FBox CellBox;
        uint32_t StartIndex; // GlobalIndexBuffer 내 시작 위치
        uint32_t Count;      // 이 셀에 포함된 객체 수
    };

    class UUniformGrid
    {
    private:
        int Width, Height, Depth;
        float CellSize;
        float OriginX;
        float OriginY;
        float OriginZ;
        float InvCellSize;
        std::vector<FGridCell> Cells;
        FSceneDataSOA* SceneData;

        /** [Zero-Alloc] 모든 셀의 인덱스를 통합 관리하는 거대 버퍼 */
        static constexpr uint32_t MAX_GRID_ENTRIES = FSceneDataSOA::MAX_OBJECTS * 8;
        std::array<uint32_t, MAX_GRID_ENTRIES> GlobalIndexBuffer;
        std::array<uint32_t, FSceneDataSOA::MAX_OBJECTS> VisitTokens = {};
        uint32_t CurrentVisitToken = 1;
        uint32_t TotalEntryCount = 0;

        void ReconfigureToSceneBounds();
        void FallbackToSingleCell();

    public:
        UUniformGrid(int InW, int InH, int InD, float InCellSize, FSceneDataSOA* InSceneData);

        /** [성능] 그리드 초기화 및 객체 재배치 준비 */
        void ClearGrid();

        void BuildGrid();

        // void InsertObject(uint32_t ObjectIndex);
        // void QueryFrustum(const Math::FFrustum& Frustum, uint32_t* OutIndices, uint32_t& OutCount, uint32_t MaxCapacity);

        const std::vector<FGridCell>& GetCells() const { return Cells; }

        template <typename NarrowPhaseFunc>
        bool Raycast(const Math::FRay& Ray, float MaxDistance, uint32_t& OutHitIndex, float& OutHitDistance, NarrowPhaseFunc NarrowPhaseTest);
        /** [최적화] 프러스텀 컬링, LOD 갱신, 렌더 큐 빌드를 한 번에 수행 */
        void CullingAndBuildRenderQueue(const Math::FFrustum& Frustum, const FLODSelectionContext& LODContext);
    };

    template <typename NarrowPhaseFunc>
    bool UUniformGrid::Raycast(const Math::FRay& Ray, float MaxDistance, uint32_t& OutHitIndex, float& OutHitDistance,
                               NarrowPhaseFunc NarrowPhaseTest)
    {
        if (!SceneData || Cells.empty())
            return false;
        CurrentVisitToken++;

        int GridX = std::clamp(static_cast<int>((Ray.Origin.x - OriginX) * InvCellSize), 0, Width - 1);
        int GridY = std::clamp(static_cast<int>((Ray.Origin.y - OriginY) * InvCellSize), 0, Height - 1);
        int GridZ = std::clamp(static_cast<int>((Ray.Origin.z - OriginZ) * InvCellSize), 0, Depth - 1);

        const int StepX = (Ray.Direction.x > 0.0f) ? 1 : -1;
        const int StepY = (Ray.Direction.y > 0.0f) ? 1 : -1;
        const int StepZ = (Ray.Direction.z > 0.0f) ? 1 : -1;

        const float tDeltaX = std::abs(CellSize * Ray.InvDirection.x);
        const float tDeltaY = std::abs(CellSize * Ray.InvDirection.y);
        const float tDeltaZ = std::abs(CellSize * Ray.InvDirection.z);

        float tMaxX = ((OriginX + (GridX + (StepX > 0 ? 1 : 0)) * CellSize) - Ray.Origin.x) * Ray.InvDirection.x;
        float tMaxY = ((OriginY + (GridY + (StepY > 0 ? 1 : 0)) * CellSize) - Ray.Origin.y) * Ray.InvDirection.y;
        float tMaxZ = ((OriginZ + (GridZ + (StepZ > 0 ? 1 : 0)) * CellSize) - Ray.Origin.z) * Ray.InvDirection.z;

        bool bHitFound = false;
        float ClosestHitT = MaxDistance;

        while (GridX >= 0 && GridX < Width && GridY >= 0 && GridY < Height && GridZ >= 0 && GridZ < Depth)
        {
            Core::GPerformanceMetrics.GridCellTestCount++;
            const FGridCell& Cell = Cells[GridX + (GridY * Width) + (GridZ * Width * Height)];

            if (Cell.Count > 0)
            {
                bool bHitInCell = false;

                float ClosestHitInCell = ClosestHitT;
                uint32_t BestIndexInCell = 0;

                const uint32_t* Indices = &GlobalIndexBuffer[Cell.StartIndex];
                for (uint32_t i = 0; i < Cell.Count; ++i)
                {
                    const uint32_t Idx = Indices[i];

                    if (VisitTokens[Idx] == CurrentVisitToken)
                        continue;
                    VisitTokens[Idx] = CurrentVisitToken;

                    Core::GPerformanceMetrics.GridObjectAABBTestCount++;

                    const float dx = SceneData->CenterX[Idx] - Ray.Origin.x;
                    const float dy = SceneData->CenterY[Idx] - Ray.Origin.y;
                    const float dz = SceneData->CenterZ[Idx] - Ray.Origin.z;

                    const float tca = (dx * Ray.Direction.x) + (dy * Ray.Direction.y) + (dz * Ray.Direction.z);

                    const float d2 = (dx * dx + dy * dy + dz * dz) - (tca * tca);
                    const float r2 = SceneData->Radius[Idx] * SceneData->Radius[Idx];

                    if (d2 > r2)
                        continue;

                    const float thc = std::sqrt(r2 - d2);
                    const float t0 = tca - thc;
                    const float t1 = tca + thc;

                    if (t1 < 0.0f)
                        continue;

                    const float tNear = (t0 < 0.0f) ? t1 : t0;

                    if (tNear < ClosestHitInCell)
                    {
                        float PreciseDistance = ClosestHitInCell;

                        if (NarrowPhaseTest(Idx, PreciseDistance) && PreciseDistance < ClosestHitInCell)
                        {
                            ClosestHitInCell = PreciseDistance;
                            BestIndexInCell = Idx;
                            bHitInCell = true;
                        }
                    }
                }
                if (bHitInCell)
                {
                    OutHitIndex = BestIndexInCell;
                    OutHitDistance = ClosestHitInCell;
                    return true;
                }
            }

            if (tMaxX < tMaxY)
            {
                if (tMaxX < tMaxZ)
                {
                    GridX += StepX;
                    tMaxX += tDeltaX;
                }
                else
                {
                    GridZ += StepZ;
                    tMaxZ += tDeltaZ;
                }
            }
            else
            {
                if (tMaxY < tMaxZ)
                {
                    GridY += StepY;
                    tMaxY += tDeltaY;
                }
                else
                {
                    GridZ += StepZ;
                    tMaxZ += tDeltaZ;
                }
            }
        }

        return false;
    }
    }
