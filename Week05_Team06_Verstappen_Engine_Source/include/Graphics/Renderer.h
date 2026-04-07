#pragma once
#include <Graphics/RendererTypes.h>
#include <Graphics/HUD.h>
#include <Graphics/DebugLine.h> // 추가: DebugLine.h
#include <Core/AppTypes.h>
#include <DirectXMath.h>
#include <array>
#include <d3d11_1.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>
#include "Math/MathTypes.h"
#include "Scene/SceneData.h"

class UDebugRenderer;
namespace Scene { class USceneManager; }

namespace Graphics
{
    using Microsoft::WRL::ComPtr;

    class URenderer
    {
    public:
        struct FMeshVertex
        {
            DirectX::XMFLOAT3 Position;
            DirectX::XMFLOAT3 Normal;
            DirectX::XMFLOAT2 TexCoord;
        };

        struct FBVHNode
        {
            Math::FBox Bounds;
            uint32_t LeftChild = 0;
            uint32_t RightChild = 0;
            uint32_t TriangleIndex = 0;
            uint32_t TriangleCount = 0;

            bool IsLeaf() const { return TriangleCount > 0; }
        };

        struct FBVH
        {
            std::vector<FBVHNode> Nodes;
            std::vector<uint32_t> TriangleIndices; // Indices of triangles (first vertex in SourceIndices)
        };

        struct FBillboardVertex
        {
            DirectX::XMFLOAT3 Position;
            DirectX::XMFLOAT2 TexCoord;
        };

        struct FImpostorResource
        {
            ComPtr<ID3D11Texture2D> SnapshotTexture;
            ComPtr<ID3D11ShaderResourceView> SnapshotSRV;
            bool bIsBaked = false;
        };

        struct FMeshResource
        {
            std::vector<FMeshVertex> SourceVertices;
            std::vector<uint32_t> SourceIndices;
            ComPtr<ID3D11Buffer> VertexBuffer;
            ComPtr<ID3D11Buffer> IndexBuffer;
            ComPtr<ID3D11ShaderResourceView> DiffuseTextureView;
            std::wstring DiffuseTexturePath;
            uint32_t IndexCount = 0;
            uint32_t ObjectCount = 0;
            FBVH MeshBVH;

            Math::FBox LocalAABB;
            float LocalRadius = 0.0f;

            void BuildBVH();
            bool Raycast(const Math::FRay& LocalRay, float& OutT) const;
            DirectX::XMFLOAT3 LocalCenter = { 0, 0, 0 };
        };

        URenderer();
        ~URenderer();

        bool Initialize(HWND InWindowHandle, int InWidth, int InHeight);
        void Resize(int Width, int Height);
        void BeginFrame();
        void EndFrame();
        void RenderScene(const Scene::USceneManager& InSceneManager, float DeltaTime);
        void RenderHUD(); // 추가: HUD 렌더링 호출용
        void SetSceneViewportRect(int X, int Y, int Width, int Height);
        void BindMainRenderTarget();
        ID3D11ShaderResourceView* GetSceneViewportSRV() const { return SceneViewportSRV.Get(); }

        void SetCameraState(const FCameraState& InCameraState) { CameraState = InCameraState; }
        const FCameraState& GetCameraState() const { return CameraState; }

        void SetDebugRenderSettings(const FDebugRenderSettings& InSettings) { DebugSettings = InSettings; }
        const FDebugRenderSettings& GetDebugRenderSettings() const { return DebugSettings; }

        // 추가: 성능 데이터 전달용
        void UpdatePerformanceMetrics(const Core::FFramePerformanceMetrics& InMetrics) { CurrentMetrics = InMetrics; }

        ID3D11Device* GetDevice() { return Device.Get(); }
        ID3D11DeviceContext* GetContext() { return Context.Get(); }
        ID3D11DeviceContext1* GetContext1() { return Context1.Get(); }

        const FMeshResource* GetMeshResource(uint32_t MeshID) const;
        uint32_t GetOrLoadMesh(const std::wstring& FileName);

    private:
        bool CreateDefaultResources();
        bool LoadMesh(const std::wstring& InFilePath, uint32_t& OutBaseMeshID);
        bool EnsureSceneViewportResources(uint32_t Width, uint32_t Height);
        void DrawDebugBVH(const Scene::USceneManager& InSceneManager); // 추가: BVH AABB 디버그 드로우 함수
        void DrawDebugGrid(const Scene::USceneManager& InSceneManager); // 추가: Uniform Grid 디버그 드로우 함수
        void DrawDebugUniformGrid(const Scene::USceneManager& InSceneManager); // 추가: Uniform Grid 셀 디버그 드로우 함수
        void DrawDebugObjects(const Scene::USceneManager& InSceneManager); // 추가: 모든 객체의 AABB 및 Sphere 디버그 드로우 함수
        void DrawDebugSelected(const Scene::USceneManager& InSceneManager); // 추가: 선택된 객체 디버그 드로우

        void InitHiZResources(uint32_t Width, uint32_t Height);  // 추가
        void BuildHiZMips();                                      // 추가
        void RunOcclusionCull(Scene::FSceneDataSOA* SceneData,
            const DirectX::XMMATRIX& ViewProj,
                              std::array<float, Scene::FSceneDataSOA::MAX_OBJECTS>& OutIsVisiInOutConfidenceble,
                              float DeltaTime);

