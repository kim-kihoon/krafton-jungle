#include "Editor/Viewer/AnimStateMachineGraphViewer.h"

#include "Animation/AnimStateMachineAsset.h"
#include "Core/Logging/Log.h"
#include "Core/ResourceManager.h"
#include "Editor/Selection/SelectionManager.h"
#include "GameFramework/World.h"
#include "imgui-node-editor/imgui_node_editor.h"

#include <cmath>

namespace ed = ax::NodeEditor;

FAnimStateMachineGraphViewer::~FAnimStateMachineGraphViewer()
{
    Shutdown();
}

void FAnimStateMachineGraphViewer::Init(
    FWindowsWindow* InWindow,
    UEditorEngine* InEditor,
    UWorld* InWorld,
    FSelectionManager* InSelectionManager)
{
    FEditorViewer::Init(InWindow, InEditor, InWorld, InSelectionManager);

    Viewport.SetClient(&Client);
    Client.Initialize(InWindow, InEditor);
    Client.SetWorld(InWorld);
    Client.SetGizmo(InSelectionManager->GetGizmo());
    Client.SetSelectionManager(InSelectionManager);
    Client.SetSceneEditingShortcutsEnabled(false);
    Client.SetViewport(&Viewport);
    Client.SetState(&Viewport.GetState());
    Client.SetViewportType(EEditorViewportType::EVT_Perspective);
    Client.CreateCamera();
    Client.ApplyCameraMode();

    ed::Config Config;
    Config.SettingsFile = nullptr;
    NodeEditorContext = ed::CreateEditor(&Config);
}

void FAnimStateMachineGraphViewer::Shutdown()
{
    if (NodeEditorContext)
    {
        ed::DestroyEditor(NodeEditorContext);
        NodeEditorContext = nullptr;
    }

    Asset = nullptr;
    ObservedNodePositions.clear();
    ValidationMessage.clear();
    bDirty = false;
    Viewport.SetClient(nullptr);
}

void FAnimStateMachineGraphViewer::Tick(float DeltaTime)
{
    Client.Tick(DeltaTime);
}

void FAnimStateMachineGraphViewer::ChangeTarget(const FString& InFileName)
{
    FEditorViewer::ChangeTarget(InFileName);

    Asset = FResourceManager::Get().LoadAnimStateMachineAsset(InFileName);
    SelectedStateId = 0;
    SelectedTransitionId = 0;
    ObservedNodePositions.clear();
    bLayoutInitialized = false;
    bFitToViewPending = true;
    bDirty = false;

    if (!Asset)
    {
        ValidationMessage = "Failed to load animation state machine asset";
        UE_LOG_WARNING("[AnimSMGraphViewer] Failed to load asset: %s", InFileName.c_str());
        return;
    }

    ValidateAsset();
}

bool FAnimStateMachineGraphViewer::ValidateAsset()
{
    if (!Asset)
    {
        ValidationMessage = "State machine asset is not loaded";
        return false;
    }

    FString Message;
    if (!Asset->Validate(&Message))
    {
        ValidationMessage = Message;
        return false;
    }

    ValidationMessage = "State machine is valid.";
    return true;
}

bool FAnimStateMachineGraphViewer::SaveAsset()
{
    if (!ValidateAsset())
    {
        return false;
    }

    if (!FResourceManager::Get().SaveAnimStateMachineAsset(FileName, Asset))
    {
        ValidationMessage = "Failed to save animation state machine asset";
        return false;
    }

    ValidationMessage = "State machine saved.";
    bDirty = false;
    return true;
}

bool FAnimStateMachineGraphViewer::UpdateObservedNodePosition(FAnimStateId StateId, float NodeX, float NodeY)
{
    constexpr float PositionEpsilon = 0.5f;
    for (FAnimStateEditorMetadata& Position : ObservedNodePositions)
    {
        if (Position.StateId == StateId)
        {
            const bool bChanged =
                std::abs(Position.NodeX - NodeX) > PositionEpsilon ||
                std::abs(Position.NodeY - NodeY) > PositionEpsilon;
            Position.NodeX = NodeX;
            Position.NodeY = NodeY;
            return bChanged;
        }
    }

    FAnimStateEditorMetadata Position;
    Position.StateId = StateId;
    Position.NodeX = NodeX;
    Position.NodeY = NodeY;
    ObservedNodePositions.push_back(Position);
    return false;
}
