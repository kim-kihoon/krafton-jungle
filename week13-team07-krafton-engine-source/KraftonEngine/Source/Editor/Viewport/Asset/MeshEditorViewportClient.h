#pragma once

#include "Viewport/EditorPreviewViewportClient.h"
#include "Viewport/ViewportClient.h"
#include "Editor/Viewport/ViewportCameraTransform.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Editor/Slate/SWindow.h"
#include "Core/Types/RayTypes.h"
#include "Gizmo/BoneTransformGizmoTarget.h"
#include "Gizmo/SocketTransformGizmoTarget.h"
#include "Component/Debug/BoneDebugComponent.h"
#include "Object/GarbageCollection.h"
#include "Object/FName.h"

#include <d3d11.h>
#include <functional>
#include <utility>

class UGizmoComponent;
class FWindowsWindow;
class UWorld;
class AActor;
class USkeletalMesh;
class UStaticMeshComponent;

class FMeshEditorViewportClient : public FViewportClient, public IEditorPreviewViewportClient, public FGCObject
{
public:
	void Initialize(ID3D11Device* Device, uint32 Width, uint32 Height);
	void Release();

	const char* GetReferencerName() const override { return "FMeshEditorViewportClient"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	void CreatePreviewGizmo();
	void CreateBoneDebugComponent();
	void ResetCameraToPreviousBounds();

	void SetPreviewWorld(UWorld* InWorld) { PreviewWorld = InWorld; }
	void SetPreviewActor(AActor* InActor) { PreviewActor = InActor; }
	void SetPreviewMeshComponent(USkeletalMeshComponent* InComp) { PreviewMeshComponent = InComp; }
	void SetSocketPreviewMesh(const FName& SocketName, const FString& StaticMeshPath);
	void ClearSocketPreviewMesh(const FName& SocketName);
	void ClearSocketPreviewMeshes();
	void RefreshSocketPreviewMeshes(USkeletalMesh* Mesh);
	UStaticMeshComponent* FindSocketPreviewMesh(const FName& SocketName) const;
	void SetViewportRect(float X, float Y, float Width, float Height);
	void SetPreviewPickHandler(std::function<bool(const FRay&)> InHandler) { PreviewPickHandler = std::move(InHandler); }
	void SetPreviewRagdollInteractionHandler(std::function<bool(const FRay&, bool, bool, bool, float)> InHandler)
	{
		PreviewRagdollInteractionHandler = std::move(InHandler);
	}

	bool IsRenderable() const override { return bIsRenderable; }
	bool IsMouseOverViewport() const override;

	bool IsGizmoHolding() const;

	FViewport* GetViewport() const override { return Viewport; }
	UWorld* GetPreviewWorld() const override { return PreviewWorld; }

	UGizmoComponent* GetGizmo() const { return Gizmo; }
	USkeletalMeshComponent* GetPreviewMeshComponent() const { return PreviewMeshComponent; }

	FViewportRenderOptions& GetRenderOptions() override { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const override { return RenderOptions; }

	void NotifyViewportResized(int32 NewWidth, int32 NewHeight) override;

	bool GetCameraView(FMinimalViewInfo& OutPOV) const override;

	void Tick(float DeltaTime);

	void SetSelectedBone(USkeletalMesh* Mesh, int32 BoneIndex);
	void SetSelectedSocket(USkeletalMesh* Mesh, const FName& SocketName);
	bool ConsumeSocketTransformEdited();
	const FBone* GetSelectedBone() const;

	EBoneDebugDrawMode GetBoneDebugDrawMode() const;
	void SetBoneDebugDrawMode(EBoneDebugDrawMode InDrawMode);

	void ApplyTransformSettingsToGizmo();

private:
	void TickShortcuts();
	void TickInput(float DeltaTime);
	void TickInteraction(float DeltaTime);
	void SyncCameraSmoothingTarget();
	void ApplySmoothedCameraLocation(float DeltaTime);

	void SyncGizmo();

	void HandleDragStart(const FRay& Ray);

private:
	USkeletalMesh* SelectedMesh = nullptr;
	int32 SelectedBoneIndex = -1;
	FName SelectedSocketName = FName::None;
	bool bSocketTransformEdited = false;

	FViewport* Viewport = nullptr;
	FWindowsWindow* Window = nullptr;
	FViewportRenderOptions RenderOptions;

	FBoneTransformGizmoTarget BoneTarget;
	FSocketTransformGizmoTarget SocketTarget;
	UGizmoComponent* Gizmo = nullptr;
	USkeletalMeshComponent* PreviewMeshComponent = nullptr;
	UBoneDebugComponent* BoneDebugComponent = nullptr;
	TMap<FName, UStaticMeshComponent*> SocketPreviewMeshes;
	std::function<bool(const FRay&)> PreviewPickHandler;
	std::function<bool(const FRay&, bool, bool, bool, float)> PreviewRagdollInteractionHandler;

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;

	bool bIsRenderable = false;

	FViewportCameraTransform ViewTransform;

	FRect ViewportScreenRect;

	// Camera Focus Animation
	bool bIsFocusAnimating = false;
	FVector FocusStartLoc;
	FRotator FocusStartRot;
	FVector FocusEndLoc;
	FRotator FocusEndRot;
	float FocusAnimTimer = 0.0f;
	const float FocusAnimDuration = 0.5f;

	// Camera Smoothing
	FVector TargetLocation;
	bool bTargetLocationInitialized = false;
	FVector LastAppliedCameraLocation;
	bool bLastAppliedCameraLocationInitialized = false;
	const float SmoothLocationSpeed = 10.0f;
};
