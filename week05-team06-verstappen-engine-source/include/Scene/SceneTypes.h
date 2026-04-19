#pragma once
#include <Math/MathTypes.h>
#include <cstdint>
#include <DirectXMath.h>

#include <vector>
#include <functional>

namespace Math { struct FFrustum; }

namespace Scene
{
    struct FSceneDataSOA;
    using namespace Math;

    typedef std::function<bool(uint32_t, float&)> NarrowPhaseFunc;

    struct FSceneBVHNode
    {
        Math::FBox Bounds;
        uint32_t LeftChild = 0;
        uint32_t RightChild = 0;
        uint32_t ObjectIndex = 0;
        uint32_t ObjectCount = 0;

        bool IsLeaf() const { return ObjectCount > 0; }
    };

    struct FSceneBVH
    {
        std::vector<FSceneBVHNode> Nodes;
        std::vector<uint32_t> ObjectIndices;

        const struct FSceneDataSOA* SceneData = nullptr;

        void Build(const struct FSceneDataSOA& SceneData);
        void QueryFrustum(const Math::FFrustum& Frustum, uint32_t* OutObjectIndices, uint32_t& OutCount, uint32_t MaxCount) const;
        bool Raycast(const Math::FRay& Ray, float MaxDistance, uint32_t& OutHitIndex, float& OutHitDistance, NarrowPhaseFunc NarrowPhaseTest) const;
    };

    enum class ELODLevel : uint8_t
    {
        LOD0 = 0,
        LOD1,
        LOD2,
        Billboard
    };

    static constexpr uint32_t BASE_MESH_TYPE_COUNT = 10;
    static constexpr uint32_t MESH_LOD_RESOURCE_COUNT = 3;
    static constexpr uint32_t LOD1_MESH_ID_OFFSET = BASE_MESH_TYPE_COUNT;
    static constexpr uint32_t LOD2_MESH_ID_OFFSET = BASE_MESH_TYPE_COUNT * 2;
    static constexpr uint32_t TOTAL_MESH_RESOURCE_COUNT = BASE_MESH_TYPE_COUNT * MESH_LOD_RESOURCE_COUNT;
    static constexpr uint32_t BILLBOARD_MESH_ID_OFFSET = 40;

    constexpr uint32_t EncodeRenderMeshID(uint32_t BaseMeshID, ELODLevel LODLevel)
    {
        switch (LODLevel)
        {
        case ELODLevel::LOD0: return BaseMeshID;
        case ELODLevel::LOD1: return LOD1_MESH_ID_OFFSET + BaseMeshID;
        case ELODLevel::LOD2: return LOD2_MESH_ID_OFFSET + BaseMeshID;
        case ELODLevel::Billboard: return BILLBOARD_MESH_ID_OFFSET + BaseMeshID;
        }

        return BaseMeshID;
    }

    struct FLODSelectionContext
    {
        DirectX::XMFLOAT3 CameraPosition = { 0.0f, 0.0f, 0.0f };
        float ViewportHeight = 1080.0f;
        float TanHalfFovY = 0.57735026f;
        float LOD0ProjectedSizePx = 30.0f;
        float LOD1ProjectedSizePx = 15.0f;
        float BillboardProjectedSizePx = 5.0f;
        float BillboardMinDistance = 45.0f;
        float HysteresisRatio = 0.15f;
        float TransitionJitterRatio = 0.30f;
    };

    /**
     * 단일 Static Mesh 생성 요청 데이터.
     */
    struct FSceneSpawnRequest
    {
        FMatrix WorldMatrix = DirectX::XMMatrixIdentity();
        uint32_t MeshID = 0;
        uint32_t MaterialID = 0;

        float LocalRadius;
        DirectX::XMFLOAT3 LocalCenter;
        Math::FBox LocalAABB = {};
    };

    /**
     * 규칙적인 3차원 배치를 생성하기 위한 요청 데이터.
     */
    struct FSceneGridSpawnRequest
    {
        uint32_t Width = 50;
        uint32_t Height = 50;
        uint32_t Depth = 20;
        float Spacing = 100.0f;
        uint32_t MeshID = 0;
        uint32_t MaterialID = 0;

        float LocalRadius = 0.5f;
        Math::FBox LocalAABB = {};
    };

    /**
     * 현재 선택된 오브젝트 정보를 저장하는 구조체.
     */
    struct FSceneSelectionData
    {
        bool bHasSelection = false;
        uint32_t ObjectIndex = 0;
        uint32_t MeshID = 0;
        uint32_t MaterialID = 0;
        uint32_t SelectionCount = 0;
        std::vector<uint32_t> SelectedObjectIndices;
    };

    /**
     * 씬 상태 요약 정보를 저장하는 구조체.
	 */
	struct FSceneStatistics
	{
		uint32_t TotalObjectCount = 0;
		uint32_t VisibleObjectCount = 0;
	};
}