    private:
        static constexpr uint32_t BASE_MESH_TYPES = Scene::BASE_MESH_TYPE_COUNT;
        static constexpr uint32_t TOTAL_MESH_RESOURCE_COUNT = Scene::TOTAL_MESH_RESOURCE_COUNT;
        static constexpr uint32_t BILLBOARD_MESH_ID_OFFSET = Scene::BILLBOARD_MESH_ID_OFFSET;
        static constexpr uint32_t RENDER_BUCKET_COUNT = TOTAL_MESH_RESOURCE_COUNT + BASE_MESH_TYPES;

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        ComPtr<ID3D11DeviceContext1> Context1;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> MainRenderTargetView;
        ComPtr<ID3D11RenderTargetView> SceneRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> SceneViewportSRV;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;

        ComPtr<ID3D11VertexShader> VertexShader;
        ComPtr<ID3D11PixelShader> PixelShader;
        ComPtr<ID3D11InputLayout> InputLayout;
        ComPtr<ID3D11Buffer> PerFrameBuffer;
        ComPtr<ID3D11Buffer> PerObjectBuffer;
        ComPtr<ID3D11Buffer> MaterialBuffer;
        ComPtr<ID3D11SamplerState> DiffuseSamplerState;
        ComPtr<ID3D11ShaderResourceView> DefaultWhiteTextureView;

        ComPtr<ID3D11VertexShader> BillboardVS;
        ComPtr<ID3D11PixelShader> BillboardPS;
        ComPtr<ID3D11InputLayout> BillboardLayout;
        ComPtr<ID3D11Buffer> BillboardVB;
        ComPtr<ID3D11Buffer> BillboardIB;

        ComPtr<ID3D11RasterizerState> DefaultRasterizerState;
        ComPtr<ID3D11DepthStencilState> DefaultDepthStencilState;

        std::array<FMeshResource, TOTAL_MESH_RESOURCE_COUNT> MeshResources = {};
        std::array<FImpostorResource, BASE_MESH_TYPES> ImpostorResources = {};
        std::unordered_map<std::wstring, uint32_t> MeshPathMap;
        uint32_t NextBaseMeshID = 0;
        uint32_t ViewportWidth = 0;
        uint32_t ViewportHeight = 0;
        uint32_t SceneRenderTargetWidth = 0;
        uint32_t SceneRenderTargetHeight = 0;
        int SceneViewportX = 0;
        int SceneViewportY = 0;
        uint32_t SceneViewportWidth = 0;
        uint32_t SceneViewportHeight = 0;
        uint32_t PerObjectRingBufferOffset = 0;
        FCameraState CameraState = {};

        FDebugRenderSettings DebugSettings;
        Core::FFramePerformanceMetrics CurrentMetrics; // 추가: HUD에 전달할 데이터
        std::unique_ptr<FHUD> HUD; // 추가: HUD 객체

        std::unique_ptr<UDebugRenderer> DebugRenderer; // 추가: 디버그 렌더러

        ComPtr<ID3D11ShaderResourceView> DepthCopySRV;
        // Hi-Z Occlusion Culling
        ComPtr<ID3D11Texture2D>                          HiZTexture;
        ComPtr<ID3D11ShaderResourceView>                 HiZSRV;
        std::vector<ComPtr<ID3D11UnorderedAccessView>>   HiZMipUAVs;
        std::vector<ComPtr<ID3D11ShaderResourceView>>    HiZMipSRVs;

        ComPtr<ID3D11ComputeShader>                      CSBuildHiZ;
        ComPtr<ID3D11ComputeShader>                      CSTestOcclusion;

        ComPtr<ID3D11Buffer>                             BoundsBuffer;
        ComPtr<ID3D11ShaderResourceView>                 BoundsSRV;

        ComPtr<ID3D11Buffer>                             VisibilityBuffer;
        ComPtr<ID3D11UnorderedAccessView>                VisibilityUAV;
        ComPtr<ID3D11Buffer> VisibilityStagingBuffers[2];
        uint32_t             StagingReadIndex = 0;
        uint32_t             StagingWriteIndex = 1;
        bool                 bFirstFrame = true;

        ComPtr<ID3D11Buffer>                             CullParamBuffer;
        ComPtr<ID3D11Buffer>                             HiZBuildParamBuffer;
        ComPtr<ID3D11SamplerState>                       PointClampSamplerState;

        uint32_t HiZWidth = 0;
        uint32_t HiZHeight = 0;
        uint32_t HiZMipCount = 0;

        // 이전 프레임 visibility 저장용
        std::array<float, Scene::FSceneDataSOA::MAX_OBJECTS> VisibilityConfidence = {};
        uint32_t WarmupFramesRemaining = 3;

        bool bHasPrevFrame = false;
        ComPtr<ID3D11Buffer> BakePerFrameBuffer;
        ComPtr<ID3D11Buffer> BakePerObjectBuffer;
        ComPtr<ID3D11Buffer> BakeMatBuffer;

        float PrevCameraYaw = 0.0f;
        float PrevCameraPitch = 0.0f;

        void BakeImpostor(uint32_t MeshID);
    };
}
