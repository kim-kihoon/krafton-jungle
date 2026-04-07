#pragma once
#include <Math/MathTypes.h>
#include <Scene/SceneTypes.h>
#include <DirectXMath.h>
#include <array>
#include <malloc.h>

namespace Scene
{
    using namespace Math;

    /**
     * [전략 반영] 20만 개의 사과를 위한 극한의 SIMD-SoA 구조체.
     * Culling과 Picking 루프에서 최상의 성능을 냅니다.
     */
    struct alignas(64) FSceneDataSOA
    {
        static constexpr uint32_t MAX_OBJECTS = 200000;

        // [SIMD Hot Path] AABB를 X, Y, Z 각각의 배열로 분리하여 8개씩 한꺼번에 로드 가능하게 함
        alignas(64) std::array<float, MAX_OBJECTS> MinX;
        alignas(64) std::array<float, MAX_OBJECTS> MinY;
        alignas(64) std::array<float, MAX_OBJECTS> MinZ;
        alignas(64) std::array<float, MAX_OBJECTS> MaxX;
        alignas(64) std::array<float, MAX_OBJECTS> MaxY;
        alignas(64) std::array<float, MAX_OBJECTS> MaxZ;

        // Bounding Sphere의 연산 효율을 사용하기 위한 x, y, z, radius 추가.
        alignas(64) std::array<float, MAX_OBJECTS> CenterX;
        alignas(64) std::array<float, MAX_OBJECTS> CenterY;
        alignas(64) std::array<float, MAX_OBJECTS> CenterZ;
        alignas(64) std::array<float, MAX_OBJECTS> Radius;

        // [Render Hot Path] 압축된 3x4 행렬 사용
        alignas(64) std::array<FPacked3x4Matrix, MAX_OBJECTS> WorldMatrices;
        alignas(64) std::array<DirectX::XMFLOAT4, MAX_OBJECTS> RotationQuaternions;
        alignas(64) std::array<DirectX::XMFLOAT3, MAX_OBJECTS> Scale3D;
        alignas(64) std::array<uint32_t, MAX_OBJECTS> ObjectIDs;

        // Metadata
        alignas(64) std::array<uint32_t, MAX_OBJECTS> MeshIDs;
        alignas(64) std::array<uint32_t, MAX_OBJECTS> BaseMeshIDs;
        alignas(64) std::array<uint32_t, MAX_OBJECTS> MaterialIDs;
        alignas(64) std::array<uint8_t, MAX_OBJECTS> LODLevels;
        alignas(64) std::array<bool, MAX_OBJECTS> IsVisible;

        // Render Queue
        alignas(64) std::array<uint32_t, MAX_OBJECTS> RenderQueue;
        uint32_t TotalObjectCount = 0;
        uint32_t RenderCount = 0;

        void* operator new(size_t size) { return _aligned_malloc(size, 64); }
        void operator delete(void* p) { _aligned_free(p); }

        FSceneDataSOA() : RenderCount(0)
        {
            LODLevels.fill(static_cast<uint8_t>(ELODLevel::LOD0));
            IsVisible.fill(false);
            for (uint32_t Index = 0; Index < MAX_OBJECTS; ++Index)
            {
                RotationQuaternions[Index] = {0.0f, 0.0f, 0.0f, 1.0f};
                Scale3D[Index] = {1.0f, 1.0f, 1.0f};
                ObjectIDs[Index] = Index;
            }
        }

        inline void ResetRenderQueue() { RenderCount = 0; }
        inline void AddToRenderQueue(uint32_t Index)
        {
            RenderQueue[RenderCount++] = Index;
        }
    };
}
