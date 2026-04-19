#pragma once
#include <Graphics/RendererTypes.h>
#include <cstdint>
#include <string>

namespace Scene
{
    class USceneManager;

    struct FObjMeshSummary
    {
        uint32_t VertexCount = 0;
        uint32_t TriangleCount = 0;
    };

    struct FAssetLoadOptions
    {
        bool bBakeBinary = true;
    };

    class FAssetLoader
    {
    public:
        static bool LoadAppleMid(USceneManager& InSceneManager, const FAssetLoadOptions& InOptions, FObjMeshSummary* OutSummary = nullptr);
        static bool LoadDefaultScene(USceneManager& InSceneManager, Graphics::FCameraState* OutCameraState = nullptr, const std::wstring& InScenePath = std::wstring());
    };
}
