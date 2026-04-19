#include <Scene/UniformGrid.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <immintrin.h>

namespace Scene
{
    namespace
    {
        constexpr float MIN_CELL_SIZE = 2.0f;
        constexpr float MAX_CELL_SIZE = 8.0f;
        // constexpr float CELL_SIZE_MULTIPLIER = 2.0f;
        constexpr float CELL_SIZE_MULTIPLIER = 2.0f;
        constexpr float MIN_DISTANCE_EPSILON = 0.001f;

        // [AVX2 최적화] Gather 명령어를 사용하여 8개의 불연속적인 객체를 동시 검사
        inline uint32_t Culling8ObjectsAVX2(
            const float* BaseMinX, const float* BaseMinY, const float* BaseMinZ,
            const float* BaseMaxX, const float* BaseMaxY, const float* BaseMaxZ,
            const uint32_t* Indices, const Math::FFrustum& Frustum, uint32_t* OutIndices)
        {
            __m256i vIdx = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(Indices));

            __m256 vMinX = _mm256_i32gather_ps(BaseMinX, vIdx, 4);
            __m256 vMinY = _mm256_i32gather_ps(BaseMinY, vIdx, 4);
            __m256 vMinZ = _mm256_i32gather_ps(BaseMinZ, vIdx, 4);
            __m256 vMaxX = _mm256_i32gather_ps(BaseMaxX, vIdx, 4);
            __m256 vMaxY = _mm256_i32gather_ps(BaseMaxY, vIdx, 4);
            __m256 vMaxZ = _mm256_i32gather_ps(BaseMaxZ, vIdx, 4);

            uint32_t InsideMask = 0xFF;
            const __m256 vZero = _mm256_setzero_ps();
            const __m256 vEpsilon = _mm256_set1_ps(-0.01f);

            for (int i = 0; i < 6; ++i) {
                __m256 vPlaneX = _mm256_set1_ps(Frustum.SIMDPlanesX[i]);
                __m256 vPlaneY = _mm256_set1_ps(Frustum.SIMDPlanesY[i]);
                __m256 vPlaneZ = _mm256_set1_ps(Frustum.SIMDPlanesZ[i]);
                __m256 vPlaneW = _mm256_set1_ps(Frustum.SIMDPlanesW[i]);

                __m256 vPX = _mm256_blendv_ps(vMinX, vMaxX, _mm256_cmp_ps(vPlaneX, vZero, _CMP_GT_OQ));
                __m256 vPY = _mm256_blendv_ps(vMinY, vMaxY, _mm256_cmp_ps(vPlaneY, vZero, _CMP_GT_OQ));
                __m256 vPZ = _mm256_blendv_ps(vMinZ, vMaxZ, _mm256_cmp_ps(vPlaneZ, vZero, _CMP_GT_OQ));

                __m256 vDist = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(vPX, vPlaneX), _mm256_mul_ps(vPY, vPlaneY)), 
                                             _mm256_add_ps(_mm256_mul_ps(vPZ, vPlaneZ), vPlaneW));

