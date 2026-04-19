#include <UI/Panels/SControlPanel.h>
#include <Graphics/Renderer.h>
#include <Scene/SceneManager.h>
#include <Scene/SceneTypes.h>
#include <DirectXMath.h>
#include <imgui.h>

namespace UI
{
    bool SControlPanel::Initialize(const FEditorContext& InContext)
    {
        (void)InContext;
        return true;
    }

    void SControlPanel::Update(const FEditorContext& InContext, float InDeltaTime)
    {
        (void)InDeltaTime;
        ProcessSpawnRequests(InContext);
        SyncDebugRenderSettings(InContext);
    }

    void SControlPanel::Draw(const FEditorContext& InContext)
    {
        if (!ImGui::Begin(GetPanelName()))
        {
            ImGui::End();
            return;
        }

        if (InContext.SpawnSettings)
        {
            static const char* PrimitiveNames[] = {"Apple", "Bitten Apple"};
            int SelectedPrimitive = static_cast<int>(InContext.SpawnSettings->DefaultMeshID % 2u);
            if (ImGui::Combo("Primitive", &SelectedPrimitive, PrimitiveNames, IM_ARRAYSIZE(PrimitiveNames)))
            {
                InContext.SpawnSettings->DefaultMeshID = static_cast<uint32_t>(SelectedPrimitive);
                InContext.SpawnSettings->DefaultMaterialID = static_cast<uint32_t>(SelectedPrimitive);
            }

            ImGui::InputScalar("Spawn Count", ImGuiDataType_U32, &InContext.SpawnSettings->SingleSpawnCount);
            if (ImGui::Button("Spawn"))
            {
                InContext.SpawnSettings->bPendingSingleSpawn = true;
            }
        }

        if (InContext.DebugRenderSettings)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Scene Helpers");
            ImGui::Checkbox("Draw Grid Plane", &InContext.DebugRenderSettings->bDrawGrid);
            ImGui::Checkbox("Draw Axes", &InContext.DebugRenderSettings->bDrawWorldAxes);
            ImGui::Checkbox("Draw BVH Nodes", &InContext.DebugRenderSettings->bDrawBVH);
            ImGui::Checkbox("Draw Uniform Grid Cells", &InContext.DebugRenderSettings->bDrawUniformGrid);
            ImGui::DragFloat("Plane Half Extent", &InContext.DebugRenderSettings->GridPlaneHalfExtent, 1.0f, 1.0f, 10000.0f);
            ImGui::DragFloat("Plane Spacing", &InContext.DebugRenderSettings->GridPlaneSpacing, 0.1f, 0.1f, 1000.0f);
            InContext.DebugRenderSettings->GridPlaneHalfExtent = (std::max)(1.0f, InContext.DebugRenderSettings->GridPlaneHalfExtent);
            InContext.DebugRenderSettings->GridPlaneSpacing = (std::max)(0.1f, InContext.DebugRenderSettings->GridPlaneSpacing);
        }

        if (InContext.Dependencies && InContext.Dependencies->CameraState)
        {
            Graphics::FCameraState& CameraState = *InContext.Dependencies->CameraState;
            float YawDegrees = DirectX::XMConvertToDegrees(CameraState.YawRadians);
            float PitchDegrees = DirectX::XMConvertToDegrees(CameraState.PitchRadians);

            ImGui::Separator();
            ImGui::TextUnformatted("Camera");
            bool bCameraChanged = false;
            bCameraChanged |= ImGui::DragFloat3("Camera Position", &CameraState.Position.x, 0.1f);
            bCameraChanged |= ImGui::DragFloat("Camera Yaw", &YawDegrees, 0.1f, -360.0f, 360.0f);
            bCameraChanged |= ImGui::DragFloat("Camera Pitch", &PitchDegrees, 0.1f, -89.0f, 89.0f);
            bCameraChanged |= ImGui::DragFloat("FOV", &CameraState.FOVDegrees, 0.1f, 5.0f, 170.0f);
            bCameraChanged |= ImGui::DragFloat("Near Clip", &CameraState.NearClip, 0.01f, 0.01f, 100.0f);
            bCameraChanged |= ImGui::DragFloat("Far Clip", &CameraState.FarClip, 1.0f, 1.0f, 100000.0f);
            bCameraChanged |= ImGui::DragFloat("Move Speed", &CameraState.MoveSpeed, 0.1f, 0.1f, 1000.0f);
            bCameraChanged |= ImGui::DragFloat("Wheel Speed", &CameraState.WheelSpeed, 0.1f, 0.1f, 1000.0f);
            bCameraChanged |= ImGui::DragFloat("Look Sensitivity", &CameraState.LookSensitivity, 0.0001f, 0.0001f, 1.0f, "%.4f");

            if (bCameraChanged)
            {
                CameraState.PitchRadians = DirectX::XMConvertToRadians((std::clamp)(PitchDegrees, -89.0f, 89.0f));
                CameraState.YawRadians = DirectX::XMConvertToRadians(YawDegrees);
                CameraState.FOVDegrees = (std::clamp)(CameraState.FOVDegrees, 5.0f, 170.0f);
                CameraState.NearClip = (std::max)(0.01f, CameraState.NearClip);
                CameraState.FarClip = (std::max)(CameraState.NearClip + 0.1f, CameraState.FarClip);
                CameraState.MoveSpeed = (std::max)(0.1f, CameraState.MoveSpeed);
                CameraState.WheelSpeed = (std::max)(0.1f, CameraState.WheelSpeed);
                CameraState.LookSensitivity = (std::max)(0.0001f, CameraState.LookSensitivity);
                InContext.Dependencies->Renderer->SetCameraState(CameraState);
            }
        }

