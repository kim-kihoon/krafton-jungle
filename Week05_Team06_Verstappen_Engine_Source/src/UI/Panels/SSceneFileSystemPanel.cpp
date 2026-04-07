#include <UI/Panels/SSceneFileSystemPanel.h>
#include <Scene/SceneManager.h>
#include <string>
#include <imgui.h>
#include <windows.h>

namespace UI
{
    bool SSceneFileSystemPanel::Initialize(const FEditorContext& InContext)
    {
        (void)InContext;
        return true;
    }

    void SSceneFileSystemPanel::Update(const FEditorContext& InContext, float InDeltaTime)
    {
        (void)InDeltaTime;
        ProcessFileRequests(InContext);
    }

    void SSceneFileSystemPanel::Draw(const FEditorContext& InContext)
    {
        if (!ImGui::Begin(GetPanelName()))
        {
            ImGui::End();
            return;
        }

        if (InContext.SceneFileSystemState)
        {
            char CurrentSavePath[UI::FSceneFileSystemState::PATH_BUFFER_LENGTH] = {};
            char CurrentLoadPath[UI::FSceneFileSystemState::PATH_BUFFER_LENGTH] = {};

            // 1. 엔진(UTF-16) -> UI(UTF-8) 변환
            WideCharToMultiByte(CP_UTF8, 0, InContext.SceneFileSystemState->SaveFilePath.data(), -1, CurrentSavePath, sizeof(CurrentSavePath), NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, InContext.SceneFileSystemState->LoadFilePath.data(), -1, CurrentLoadPath, sizeof(CurrentLoadPath), NULL, NULL);

            if (ImGui::Button("New Scene"))
            {
                InContext.SceneFileSystemState->bPendingResetScene = true;
            }

            ImGui::Separator();
            
            // 2. UI 입력 처리
            if (ImGui::InputText("Save Path", CurrentSavePath, IM_ARRAYSIZE(CurrentSavePath)))
            {
                // 입력 즉시 임시 버퍼를 거쳐 안전하게 반영
                wchar_t TempWide[UI::FSceneFileSystemState::PATH_BUFFER_LENGTH] = {};
                MultiByteToWideChar(CP_UTF8, 0, CurrentSavePath, -1, TempWide, UI::FSceneFileSystemState::PATH_BUFFER_LENGTH);
                
                // std::array에 안전하게 복사
                wcscpy_s(InContext.SceneFileSystemState->SaveFilePath.data(), InContext.SceneFileSystemState->SaveFilePath.size(), TempWide);
            }

            if (ImGui::Button("Save Scene"))
            {
                InContext.SceneFileSystemState->bPendingSave = true;
            }

            ImGui::Separator();

            if (ImGui::InputText("Load Path", CurrentLoadPath, IM_ARRAYSIZE(CurrentLoadPath)))
            {
                wchar_t TempWide[UI::FSceneFileSystemState::PATH_BUFFER_LENGTH] = {};
                MultiByteToWideChar(CP_UTF8, 0, CurrentLoadPath, -1, TempWide, UI::FSceneFileSystemState::PATH_BUFFER_LENGTH);
                
                wcscpy_s(InContext.SceneFileSystemState->LoadFilePath.data(), InContext.SceneFileSystemState->LoadFilePath.size(), TempWide);
            }

            if (ImGui::Button("Load Scene"))
            {
                InContext.SceneFileSystemState->bPendingLoad = true;
            }
        }

        if (InContext.FrameData)
        {
            ImGui::Separator();
            ImGui::Text("Objects: %u", InContext.FrameData->TotalObjectCount);
            ImGui::Text("Visible: %u", InContext.FrameData->VisibleObjectCount);
        }

        ImGui::End();
    }

    EEditorPanelType SSceneFileSystemPanel::GetPanelType() const
    {
        return EEditorPanelType::SceneFileSystem;
    }

    const char* SSceneFileSystemPanel::GetPanelName() const
    {
        return "Scene Files";
    }

    void SSceneFileSystemPanel::ProcessFileRequests(const FEditorContext& InContext) const
    {
        if (!InContext.Dependencies || !InContext.Dependencies->SceneManager || !InContext.SceneFileSystemState)
        {
            return;
        }

        Scene::USceneManager* SceneManager = InContext.Dependencies->SceneManager;
        FSceneFileSystemState& FileSystemState = *InContext.SceneFileSystemState;

        if (FileSystemState.bPendingResetScene)
        {
            SceneManager->ResetScene();
            FileSystemState.bPendingResetScene = false;

            if (InContext.ConsoleState)
            {
                InContext.ConsoleState->PushMessage("New scene created.", EConsoleMessageSeverity::Info);
            }
        }

        if (FileSystemState.bPendingSave)
        {
            const std::wstring SavePath = FileSystemState.SaveFilePath.data();
            const bool bSaved = SceneManager->SaveSceneBinary(SavePath, InContext.CameraState);
            FileSystemState.bPendingSave = false;

            if (InContext.ConsoleState)
            {
                InContext.ConsoleState->PushMessage(
                    bSaved ? "Scene save request succeeded." : "Scene save request failed.",
                    bSaved ? EConsoleMessageSeverity::Info : EConsoleMessageSeverity::Error);
            }
        }

        if (FileSystemState.bPendingLoad)
        {
            const std::wstring LoadPath = FileSystemState.LoadFilePath.data();
            const bool bLoaded = SceneManager->LoadSceneBinary(LoadPath, InContext.CameraState);
            FileSystemState.bPendingLoad = false;

            if (InContext.ConsoleState)
            {
                InContext.ConsoleState->PushMessage(
                    bLoaded ? "Scene load request succeeded." : "Scene load request failed.",
                    bLoaded ? EConsoleMessageSeverity::Info : EConsoleMessageSeverity::Error);
            }
        }
    }
}
