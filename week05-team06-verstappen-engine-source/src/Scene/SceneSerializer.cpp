#include <Scene/SceneSerializer.h>
#include <Scene/SceneManager.h>
#include <Core/PathManager.h>
#include <fstream>

namespace Scene
{
    std::wstring FSceneSerializer::GetDefaultBinaryScenePath()
    {
        return Core::FPathManager::GetBinPath() + L"SceneMatrices.bin";
    }

    bool FSceneSerializer::SaveWorldMatrices(const USceneManager& InSceneManager, const std::wstring& InOutputPath)
    {
        const FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
        if (!SceneData) return false;

        // 인자가 비어있으면 기본 경로 사용
        std::wstring FinalPath = InOutputPath.empty() ? GetDefaultBinaryScenePath() : InOutputPath;
        
        std::ofstream File(FinalPath, std::ios::binary);
        if (!File) return false;

        const uint32_t Count = InSceneManager.GetSceneStatistics().TotalObjectCount;
        
        // 헤더 정보 기록
        FSceneBinaryHeader Header;
        Header.MatrixCount = Count;
        File.write(reinterpret_cast<const char*>(&Header), sizeof(Header));

        if (Count > 0)
        {
            File.write(reinterpret_cast<const char*>(SceneData->WorldMatrices.data()), sizeof(Math::FPacked3x4Matrix) * Count);
        }

        return File.good();
    }

    bool FSceneSerializer::LoadWorldMatrices(USceneManager& InSceneManager, const std::wstring& InInputPath)
    {
        std::wstring FinalPath = InInputPath.empty() ? GetDefaultBinaryScenePath() : InInputPath;
        
        std::ifstream File(FinalPath, std::ios::binary);
        if (!File) return false;

        FSceneBinaryHeader Header;
        File.read(reinterpret_cast<char*>(&Header), sizeof(Header));
        
        if (Header.Magic != FSceneBinaryHeader::MAGIC || Header.MatrixCount > FSceneDataSOA::MAX_OBJECTS)
        {
            return false;
        }

        const uint32_t Count = Header.MatrixCount;

        InSceneManager.ResetScene();
        if (!InSceneManager.EnsureObjectCount(Count)) return false;

        FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
        if (Count > 0 && SceneData)
        {
            File.read(reinterpret_cast<char*>(SceneData->WorldMatrices.data()), sizeof(Math::FPacked3x4Matrix) * Count);

            for (uint32_t Index = 0; Index < Count; ++Index)
            {
                SceneData->MeshIDs[Index] = Index % 2;
                SceneData->BaseMeshIDs[Index] = Index % 2;
                SceneData->MaterialIDs[Index] = 0;
                SceneData->LODLevels[Index] = static_cast<uint8_t>(ELODLevel::LOD0);
                SceneData->IsVisible[Index] = true;
                SceneData->ObjectIDs[Index] = Index; // 추가: ObjectID 세팅
            }

            InSceneManager.UpdateAllObjectBounds();

            if (UUniformGrid* Grid = InSceneManager.GetGrid())
            {
                Grid->BuildGrid();
            }
            // InSceneManager.BuildSceneBVH();
        }

        return File.good();
    }
}