        ImGui::End();
    }

    EEditorPanelType SControlPanel::GetPanelType() const
    {
        return EEditorPanelType::ControlPanel;
    }

    const char* SControlPanel::GetPanelName() const
    {
        return "Control Panel";
    }

    void SControlPanel::ProcessSpawnRequests(const FEditorContext& InContext) const
    {
        if (!InContext.Dependencies || !InContext.Dependencies->SceneManager || !InContext.SpawnSettings)
        {
            return;
        }

        Scene::USceneManager* SceneManager = InContext.Dependencies->SceneManager;
        FEditorSpawnSettings& SpawnSettings = *InContext.SpawnSettings;

        if (SpawnSettings.bPendingSingleSpawn)
        {
            for (uint32_t SpawnIndex = 0; SpawnIndex < SpawnSettings.SingleSpawnCount; ++SpawnIndex)
            {
                Scene::FSceneSpawnRequest SpawnRequest;
                SpawnRequest.MeshID = SpawnSettings.DefaultMeshID;
                SpawnRequest.MaterialID = SpawnSettings.DefaultMaterialID;
                SpawnRequest.WorldMatrix = DirectX::XMMatrixIdentity();

                // 실제 메쉬 데이터로부터 바운딩 정보 가져오기
                if (const auto* MeshRes = InContext.Dependencies->Renderer->GetMeshResource(SpawnRequest.MeshID))
                {
                    SpawnRequest.LocalAABB = MeshRes->LocalAABB;
                    SpawnRequest.LocalRadius = MeshRes->LocalRadius;
                }

                SceneManager->SpawnStaticMesh(SpawnRequest);
            }

            SpawnSettings.bPendingSingleSpawn = false;
            if (InContext.ConsoleState)
            {
                InContext.ConsoleState->PushMessage("Single spawn request processed.", EConsoleMessageSeverity::Info);
            }
        }

        if (SpawnSettings.bPendingGridSpawn)
        {
            Scene::FSceneGridSpawnRequest GridRequest;
            GridRequest.Width = SpawnSettings.GridWidth;
            GridRequest.Height = SpawnSettings.GridHeight;
            GridRequest.Depth = SpawnSettings.GridDepth;
            GridRequest.Spacing = SpawnSettings.GridSpacing;
            GridRequest.MeshID = SpawnSettings.DefaultMeshID;
            GridRequest.MaterialID = SpawnSettings.DefaultMaterialID;

            // 실제 메쉬 데이터로부터 바운딩 정보 가져오기
            if (const auto* MeshRes = InContext.Dependencies->Renderer->GetMeshResource(GridRequest.MeshID))
            {
                GridRequest.LocalAABB = MeshRes->LocalAABB;
                GridRequest.LocalRadius = MeshRes->LocalRadius;
            }

            SceneManager->SpawnStaticMeshGrid(GridRequest);

            SpawnSettings.bPendingGridSpawn = false;
            if (InContext.ConsoleState)
            {
                InContext.ConsoleState->PushMessage("Grid spawn request processed.", EConsoleMessageSeverity::Info);
            }
        }
    }

    void SControlPanel::SyncDebugRenderSettings(const FEditorContext& InContext) const
    {
        if (!InContext.Dependencies || !InContext.Dependencies->Renderer || !InContext.DebugRenderSettings)
        {
            return;
        }

        InContext.Dependencies->Renderer->SetDebugRenderSettings(*InContext.DebugRenderSettings);
    }
}
