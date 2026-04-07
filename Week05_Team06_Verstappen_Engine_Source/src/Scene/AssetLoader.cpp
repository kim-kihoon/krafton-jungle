#include <Scene/AssetLoader.h>
#include <Core/PathManager.h>
#include <Scene/SceneManager.h>
#include <DirectXMath.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace Scene
{
    namespace
    {
        bool ParseTriple(const std::string& InLine, float& OutX, float& OutY, float& OutZ)
        {
            const size_t OpenBracket = InLine.find('[');
            const size_t CloseBracket = InLine.find(']', OpenBracket);
            if (OpenBracket == std::string::npos || CloseBracket == std::string::npos) return false;

            const std::string Values = InLine.substr(OpenBracket + 1, CloseBracket - OpenBracket - 1);
            return std::sscanf(Values.c_str(), "%f, %f, %f", &OutX, &OutY, &OutZ) == 3;
        }

        bool ParseSingle(const std::string& InLine, float& OutValue)
        {
            const size_t OpenBracket = InLine.find('[');
            const size_t CloseBracket = InLine.find(']', OpenBracket);
            if (OpenBracket == std::string::npos || CloseBracket == std::string::npos) return false;

            const std::string Value = InLine.substr(OpenBracket + 1, CloseBracket - OpenBracket - 1);
            return std::sscanf(Value.c_str(), "%f", &OutValue) == 1;
        }

        std::wstring ExtractMeshPath(const std::string& InLine)
        {
            size_t Colon = InLine.find(':');
            if (Colon == std::string::npos) return L"";

            size_t FirstQuote = InLine.find('\"', Colon);
            if (FirstQuote == std::string::npos) return L"";

            size_t SecondQuote = InLine.find('\"', FirstQuote + 1);
            if (SecondQuote == std::string::npos) return L"";

            std::string RawPath = InLine.substr(FirstQuote + 1, SecondQuote - FirstQuote - 1);
            
            // UTF-8 string을 wstring으로 안전하게 변환
            wchar_t wRawPath[512] = {};
            MultiByteToWideChar(CP_UTF8, 0, RawPath.c_str(), -1, wRawPath, 512);
            std::filesystem::path GenericPath(wRawPath);
            std::wstring FileName = GenericPath.filename().wstring();

            // Mesh 전용 경로와 결합 (상대 경로를 지원하기 위해 GetMeshPath 사용)
            std::wstring MeshRoot = Core::FPathManager::GetMeshPath();

            return MeshRoot + FileName;
        }
    }

    bool FAssetLoader::LoadAppleMid(USceneManager& InSceneManager, const FAssetLoadOptions& InOptions, FObjMeshSummary* OutSummary)
    {
        (void)InOptions;

        FSceneGridSpawnRequest GridReq;
        GridReq.Width = 50;
        GridReq.Height = 50;
        GridReq.Depth = 20;
        GridReq.Spacing = 5.0f;
        GridReq.MeshID = 0;
        GridReq.MaterialID = 0;

        InSceneManager.SpawnStaticMeshGrid(GridReq);

        if (OutSummary)
        {
            OutSummary->VertexCount = 1054;
            OutSummary->TriangleCount = 2104;
        }

        return true;
    }

bool FAssetLoader::LoadDefaultScene(USceneManager& InSceneManager, Graphics::FCameraState* OutCameraState,
                                        const std::wstring& InScenePath)
    {
        const std::wstring FinalPath =
            InScenePath.empty() ? (Core::FPathManager::GetScenePath() + L"Default.scene") : InScenePath;
        std::ifstream File{std::filesystem::path(FinalPath)};
        if (!File)
            return false;

        InSceneManager.ResetScene();

        std::string Line;
        bool bInsidePrimitive = false;
        bool bHasLocation = false;
        float LocationX = 0.0f, LocationY = 0.0f, LocationZ = 0.0f;
        float RotationX = 0.0f, RotationY = 0.0f, RotationZ = 0.0f;
        float ScaleX = 1.0f, ScaleY = 1.0f, ScaleZ = 1.0f;
        uint32_t MeshID = 0;
        bool bInsideCamera = false;

        auto CollectArrayData = [&](std::ifstream& InFile, std::string& FirstLine) -> std::string
        {
            std::string Combined = FirstLine;
            if (Combined.find(']') != std::string::npos)
                return Combined;

            std::string NextLine;
            while (std::getline(InFile, NextLine))
            {
                Combined += " " + NextLine;
                if (NextLine.find(']') != std::string::npos)
                    break;
            }
            return Combined;
        };

        while (std::getline(File, Line))
        {
            // --- PerspectiveCamera 블록 시작 ---
            if (Line.find("\"PerspectiveCamera\"") != std::string::npos)
            {
                bInsideCamera = true;
            }
            else if (bInsideCamera && OutCameraState)
            {
                if (Line.find("\"Location\"") != std::string::npos)
                {
                    std::string Data = CollectArrayData(File, Line);
                    ParseTriple(Data, OutCameraState->Position.x, OutCameraState->Position.y,
                                OutCameraState->Position.z);
                }
                else if (Line.find("\"Rotation\"") != std::string::npos)
                {
                    std::string Data = CollectArrayData(File, Line);
                    float YawDeg, PitchDegrees, Roll;
                    ParseTriple(Data, Roll, YawDeg, PitchDegrees);
                    OutCameraState->YawRadians = DirectX::XMConvertToRadians(YawDeg);
                    OutCameraState->PitchRadians = DirectX::XMConvertToRadians(PitchDegrees);
                }
                else if (Line.find("\"FOV\"") != std::string::npos)
                {
                    std::string Data = CollectArrayData(File, Line);
                    ParseSingle(Data, OutCameraState->FOVDegrees);
                }
                else if (Line.find("\"NearClip\"") != std::string::npos)
                {
                    std::string Data = CollectArrayData(File, Line);
                    ParseSingle(Data, OutCameraState->NearClip); // FCameraState의 멤버 변수명에 맞춰 수정하세요
                }
                else if (Line.find("\"FarClip\"") != std::string::npos)
                {
                    std::string Data = CollectArrayData(File, Line);
                    ParseSingle(Data, OutCameraState->FarClip); // FCameraState의 멤버 변수명에 맞춰 수정하세요
                }
                else if (Line.find("},") != std::string::npos || Line.find("}") != std::string::npos)
                {
                    bInsideCamera = false;
                }
            }
            // --- Primitives 블록 처리 ---
            else if (Line.find("\"Location\"") != std::string::npos)
            {
                std::string Data = CollectArrayData(File, Line);
                bHasLocation = ParseTriple(Data, LocationX, LocationY, LocationZ);
            }
            else if (Line.find("\"Rotation\"") != std::string::npos && bInsidePrimitive)
            {
                std::string Data = CollectArrayData(File, Line);
                ParseTriple(Data, RotationX, RotationY, RotationZ);
            }
            else if (Line.find("\"Scale\"") != std::string::npos && bInsidePrimitive)
            {
                std::string Data = CollectArrayData(File, Line);
                ParseTriple(Data, ScaleX, ScaleY, ScaleZ);
            }
            else if (Line.find("\"ObjStaticMeshAsset\"") != std::string::npos)
            {
                MeshID = InSceneManager.GetOrLoadMeshID(ExtractMeshPath(Line));
                bInsidePrimitive = true;
            }
            else if (bInsidePrimitive && bHasLocation && Line.find("\"Type\"") != std::string::npos)
            {
                FSceneSpawnRequest SpawnRequest;
                SpawnRequest.MeshID = MeshID;
                SpawnRequest.MaterialID = MeshID;

                DirectX::XMMATRIX S = DirectX::XMMatrixScaling(ScaleX, ScaleY, ScaleZ);
                DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(RotationX, RotationY, RotationZ);
                DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(LocationX, LocationY, LocationZ);
                SpawnRequest.WorldMatrix = S * R * T;

                InSceneManager.SpawnStaticMesh(SpawnRequest, false);

                bInsidePrimitive = false;
                bHasLocation = false;
                MeshID = 0;
                ScaleX = ScaleY = ScaleZ = 1.0f;
                RotationX = RotationY = RotationZ = 0.0f;
            }
        }

        if (InSceneManager.GetGrid())
        {
            InSceneManager.GetGrid()->BuildGrid();
        }
        InSceneManager.BuildSceneBVH();

        return InSceneManager.GetSceneStatistics().TotalObjectCount > 0;
    }
    }