                uint32_t OutsidePlane = _mm256_movemask_ps(_mm256_cmp_ps(vDist, vEpsilon, _CMP_LT_OQ));
                InsideMask &= ~OutsidePlane;
                if (InsideMask == 0) return 0;
            }

            uint32_t Count = 0;
            for (int i = 0; i < 8; ++i) {
                if (InsideMask & (1 << i)) OutIndices[Count++] = i;
            }
            return Count;
        }

        uint32_t ResolveRenderMeshID(uint32_t BaseMeshID, ELODLevel LODLevel)
        {
            return EncodeRenderMeshID(BaseMeshID, LODLevel);
        }

        uint32_t HashObjectID(uint32_t Value)
        {
            Value ^= Value >> 16;
            Value *= 0x7feb352dU;
            Value ^= Value >> 15;
            Value *= 0x846ca68bU;
            Value ^= Value >> 16;
            return Value;
        }

        float GetTransitionJitter(uint32_t ObjectID)
        {
            const uint32_t Hash = HashObjectID(ObjectID);
            const float Normalized = static_cast<float>(Hash) / static_cast<float>(UINT32_MAX);
            return (Normalized * 2.0f) - 1.0f;
        }

        FLODSelectionContext MakeJitteredLODContext(const FLODSelectionContext& BaseContext, uint32_t ObjectID)
        {
            FLODSelectionContext AdjustedContext = BaseContext;
            const float Jitter = GetTransitionJitter(ObjectID) * BaseContext.TransitionJitterRatio;
            const float SizeScale = (std::max)(0.55f, 1.0f + Jitter);
            const float DistanceScale = (std::max)(0.70f, 1.0f + (Jitter * 0.5f));

            AdjustedContext.LOD0ProjectedSizePx *= SizeScale;
            AdjustedContext.LOD1ProjectedSizePx *= SizeScale;
            AdjustedContext.BillboardProjectedSizePx *= SizeScale;
            AdjustedContext.BillboardMinDistance *= DistanceScale;
            return AdjustedContext;
        }

        ELODLevel SelectRawLOD(float Distance, float ProjectedSizePx, const FLODSelectionContext& Context)
        {
            if (ProjectedSizePx >= Context.LOD0ProjectedSizePx)
            {
                return ELODLevel::LOD0;
            }

            if (ProjectedSizePx >= Context.LOD1ProjectedSizePx)
            {
                return ELODLevel::LOD1;
            }

            if (Distance < Context.BillboardMinDistance ||
                ProjectedSizePx >= Context.BillboardProjectedSizePx)
            {
                return ELODLevel::LOD2;
            }

            return ELODLevel::Billboard;
        }

        ELODLevel ApplyHysteresis(
            ELODLevel PreviousLOD,
            float Distance,
            float ProjectedSizePx,
            const FLODSelectionContext& Context)
        {
            const float ScaleDown = 1.0f - Context.HysteresisRatio;
            const float ScaleUp = 1.0f + Context.HysteresisRatio;

            switch (PreviousLOD)
            {
            case ELODLevel::LOD0:
                if (ProjectedSizePx >= Context.LOD0ProjectedSizePx * ScaleDown)
                {
                    return ELODLevel::LOD0;
                }
                break;
            case ELODLevel::LOD1:
                if (ProjectedSizePx < Context.LOD0ProjectedSizePx * ScaleUp &&
                    ProjectedSizePx >= Context.LOD1ProjectedSizePx * ScaleDown)
                {
                    return ELODLevel::LOD1;
                }
                break;
            case ELODLevel::LOD2:
                if (ProjectedSizePx < Context.LOD1ProjectedSizePx * ScaleUp &&
                    (Distance < Context.BillboardMinDistance * ScaleUp ||
                        ProjectedSizePx >= Context.BillboardProjectedSizePx * ScaleDown))
                {
                    return ELODLevel::LOD2;
                }
                break;
            case ELODLevel::Billboard:
                if (Distance >= Context.BillboardMinDistance * ScaleDown &&
                    ProjectedSizePx < Context.BillboardProjectedSizePx * ScaleUp)
                {
                    return ELODLevel::Billboard;
                }
                break;
            }

            return SelectRawLOD(Distance, ProjectedSizePx, Context);
        }
    }

    UUniformGrid::UUniformGrid(int InW, int InH, int InD, float InCellSize, FSceneDataSOA* InSceneData)
        : Width(InW), Height(InH), Depth(InD), CellSize(InCellSize), SceneData(InSceneData)
        , OriginX(-(static_cast<float>(InW) * InCellSize) * 0.5f)
        , OriginY(-(static_cast<float>(InH) * InCellSize) * 0.5f)
        , OriginZ(0.0f)
        , TotalEntryCount(0)
    {
        InvCellSize = 1.0f / CellSize;
        Cells.resize(static_cast<size_t>(Width) * static_cast<size_t>(Height) * static_cast<size_t>(Depth));
        ClearGrid();
    }

    void UUniformGrid::ReconfigureToSceneBounds()
    {
        if (!SceneData || SceneData->TotalObjectCount == 0)
        {
            Width = Height = Depth = 1;
            CellSize = 4.0f;
            InvCellSize = 1.0f / CellSize;
            OriginX = OriginY = OriginZ = 0.0f;
            Cells.resize(1);
            return;
        }

        float SceneMinX = FLT_MAX;
        float SceneMinY = FLT_MAX;
        float SceneMinZ = FLT_MAX;
        float SceneMaxX = -FLT_MAX;
        float SceneMaxY = -FLT_MAX;
        float SceneMaxZ = -FLT_MAX;
        float LargestObjectExtent = 0.0f;

        for (uint32_t ObjectIndex = 0; ObjectIndex < SceneData->TotalObjectCount; ++ObjectIndex)
        {
            SceneMinX = (std::min)(SceneMinX, SceneData->MinX[ObjectIndex]);
            SceneMinY = (std::min)(SceneMinY, SceneData->MinY[ObjectIndex]);
            SceneMinZ = (std::min)(SceneMinZ, SceneData->MinZ[ObjectIndex]);
            SceneMaxX = (std::max)(SceneMaxX, SceneData->MaxX[ObjectIndex]);
            SceneMaxY = (std::max)(SceneMaxY, SceneData->MaxY[ObjectIndex]);
            SceneMaxZ = (std::max)(SceneMaxZ, SceneData->MaxZ[ObjectIndex]);

            const float ExtentX = SceneData->MaxX[ObjectIndex] - SceneData->MinX[ObjectIndex];
            const float ExtentY = SceneData->MaxY[ObjectIndex] - SceneData->MinY[ObjectIndex];
            const float ExtentZ = SceneData->MaxZ[ObjectIndex] - SceneData->MinZ[ObjectIndex];
            LargestObjectExtent = (std::max)(LargestObjectExtent, (std::max)(ExtentX, (std::max)(ExtentY, ExtentZ)));
        }

        CellSize = std::clamp(LargestObjectExtent * CELL_SIZE_MULTIPLIER, MIN_CELL_SIZE, MAX_CELL_SIZE);
        InvCellSize = 1.0f / CellSize;

        OriginX = SceneMinX - CellSize * 1.5f;
        OriginY = SceneMinY - CellSize * 1.5f;
        OriginZ = SceneMinZ - CellSize * 1.5f;

        Width = (std::max)(1, static_cast<int>(std::ceil((SceneMaxX - SceneMinX + CellSize * 3.0f) * InvCellSize)));
        Height = (std::max)(1, static_cast<int>(std::ceil((SceneMaxY - SceneMinY + CellSize * 3.0f) * InvCellSize)));
        Depth = (std::max)(1, static_cast<int>(std::ceil((SceneMaxZ - SceneMinZ + CellSize * 3.0f) * InvCellSize)));

        Cells.resize(static_cast<size_t>(Width) * static_cast<size_t>(Height) * static_cast<size_t>(Depth));
    }

    void UUniformGrid::FallbackToSingleCell()
    {
        if (!SceneData) return;

        float SceneMinX = FLT_MAX;
        float SceneMinY = FLT_MAX;
        float SceneMinZ = FLT_MAX;
        float SceneMaxX = -FLT_MAX;
        float SceneMaxY = -FLT_MAX;
        float SceneMaxZ = -FLT_MAX;

        for (uint32_t ObjectIndex = 0; ObjectIndex < SceneData->TotalObjectCount; ++ObjectIndex)
        {
            SceneMinX = (std::min)(SceneMinX, SceneData->MinX[ObjectIndex]);
            SceneMinY = (std::min)(SceneMinY, SceneData->MinY[ObjectIndex]);
            SceneMinZ = (std::min)(SceneMinZ, SceneData->MinZ[ObjectIndex]);
            SceneMaxX = (std::max)(SceneMaxX, SceneData->MaxX[ObjectIndex]);
            SceneMaxY = (std::max)(SceneMaxY, SceneData->MaxY[ObjectIndex]);
            SceneMaxZ = (std::max)(SceneMaxZ, SceneData->MaxZ[ObjectIndex]);
        }

        Width = Height = Depth = 1;
        CellSize = (std::max)({ SceneMaxX - SceneMinX, SceneMaxY - SceneMinY, SceneMaxZ - SceneMinZ, 1.0f }) + 1.0f;
        InvCellSize = 1.0f / CellSize;
        OriginX = SceneMinX - 0.5f;
        OriginY = SceneMinY - 0.5f;
        OriginZ = SceneMinZ - 0.5f;

        Cells.resize(1);
        ClearGrid();
        Cells[0].StartIndex = 0;
        Cells[0].Count = SceneData->TotalObjectCount;
        TotalEntryCount = SceneData->TotalObjectCount;

        for (uint32_t ObjectIndex = 0; ObjectIndex < SceneData->TotalObjectCount; ++ObjectIndex)
        {
            GlobalIndexBuffer[ObjectIndex] = ObjectIndex;
        }
    }

    void UUniformGrid::ClearGrid()
    {
        TotalEntryCount = 0;
        for (auto& Cell : Cells)
        {
            Cell.Count = 0;
            Cell.StartIndex = 0;
        }

        for (int z = 0; z < Depth; ++z)
        {
            for (int y = 0; y < Height; ++y)
            {
                for (int x = 0; x < Width; ++x)
                {
                    const int Index = x + (y * Width) + (z * Width * Height);
                    Cells[Index].CellBox.Min = { OriginX + (x * CellSize), OriginY + (y * CellSize), OriginZ + (z * CellSize) };
                    Cells[Index].CellBox.Max = { OriginX + ((x + 1) * CellSize), OriginY + ((y + 1) * CellSize), OriginZ + ((z + 1) * CellSize) };
                }
            }
        }
    }

    void UUniformGrid::BuildGrid()
    {
        if (!SceneData) return;

        ReconfigureToSceneBounds();
        ClearGrid();

        for (uint32_t ObjectIndex = 0; ObjectIndex < SceneData->TotalObjectCount; ++ObjectIndex)
        {
            const int MinGridX = std::clamp(static_cast<int>((SceneData->MinX[ObjectIndex] - OriginX) * InvCellSize), 0, Width - 1);
            const int MinGridY = std::clamp(static_cast<int>((SceneData->MinY[ObjectIndex] - OriginY) * InvCellSize), 0, Height - 1);
            const int MinGridZ = std::clamp(static_cast<int>((SceneData->MinZ[ObjectIndex] - OriginZ) * InvCellSize), 0, Depth - 1);
            const int MaxGridX = std::clamp(static_cast<int>((SceneData->MaxX[ObjectIndex] - OriginX) * InvCellSize), 0, Width - 1);
            const int MaxGridY = std::clamp(static_cast<int>((SceneData->MaxY[ObjectIndex] - OriginY) * InvCellSize), 0, Height - 1);
            const int MaxGridZ = std::clamp(static_cast<int>((SceneData->MaxZ[ObjectIndex] - OriginZ) * InvCellSize), 0, Depth - 1);

            for (int z = MinGridZ; z <= MaxGridZ; ++z)
            {
                for (int y = MinGridY; y <= MaxGridY; ++y)
                {
                    for (int x = MinGridX; x <= MaxGridX; ++x)
                    {
                        const int CellIndex = x + (y * Width) + (z * Width * Height);
                        ++Cells[CellIndex].Count;
                    }
                }
            }
        }

        TotalEntryCount = 0;
        for (auto& Cell : Cells)
        {
            Cell.StartIndex = TotalEntryCount;
            TotalEntryCount += Cell.Count;
            Cell.Count = 0;
        }

        if (TotalEntryCount > MAX_GRID_ENTRIES)
        {
            FallbackToSingleCell();
            return;
        }

        for (uint32_t ObjectIndex = 0; ObjectIndex < SceneData->TotalObjectCount; ++ObjectIndex)
        {
            const int MinGridX = std::clamp(static_cast<int>((SceneData->MinX[ObjectIndex] - OriginX) * InvCellSize), 0, Width - 1);
            const int MinGridY = std::clamp(static_cast<int>((SceneData->MinY[ObjectIndex] - OriginY) * InvCellSize), 0, Height - 1);
            const int MinGridZ = std::clamp(static_cast<int>((SceneData->MinZ[ObjectIndex] - OriginZ) * InvCellSize), 0, Depth - 1);
            const int MaxGridX = std::clamp(static_cast<int>((SceneData->MaxX[ObjectIndex] - OriginX) * InvCellSize), 0, Width - 1);
            const int MaxGridY = std::clamp(static_cast<int>((SceneData->MaxY[ObjectIndex] - OriginY) * InvCellSize), 0, Height - 1);
            const int MaxGridZ = std::clamp(static_cast<int>((SceneData->MaxZ[ObjectIndex] - OriginZ) * InvCellSize), 0, Depth - 1);

            for (int z = MinGridZ; z <= MaxGridZ; ++z)
            {
                for (int y = MinGridY; y <= MaxGridY; ++y)
                {
                    for (int x = MinGridX; x <= MaxGridX; ++x)
                    {
                        const int CellIndex = x + (y * Width) + (z * Width * Height);
                        FGridCell& Cell = Cells[CellIndex];
                        GlobalIndexBuffer[Cell.StartIndex + Cell.Count] = ObjectIndex;
                        ++Cell.Count;
                    }
                }
            }
        }
    }

    /*void UUniformGrid::QueryFrustum(const Math::FFrustum& Frustum, uint32_t* OutIndices, uint32_t& OutCount, uint32_t MaxCapacity)
    {
        OutCount = 0;
        if (!SceneData || !OutIndices || MaxCapacity == 0 || Cells.empty()) return;

        ++CurrentVisitToken;
        if (CurrentVisitToken == 0)
        {
            VisitTokens.fill(0);
            CurrentVisitToken = 1;
        }

        const int MinGridX = std::clamp(static_cast<int>((Frustum.AABBMin.x - OriginX) * InvCellSize), 0, Width - 1);
        const int MinGridY = std::clamp(static_cast<int>((Frustum.AABBMin.y - OriginY) * InvCellSize), 0, Height - 1);
        const int MinGridZ = std::clamp(static_cast<int>((Frustum.AABBMin.z - OriginZ) * InvCellSize), 0, Depth - 1);
        const int MaxGridX = std::clamp(static_cast<int>((Frustum.AABBMax.x - OriginX) * InvCellSize), 0, Width - 1);
        const int MaxGridY = std::clamp(static_cast<int>((Frustum.AABBMax.y - OriginY) * InvCellSize), 0, Height - 1);
        const int MaxGridZ = std::clamp(static_cast<int>((Frustum.AABBMax.z - OriginZ) * InvCellSize), 0, Depth - 1);

        for (int z = MinGridZ; z <= MaxGridZ; ++z)
        {
            for (int y = MinGridY; y <= MaxGridY; ++y)
            {
                for (int x = MinGridX; x <= MaxGridX; ++x)
                {
                    const FGridCell& Cell = Cells[x + (y * Width) + (z * Width * Height)];
                    if (Cell.Count == 0) continue;

                    const Math::ECullingResult CellResult = Frustum.TestBox(Cell.CellBox);
                    if (CellResult == Math::ECullingResult::Outside) continue;

                    const uint32_t* Indices = &GlobalIndexBuffer[Cell.StartIndex];

                    if (CellResult == Math::ECullingResult::FullyInside)
                    {
                        for (uint32_t i = 0; i < Cell.Count; ++i)
                        {
                            const uint32_t Index = Indices[i];
                            if (VisitTokens[Index] == CurrentVisitToken) continue;

                            VisitTokens[Index] = CurrentVisitToken;
                            if (OutCount < MaxCapacity)
                            {
                                OutIndices[OutCount++] = Index;
                            }
                        }
                    }
                    else
                    {
                        for (uint32_t i = 0; i < Cell.Count; ++i)
                        {
                            const uint32_t Index = Indices[i];
                            if (VisitTokens[Index] == CurrentVisitToken) continue;

                            if (Frustum.TestSphere(
                                SceneData->CenterX[Index],
                                SceneData->CenterY[Index],
                                SceneData->CenterZ[Index],
                                SceneData->Radius[Index]) == Math::ECullingResult::Outside)
                            {
                                continue;
                            }

                            VisitTokens[Index] = CurrentVisitToken;
                            if (OutCount < MaxCapacity)
                            {
                                OutIndices[OutCount++] = Index;
                            }
                        }
                    }
                }
            }
        }
    }*/

    void UUniformGrid::CullingAndBuildRenderQueue(const Math::FFrustum& Frustum, const FLODSelectionContext& LODContext)
    {
        if (!SceneData)
            return;
        SceneData->ResetRenderQueue();
        SceneData->IsVisible.fill(false);

        const DirectX::XMFLOAT3& cam = LODContext.CameraPosition;
        const float tanHalfFovY = (std::max)(LODContext.TanHalfFovY, 0.001f);

        const int minGX = std::clamp(static_cast<int>((Frustum.AABBMin.x - OriginX) * InvCellSize) - 1, 0, Width - 1);
        const int minGY = std::clamp(static_cast<int>((Frustum.AABBMin.y - OriginY) * InvCellSize) - 1, 0, Height - 1);
        const int minGZ = std::clamp(static_cast<int>((Frustum.AABBMin.z - OriginZ) * InvCellSize) - 1, 0, Depth - 1);
        const int maxGX = std::clamp(static_cast<int>((Frustum.AABBMax.x - OriginX) * InvCellSize) + 1, 0, Width - 1);
        const int maxGY = std::clamp(static_cast<int>((Frustum.AABBMax.y - OriginY) * InvCellSize) + 1, 0, Height - 1);
        const int maxGZ = std::clamp(static_cast<int>((Frustum.AABBMax.z - OriginZ) * InvCellSize) + 1, 0, Depth - 1);

        ++CurrentVisitToken;
        if (CurrentVisitToken == 0)
        {
            VisitTokens.fill(0);
            CurrentVisitToken = 1;
        }

        auto ProcessObject = [&](uint32_t oid)
        {
            if (VisitTokens[oid] == CurrentVisitToken)
                return;
            VisitTokens[oid] = CurrentVisitToken;

            const float dx = SceneData->CenterX[oid] - cam.x;
            const float dy = SceneData->CenterY[oid] - cam.y;
            const float dz = SceneData->CenterZ[oid] - cam.z;
            const float radius = SceneData->Radius[oid];

            const float distanceSq = (dx * dx) + (dy * dy) + (dz * dz);
            const float distance = (std::max)(std::sqrt(distanceSq), MIN_DISTANCE_EPSILON);

            const float projectedSizePx = (radius * LODContext.ViewportHeight) / (distance * tanHalfFovY);
            const FLODSelectionContext adjustedLODContext = MakeJitteredLODContext(LODContext, oid);

            const ELODLevel previousLOD = static_cast<ELODLevel>(SceneData->LODLevels[oid]);
            const ELODLevel selectedLOD = ApplyHysteresis(previousLOD, distance, projectedSizePx, adjustedLODContext);
            const uint32_t baseID = SceneData->BaseMeshIDs[oid];

            SceneData->LODLevels[oid] = static_cast<uint8_t>(selectedLOD);
            SceneData->MeshIDs[oid] = ResolveRenderMeshID(baseID, selectedLOD);
            SceneData->IsVisible[oid] = true;
            SceneData->AddToRenderQueue(oid);
        };

        for (int z = minGZ; z <= maxGZ; ++z)
        {
            for (int y = minGY; y <= maxGY; ++y)
            {
                for (int x = minGX; x <= maxGX; ++x)
                {
                    const FGridCell& Cell = Cells[x + (y * Width) + (z * Width * Height)];
                    if (Cell.Count == 0)
                        continue;

                    const Math::ECullingResult cres = Frustum.TestBox(Cell.CellBox);
                    if (cres == Math::ECullingResult::Outside)
                        continue;

                    const uint32_t* Indices = &GlobalIndexBuffer[Cell.StartIndex];
                    uint32_t i = 0;

                    if (cres == Math::ECullingResult::FullyInside)
                    {
                        for (; i < Cell.Count; ++i)
                        {
                            ProcessObject(Indices[i]);
                        }
                    }
                    else
                    {
                        for (; i + 8 <= Cell.Count; i += 8)
                        {
                            uint32_t poff[8];
                            uint32_t pcnt = Culling8ObjectsAVX2(SceneData->MinX.data(), SceneData->MinY.data(),
                                                                SceneData->MinZ.data(), SceneData->MaxX.data(),
                                                                SceneData->MaxY.data(), SceneData->MaxZ.data(),
                                                                &Indices[i], Frustum, poff);
                            for (uint32_t j = 0; j < pcnt; ++j)
                            {
                                ProcessObject(Indices[i + poff[j]]);
                            }
                                
                        }
                        for (; i < Cell.Count; ++i)
                        {
                            if (Frustum.TestBox(SceneData->MinX[Indices[i]], SceneData->MinY[Indices[i]],
                                                SceneData->MinZ[Indices[i]], SceneData->MaxX[Indices[i]],
                                                SceneData->MaxY[Indices[i]],
                                                SceneData->MaxZ[Indices[i]]) != Math::ECullingResult::Outside)
                                ProcessObject(Indices[i]);
                        }
                    }
                }
            }
        }
    }
    }
