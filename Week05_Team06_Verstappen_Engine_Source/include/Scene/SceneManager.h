#pragma once
#include <Math/MathTypes.h>
#include <Scene/SceneData.h>
#include <Scene/SceneTypes.h>
#include <Scene/UniformGrid.h>
#include <array>
#include <memory>
#include <string>
#include <Graphics/RendererTypes.h>

namespace Scene
{
using FMeshBoundsLookup = std::function<void(uint32_t MeshID, Math::FBox& OutAABB, DirectX::XMFLOAT3& OutCenter, float& OutRadius)>;
using FMeshLoaderLookup = std::function<uint32_t(const std::wstring& Path)>;
/**
     * Verstappen Engine의 씬 관리자.
     * 5만 개의 객체 데이터를 SOA 구조로 관리하며, 최적화된 데이터 접근 인터페이스를 제공함.
     */
    class USceneManager
    {
    public:
        USceneManager();
        ~USceneManager();

        void Initialize();
        void Update(float DeltaTime);

        /** 씬 초기화 */
        void ResetScene();

        /** [High-Level] 객체 생성 인터페이스 */
        bool SpawnStaticMesh(const FSceneSpawnRequest& InRequest, bool bRebuildGrid = true);
        void SpawnStaticMeshGrid(const FSceneGridSpawnRequest& InRequest);

        /** [Low-Level] 데이터 직접 주입 인터페이스 (성능 최적화용) */
        bool EnsureObjectCount(uint32_t InObjectCount);
        bool AddObject(const Math::FBox& InBounds, const Math::FMatrix& InWorldMatrix, uint32_t InMeshID = 0, uint32_t InMaterialID = 0);
        bool AddObjectPacked(const Math::FBox& InBounds, const Math::FPacked3x4Matrix& InWorldMatrix, uint32_t InMeshID = 0, uint32_t InMaterialID = 0);

        /** 파일 I/O */
        bool SaveSceneBinary(const std::wstring& InFilePath, const Graphics::FCameraState* InCameraState) const;
        bool LoadSceneBinary(const std::wstring& InFilePath, Graphics::FCameraState* OutCameraState);

        /** 객체 선택 및 관리 */
        bool SelectObject(uint32_t InObjectIndex, bool bAdditive = false);
        bool ToggleObjectSelection(uint32_t InObjectIndex);
        void ClearSelection();
        bool IsObjectSelected(uint32_t InObjectIndex) const;
        void RefreshSelectionMetadata();
        bool DestroySelectedObjects();
        bool IsValidIndex(uint32_t InIndex) const { return SceneData != nullptr && InIndex < SceneData->TotalObjectCount; }

        void MarkSpatialDataDirty() { bNeedsSpatialRebuild = true; }
        void FlushSpatialBuilds();

        /** Getters */
        FSceneDataSOA* GetSceneData() { return SceneData.get(); }
        const FSceneDataSOA* GetSceneData() const { return SceneData.get(); }
        UUniformGrid* GetGrid() { return Grid.get(); }
		const UUniformGrid* GetGrid() const { return Grid.get(); }
        FSceneBVH* GetSceneBVH() { return &SceneBVH; }
        const FSceneBVH* GetSceneBVH() const { return &SceneBVH; }

        FSceneStatistics GetSceneStatistics() const;
        const FSceneSelectionData& GetSelectionData() const { return SelectionData; }
        uint32_t GetVisibleObjectCount() const { return SceneData ? SceneData->RenderCount : 0; }

        uint32_t GetObjectCount() const { return SceneData ? SceneData->TotalObjectCount : 0; }
        void UpdateAllObjectBounds();
        void SetBoundsLookup(FMeshBoundsLookup InLookup) { BoundsLookup = InLookup; }
        void SetMeshLoader(FMeshLoaderLookup InLookup) { MeshLoader = InLookup; }
        uint32_t GetOrLoadMeshID(const std::wstring& Path) { return MeshLoader ? MeshLoader(Path) : 0; }
        static constexpr uint32_t GetMaxObjectCount() { return FSceneDataSOA::MAX_OBJECTS; }

        void BuildSceneBVH();
        void RebuildCentersAndRadii();
        Core::ESpatialStructure DetermineOptimalStructure() const;

    private:
        void ResetSelectionState();
        FMeshBoundsLookup BoundsLookup;
        FMeshLoaderLookup MeshLoader;
        
        std::unique_ptr<FSceneDataSOA> SceneData;
        std::unique_ptr<UUniformGrid> Grid;
        FSceneBVH SceneBVH;
        FSceneSelectionData SelectionData;
        bool bNeedsSpatialRebuild = false;
        std::array<uint8_t, FSceneDataSOA::MAX_OBJECTS> SelectionMask = {};
        uint32_t NextObjectID = 1;
    };
}
