#include <UI/Panels/SPropertyWindowPanel.h>
#include <Scene/SceneManager.h>
#include <UI/EditorSceneEditing.h>
#include <DirectXMath.h>
#include <algorithm>
#include <imgui.h>
#include <filesystem>
#include <vector>
#include <string>
#include <Core/PathManager.h>
#include <Graphics/Renderer.h>

namespace UI
{
    bool SPropertyWindowPanel::Initialize(const FEditorContext& InContext)
    {
        (void)InContext;
        return true;
    }

    void SPropertyWindowPanel::Update(const FEditorContext& InContext, float InDeltaTime)
    {
        (void)InContext;
        (void)InDeltaTime;
    }

    void SPropertyWindowPanel::Draw(const FEditorContext& InContext)
    {
        if (!ImGui::Begin(GetPanelName()))
        {
            ImGui::End();
            return;
        }

        if (!InContext.Dependencies || !InContext.Dependencies->SceneManager || !InContext.Dependencies->Renderer || !InContext.FrameData)
        {
            ImGui::TextUnformatted("Property window unavailable.");
            ImGui::End();
            return;
        }

        if (!InContext.FrameData->bHasSelection)
        {
            ImGui::TextUnformatted("No object selected.");
            ImGui::End();
            return;
        }

        DirectX::XMFLOAT3 Translation = {};
        DirectX::XMVECTOR RotationQuaternion = {};
        DirectX::XMFLOAT3 Scale = {};
        if (!GetObjectTransform(
            *InContext.Dependencies->SceneManager,
            InContext.FrameData->SelectedObjectIndex,
            Translation,
            RotationQuaternion,
            Scale))
        {
            ImGui::TextUnformatted("Failed to read object transform.");
            ImGui::End();
            return;
        }

        DirectX::XMFLOAT3 RotationDegrees = {};
        RotationDegrees = QuaternionToEulerDegrees(RotationQuaternion);

        bool bChanged = false;
        bChanged |= ImGui::DragFloat3("Location", &Translation.x, 0.1f);
        bChanged |= ImGui::DragFloat3("Rotation", &RotationDegrees.x, 0.5f);
        bChanged |= ImGui::DragFloat3("Scale", &Scale.x, 0.01f, 0.01f, 1000.0f);

        ImGui::Separator();
        ImGui::Text("Selected Objects: %u", InContext.FrameData->SelectedObjectCount);
        ImGui::Text("Object Index: %u", InContext.FrameData->SelectedObjectIndex);
        ImGui::Text("Mesh ID: %u", InContext.FrameData->SelectedMeshID);
        ImGui::Text("Material ID: %u", InContext.FrameData->SelectedMaterialID);
        if (ImGui::Button("Destroy Selected"))
        {
            if (InContext.Dependencies->SceneManager->DestroySelectedObjects())
            {
                if (InContext.ConsoleState)
                {
                    InContext.ConsoleState->PushMessage("Selected actor(s) destroyed.", EConsoleMessageSeverity::Info);
                }
            }
            else if (InContext.ConsoleState)
            {
                InContext.ConsoleState->PushMessage("Destroy failed.", EConsoleMessageSeverity::Warning);
            }

            ImGui::End();
            return;
        }

        if (bChanged)
        {
            ApplyObjectTransform(
                *InContext.Dependencies->SceneManager,
                *InContext.Dependencies->Renderer,
                InContext.FrameData->SelectedObjectIndex,
                Translation,
                EulerDegreesToQuaternion(RotationDegrees),
                Scale);
        }

        // [Task 3 & 4] 실시간 메쉬 교체 UI
        ImGui::Separator();
        ImGui::TextUnformatted("Mesh Settings");

        static std::vector<std::string> CachedMeshFiles;
        static float LastScanTime = -1.0f;
        float CurrentTime = (float)ImGui::GetTime();

        // 1초마다 디스크 스캔하여 프레임 드랍 방지 (Big-O: O(Files))
        if (LastScanTime < 0 || CurrentTime - LastScanTime > 1.0f)
        {
            CachedMeshFiles.clear();
            std::wstring WMeshPath = Core::FPathManager::GetMeshPath();
            
            if (std::filesystem::exists(WMeshPath))
            {
                for (const auto& Entry : std::filesystem::directory_iterator(WMeshPath))
                {
                    if (Entry.path().extension() == ".obj")
                    {
                        std::wstring WFilename = Entry.path().filename().wstring();
                        char Utf8Filename[512] = {};
                        WideCharToMultiByte(CP_UTF8, 0, WFilename.c_str(), -1, Utf8Filename, sizeof(Utf8Filename), NULL, NULL);
                        CachedMeshFiles.push_back(Utf8Filename);
                    }
                }
            }
            LastScanTime = CurrentTime;
        }

        uint32_t ObjIdx = InContext.FrameData->SelectedObjectIndex;
        Scene::FSceneDataSOA* SceneData = const_cast<Scene::FSceneDataSOA*>(InContext.Dependencies->SceneManager->GetSceneData());
        
        if (ImGui::BeginCombo("Change Mesh", "Select .obj File..."))
        {
            for (const auto& File : CachedMeshFiles)
            {
                if (ImGui::Selectable(File.c_str()))
                {
                    std::wstring WMeshPath = Core::FPathManager::GetMeshPath();
                    wchar_t WFilename[512] = {};
                    MultiByteToWideChar(CP_UTF8, 0, File.c_str(), -1, WFilename, 512);
                    std::wstring FullPath = WMeshPath + WFilename;

                    // Flyweight Registry를 통해 메쉬 로드 또는 획득
                    uint32_t NewBaseID = InContext.Dependencies->Renderer->GetOrLoadMesh(FullPath);


                    if (NewBaseID != 0xFFFFFFFF)
                    {
                        // SoA 구조체에 즉시 반영 (Data Locality 유지)
                        SceneData->BaseMeshIDs[ObjIdx] = NewBaseID;
                        SceneData->MeshIDs[ObjIdx] = NewBaseID; // 다음 프레임 LOD 계산 전 즉시 가시화
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::End();
    }

    EEditorPanelType SPropertyWindowPanel::GetPanelType() const
    {
        return EEditorPanelType::PropertyWindow;
    }

    const char* SPropertyWindowPanel::GetPanelName() const
    {
        return "Properties";
    }
}
