#pragma once
#include <cstdint>
#include <string>

namespace Scene
{
    class USceneManager;

    struct FSceneBinaryHeader
    {
        static constexpr uint32_t MAGIC = 0x56454E47;
        static constexpr uint32_t VERSION = 1;

        uint32_t Magic = MAGIC;
        uint32_t Version = VERSION;
        uint32_t MatrixCount = 0;
    };

    class FSceneSerializer
    {
    public:
        static std::wstring GetDefaultBinaryScenePath();
        static bool SaveWorldMatrices(const USceneManager& InSceneManager, const std::wstring& InOutputPath = std::wstring());
        static bool LoadWorldMatrices(USceneManager& InSceneManager, const std::wstring& InInputPath = std::wstring());
    };
}
