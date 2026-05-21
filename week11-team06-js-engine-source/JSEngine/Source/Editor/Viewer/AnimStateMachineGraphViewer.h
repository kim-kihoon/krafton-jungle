#pragma once

#include "Animation/AnimStateMachineTypes.h"
#include "Editor/Viewer/EditorViewer.h"

class UAnimStateMachineAsset;

namespace ax::NodeEditor
{
struct EditorContext;
}

class FAnimStateMachineGraphViewer : public FEditorViewer
{
public:
    ~FAnimStateMachineGraphViewer() override;

    void Init(
        FWindowsWindow* InWindow,
        UEditorEngine* InEditor,
        UWorld* InWorld,
        FSelectionManager* InSelectionManager) override;
    void Shutdown() override;
    void Tick(float DeltaTime) override;

    FEditorViewportClient* GetViewportClient() override { return &Client; }
    const FEditorViewportClient* GetViewportClient() const override { return &Client; }
    FEditorViewportClient& GetClient() override { return Client; }
    const FEditorViewportClient& GetClient() const override { return Client; }

    void ChangeTarget(const FString& InFileName) override;

    UAnimStateMachineAsset* GetAsset() const { return Asset; }
    ax::NodeEditor::EditorContext* GetNodeEditorContext() const { return NodeEditorContext; }

    uint32 GetSelectedStateId() const { return SelectedStateId; }
    void SetSelectedStateId(uint32 InStateId) { SelectedStateId = InStateId; }

    uint32 GetSelectedTransitionId() const { return SelectedTransitionId; }
    void SetSelectedTransitionId(uint32 InTransitionId) { SelectedTransitionId = InTransitionId; }

    const FString& GetValidationMessage() const { return ValidationMessage; }
    bool IsDirty() const { return bDirty; }
    void MarkDirty() { bDirty = true; }
    bool ValidateAsset();
    bool SaveAsset();

    bool IsLayoutInitialized() const { return bLayoutInitialized; }
    void MarkLayoutInitialized() { bLayoutInitialized = true; }

    bool IsFitToViewPending() const { return bFitToViewPending; }
    void MarkFitToViewDone() { bFitToViewPending = false; }

    bool UpdateObservedNodePosition(FAnimStateId StateId, float NodeX, float NodeY);

private:
    FEditorViewportClient Client;
    UAnimStateMachineAsset* Asset = nullptr;
    ax::NodeEditor::EditorContext* NodeEditorContext = nullptr;
    FString ValidationMessage;
    TArray<FAnimStateEditorMetadata> ObservedNodePositions;
    uint32 SelectedStateId = 0;
    uint32 SelectedTransitionId = 0;
    bool bLayoutInitialized = false;
    bool bFitToViewPending = false;
    bool bDirty = false;
};
