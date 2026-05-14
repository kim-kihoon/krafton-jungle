#pragma once

#include "Core/CoreMinimal.h"
#include "Object/FName.h"
#include "Editor/Viewport/FSceneViewport.h"
#include "Editor/Viewport/SkeletalMeshPreviewViewportClient.h"
#include "Editor/Input/SkeletalMeshPreviewController.h"
#include "Engine/Input/InputRouter.h"
#include "Engine/Geometry/Ray.h"

class UEditorEngine;
class UWorld;
class AActor;
class USkeletalMesh;
class USkeletalMeshComponent;
class UGizmoComponent;

class FSkeletalMeshPreviewScene
{
public:
	FSkeletalMeshPreviewScene() = default;
	~FSkeletalMeshPreviewScene();

	void Initialize(UEditorEngine* InEditor);
	void Shutdown();
	void Tick(float DeltaTime);

	void SetVisible(bool bInVisible);
	void SetSkeletalMesh(USkeletalMesh* Mesh);
	void ResetPose();

	USkeletalMeshComponent* GetPreviewMeshComponent() const;
	USkeletalMesh* GetCurrentSkeletalMesh() const;

	void SelectBone(int32 BoneIndex);
	int32 GetSelectedBoneIndex() const;
	bool PickBoneJoint(const FRay& Ray, int32& OutBoneIndex) const;

	FSceneViewport& GetSceneViewport() { return PreviewViewport; }
	const FSceneViewport& GetSceneViewport() const { return PreviewViewport; }
	int32 GetViewportIndex() const { return 4; }

	void SetViewportSize(uint32 Width, uint32 Height);

	UWorld* GetWorld() const { return PreviewWorld; }
	AActor* GetPreviewActor() const { return PreviewActor; }
	FSkeletalMeshPreviewViewportClient& GetViewportClient() { return ViewportClient; }
	const FSkeletalMeshPreviewViewportClient& GetViewportClient() const { return ViewportClient; }

	void SetInputRectFromScreenRect(float MinX, float MinY, float MaxX, float MaxY);
	void SetViewportHovered(bool bHovered) { bPreviewHovered = bHovered; }
	bool IsInputCaptured() const { return bPreviewInputCaptured; }
	bool IsInputActive() const { return bPreviewHovered || bPreviewInputCaptured; }

	bool IsSkeletonVisible() const { return bShowSkeleton; }
	void SetSkeletonVisible(bool bVisible) { bShowSkeleton = bVisible; }

	bool IsFullSkeletonVisible() const { return bShowFullSkeleton; }
	void SetFullSkeletonVisible(bool bVisible) { bShowFullSkeleton = bVisible; }

	UGizmoComponent* GetPreviewGizmo() const { return PreviewGizmo; }

public:
	UGizmoComponent* PreviewGizmo = nullptr;

private:
	UEditorEngine* Editor = nullptr;
	FName WorldHandle;

	FSkeletalMeshPreviewViewportClient ViewportClient;
	FSceneViewport PreviewViewport;

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;

	FInputRouter PreviewInputRouter;
	FSkeletalMeshPreviewController PreviewInputController;

	FViewportRect PreviewInputRect;
	bool bPreviewHovered = false;
	bool bPreviewInputCaptured = false;

	int32 SelectedBoneIndex = -1;

	bool bShowSkeleton = true;
	bool bShowFullSkeleton = false;
};
