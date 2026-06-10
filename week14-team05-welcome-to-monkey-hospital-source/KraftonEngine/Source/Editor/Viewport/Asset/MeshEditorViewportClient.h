#pragma once

#include "Viewport/EditorPreviewViewportClient.h"
#include "Viewport/ViewportClient.h"
#include "Editor/Viewport/ViewportCameraTransform.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Editor/Slate/SWindow.h"
#include "Core/Types/RayTypes.h"
#include "Gizmo/BoneTransformGizmoTarget.h"
#include "Gizmo/PhysicsAssetGizmoTarget.h"
#include "Component/Debug/BoneDebugComponent.h"

#include <d3d11.h>
#include "Object/GarbageCollection.h"

class UGizmoComponent;
class UPhysicsAssetPreviewComponent;
class UPhysicsAsset;
class FWindowsWindow;
class UWorld;
class AActor;
class USkeletalMesh;
class USkeleton;

class FMeshEditorViewportClient : public FViewportClient, public IEditorPreviewViewportClient, public FGCObject
{
public:
	void Initialize(ID3D11Device* Device, uint32 Width, uint32 Height);
	void Release();

	const char* GetReferencerName() const override { return "FMeshEditorViewportClient"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	void CreatePreviewGizmo();
	void CreateBoneDebugComponent();
	void CreatePhysicsAssetPreviewComponent();
	void ResetCameraToPreviousBounds();

	void SetPreviewWorld(UWorld* InWorld) { PreviewWorld = InWorld; }
	void SetPreviewActor(AActor* InActor) { PreviewActor = InActor; }
	void SetPreviewMeshComponent(USkeletalMeshComponent* InComp) { PreviewMeshComponent = InComp; }
	void SetViewportRect(float X, float Y, float Width, float Height) { ViewportScreenRect = { X, Y, Width, Height }; }

	bool IsRenderable() const override { return bIsRenderable; }
	bool IsMouseOverViewport() const override;
	bool GetMouseRay(FRay& OutRay) const;

	bool IsGizmoHolding() const;

	FViewport* GetViewport() const override { return Viewport; }
	UWorld* GetPreviewWorld() const override { return PreviewWorld; }

	UGizmoComponent* GetGizmo() const { return Gizmo; }
	USkeletalMeshComponent* GetPreviewMeshComponent() const { return PreviewMeshComponent; }
	UPhysicsAssetPreviewComponent* GetPhysicsAssetPreviewComponent() const { return PhysicsAssetPreviewComponent; }
	ID3D11Device* GetRenderDevice() const { return RenderDevice; }

	FViewportRenderOptions& GetRenderOptions() override { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const override { return RenderOptions; }

	void NotifyViewportResized(int32 NewWidth, int32 NewHeight) override;

	bool GetCameraView(FMinimalViewInfo& OutPOV) const override;

	void Tick(float DeltaTime);

	void SetSelectedBone(USkeletalMesh* Mesh, int32 BoneIndex);
	void SetSelectedSocket(USkeletalMesh* Mesh, USkeleton* Skeleton, int32 SocketIndex);
	void SetSelectedPhysicsAssetElement(
		UPhysicsAsset* PhysicsAsset,
		int32 BodyIndex,
		int32 ShapeIndex,
		int32 ConstraintIndex,
		EPhysicsAssetConstraintFrameTarget ConstraintFrameTarget);
	void ClearPhysicsAssetGizmoTarget();
	const FBone* GetSelectedBone() const;
	bool ConsumeSocketGizmoModified();
	bool ConsumePhysicsAssetGizmoModified();
	bool ConsumePhysicsAssetViewportPick(int32& OutBodyIndex, int32& OutShapeIndex, int32& OutConstraintIndex);
	bool FocusSelectedPhysicsAssetElementImmediate();
	void RefreshBoneDebug();
	void SetPhysicsSimulationInteractionActive(bool bActive);

	EBoneDebugDrawMode GetBoneDebugDrawMode() const;
	void SetBoneDebugDrawMode(EBoneDebugDrawMode InDrawMode);

	void ApplyTransformSettingsToGizmo();
	void ToggleCoordSystem();

private:
	enum class EPhysicsGizmoSelectionKind : uint8
	{
		None,
		Body,
		Shape,
		ConstraintFrame
	};

	void TickShortcuts();
	void TickInput(float DeltaTime);
	void TickInteraction(float DeltaTime);
	void SyncCameraSmoothingTarget();
	void ApplySmoothedCameraLocation(float DeltaTime);
	void FocusOnLocation(const FVector& TargetLoc, bool bAnimate);

	void SyncGizmo();

	void HandleDragStart(const FRay& Ray);
	bool TryPickPhysicsAssetPreviewShape(const FRay& Ray);

private:
	USkeletalMesh* SelectedMesh = nullptr;
	USkeleton* SelectedSocketSkeleton = nullptr;
	int32 SelectedBoneIndex = -1;
	int32 SelectedSocketIndex = -1;

	FViewport* Viewport = nullptr;
	FWindowsWindow* Window = nullptr;
	FViewportRenderOptions RenderOptions;

	FBoneTransformGizmoTarget BoneTarget;
	FSocketTransformGizmoTarget SocketTarget;
	FPhysicsAssetBodyGizmoTarget PhysicsBodyTarget;
	FPhysicsAssetShapeGizmoTarget PhysicsShapeTarget;
	FPhysicsAssetConstraintFrameGizmoTarget PhysicsConstraintFrameTarget;
	UGizmoComponent* Gizmo = nullptr;
	USkeletalMeshComponent* PreviewMeshComponent = nullptr;
	UBoneDebugComponent* BoneDebugComponent = nullptr;
	UPhysicsAssetPreviewComponent* PhysicsAssetPreviewComponent = nullptr;
	ID3D11Device* RenderDevice = nullptr;

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;

	EPhysicsGizmoSelectionKind ActivePhysicsGizmoKind = EPhysicsGizmoSelectionKind::None;
	UPhysicsAsset* ActivePhysicsGizmoAsset = nullptr;
	int32 ActivePhysicsGizmoBodyIndex = -1;
	int32 ActivePhysicsGizmoShapeIndex = -1;
	int32 ActivePhysicsGizmoConstraintIndex = -1;
	EPhysicsAssetConstraintFrameTarget ActivePhysicsGizmoConstraintFrame = EPhysicsAssetConstraintFrameTarget::Child;

	bool bHasPendingPhysicsAssetViewportPick = false;
	int32 PendingPhysicsAssetPickBodyIndex = -1;
	int32 PendingPhysicsAssetPickShapeIndex = -1;
	int32 PendingPhysicsAssetPickConstraintIndex = -1;
	bool bPhysicsSimulationInteractionActive = false;

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
