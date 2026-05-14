#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/SkeletalMesh/SkeletalMeshPreviewScene.h"
#include "Engine/Asset/SkeletalMesh.h"

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	void Initialize(UEditorEngine* InEditorEngine) override;
	void Render(float DeltaTime) override;

	void SetInstanceId(int32 InInstanceId);
	int32 GetInstanceId() const { return InstanceId; }
	const FString& GetWindowName() const { return WindowName; }
	void OpenMesh(const FString& MeshPath);
	void RequestFocus() { bFocusNextFrame = true; }

	void SetOpen(bool bInOpen) { bIsOpen = bInOpen; }
	bool IsOpen() const { return bIsOpen; }
	bool IsWindowFocused() const { return bWindowFocused; }
	bool IsViewportInputActive() const { return bIsOpen && PreviewScene.IsInputActive(); }
	bool IsViewportInputCaptured() const { return bIsOpen && PreviewScene.IsInputCaptured(); }

	FSkeletalMeshPreviewScene* GetPreviewScene() { return &PreviewScene; }
	const FSkeletalMeshPreviewScene* GetPreviewScene() const { return &PreviewScene; }

private:
	void RenderToolbar();

	void RenderBoneHierarchyPanel();
	void RenderBoneTree(int32 BoneIndex, const TArray<FSkeletalBone>& Bones);
	void RenderBoneDetailsPanel();

	void RebuildBoneCache(USkeletalMesh* Mesh);
	void SyncCurrentMeshFromPreview();
	void RefreshSkeletalMeshPathCache();

	bool bIsOpen = false;
	bool bWindowFocused = false;
	bool bFocusNextFrame = false;
	int32 InstanceId = 0;
	FString WindowName;

	int32 SelectedMeshPathIndex = -1;
	TArray<FString> CachedSkeletalMeshPaths;

	FSkeletalMeshPreviewScene PreviewScene;

	USkeletalMesh* CurrentSkeletalMesh = nullptr;
	int32 SelectedBoneIndex = -1;
	FVector CachedEulerRotation;
	int32 LastEditedBoneIndex = -1;

	TArray<TArray<int32>> CachedBoneChildren;

	TArray<int32> RootBones;
	uint32 LastViewportWidth = 0;
	uint32 LastViewportHeight = 0;
};
