#include "MeshEditorViewportClient.h"

#include "Render/Types/MinimalViewInfo.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"
#include "Input/InputSystem.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/Debug/GizmoComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Debug/BoneDebugComponent.h"
#include "Component/Debug/PhysicsAssetPreviewComponent.h"
#include "Physics/PhysicsAsset.h"
#include "Collision/Ray/RayUtils.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"

#include <imgui.h>
#include <algorithm>

void FMeshEditorViewportClient::Initialize(ID3D11Device* Device, uint32 Width, uint32 Height)
{
	RenderDevice = Device;
	Viewport = new FViewport();
	Viewport->Initialize(Device, Width, Height);
	Viewport->SetClient(this);

	bIsRenderable = true;
}

void FMeshEditorViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SelectedMesh);
	Collector.AddReferencedObject(SelectedSocketSkeleton);
	Collector.AddReferencedObject(Gizmo);
	Collector.AddReferencedObject(PreviewMeshComponent);
	Collector.AddReferencedObject(BoneDebugComponent);
	Collector.AddReferencedObject(PhysicsAssetPreviewComponent);
	Collector.AddReferencedObject(PreviewWorld);
	Collector.AddReferencedObject(PreviewActor);
	Collector.AddReferencedObject(ActivePhysicsGizmoAsset);
}

void FMeshEditorViewportClient::Release()
{
	if (Viewport)
	{
		Viewport->Release();
		delete Viewport;
		Viewport = nullptr;
	}

	PreviewWorld = nullptr;
	PreviewActor = nullptr;
	ClearPhysicsAssetGizmoTarget();

	UObjectManager::Get().DestroyObject(Gizmo);
	Gizmo = nullptr;
	BoneDebugComponent = nullptr;
	PhysicsAssetPreviewComponent = nullptr;
	RenderDevice = nullptr;

	bIsRenderable = false;

	SetSelectedBone(nullptr, -1);
}

void FMeshEditorViewportClient::CreatePreviewGizmo()
{
	Gizmo = UObjectManager::Get().CreateObject<UGizmoComponent>();
	Gizmo->SetScene(&PreviewWorld->GetScene());
	Gizmo->CreateRenderState();
	Gizmo->Deactivate();
}

void FMeshEditorViewportClient::CreateBoneDebugComponent()
{
	BoneDebugComponent = PreviewActor->AddComponent<UBoneDebugComponent>();
	BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
	BoneDebugComponent->SetSelectedBoneIndex(SelectedBoneIndex);
	BoneDebugComponent->SetSelectedSocketIndex(SelectedSocketIndex);
	BoneDebugComponent->CreateRenderState();
}

void FMeshEditorViewportClient::CreatePhysicsAssetPreviewComponent()
{
	PhysicsAssetPreviewComponent = PreviewActor->AddComponent<UPhysicsAssetPreviewComponent>();
	PhysicsAssetPreviewComponent->CreateRenderState();
	PhysicsAssetPreviewComponent->SetVisibility(false);
}

void FMeshEditorViewportClient::ResetCameraToPreviousBounds()
{
	if (!PreviewActor)
	{
		ViewTransform.ViewLocation = FVector(-5.0f, -5.0f, 3.0f);
		ViewTransform.LookAt(FVector::ZeroVector);
		TargetLocation = ViewTransform.ViewLocation;
		return;
	}

	FBoundingBox Bounds = PreviewMeshComponent->GetWorldBoundingBox();
	FVector Center = Bounds.GetCenter();
	float Radius = Bounds.GetExtent().Length();

	if (Radius < 0.1f)
	{
		Radius = 1.0f;
	}

	const float FovRadians = ViewTransform.FOV;
	const float Distance = Radius / std::tan(FovRadians * 0.5f) * 1.25f;

	const FVector ViewDir = FVector(-1.0f, -1.0f, -0.6f).Normalized();
	
	ViewTransform.ViewLocation = Center - ViewDir * Distance;
	ViewTransform.LookAt(Center);

	TargetLocation = ViewTransform.ViewLocation;
	LastAppliedCameraLocation = ViewTransform.ViewLocation;
	bTargetLocationInitialized = true;
	bLastAppliedCameraLocationInitialized = true;
}

bool FMeshEditorViewportClient::IsMouseOverViewport() const
{
	if (!bIsRenderable || ViewportScreenRect.Width <= 0.0f || ViewportScreenRect.Height <= 0.0f) return false;

	ImVec2 MousePos = ImGui::GetMousePos();
	return MousePos.x >= ViewportScreenRect.X && MousePos.x <= (ViewportScreenRect.X + ViewportScreenRect.Width) &&
		MousePos.y >= ViewportScreenRect.Y && MousePos.y <= (ViewportScreenRect.Y + ViewportScreenRect.Height);
}

bool FMeshEditorViewportClient::GetMouseRay(FRay& OutRay) const
{
	if (!IsMouseOverViewport())
	{
		return false;
	}

	const float VPWidth = std::max(1.0f, ViewportScreenRect.Width);
	const float VPHeight = std::max(1.0f, ViewportScreenRect.Height);
	ImVec2 MousePos = ImGui::GetIO().MousePos;
	const float LocalMouseX = MousePos.x - ViewportScreenRect.X;
	const float LocalMouseY = MousePos.y - ViewportScreenRect.Y;

	FMinimalViewInfo POV;
	if (!GetCameraView(POV))
	{
		return false;
	}

	OutRay = POV.DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);
	return true;
}

bool FMeshEditorViewportClient::IsGizmoHolding() const
{
	return Gizmo && Gizmo->IsHolding();
}

void FMeshEditorViewportClient::NotifyViewportResized(int32 NewWidth, int32 NewHeight)
{
	if (Viewport)
	{
		ViewTransform.AspectRatio = static_cast<float>(NewWidth) / static_cast<float>(NewHeight);
	}
}

bool FMeshEditorViewportClient::GetCameraView(FMinimalViewInfo& OutPOV) const
{
	OutPOV.Location = ViewTransform.ViewLocation;
	OutPOV.Rotation = ViewTransform.ViewRotation;
	OutPOV.FOV = ViewTransform.FOV;
	OutPOV.AspectRatio = ViewTransform.AspectRatio;
	return true;
}

void FMeshEditorViewportClient::Tick(float DeltaTime)
{
	SyncCameraSmoothingTarget();

	if (bIsFocusAnimating)
	{
		FocusAnimTimer += DeltaTime;
		float Alpha = Clamp(FocusAnimTimer / FocusAnimDuration, 0.0f, 1.0f);
		if (Alpha >= 1.0f)
		{
			Alpha = 1.0f;
			bIsFocusAnimating = false;
		}

		float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);

		FVector NewLoc = FocusStartLoc * (1.0f - SmoothAlpha) + FocusEndLoc * SmoothAlpha;

		FQuat StartQuat = FocusStartRot.ToQuaternion();
		FQuat EndQuat = FocusEndRot.ToQuaternion();
		FQuat BlendedQuat = FQuat::Slerp(StartQuat, EndQuat, SmoothAlpha);

		ViewTransform.ViewLocation = NewLoc;
		ViewTransform.ViewRotation = FRotator::FromQuaternion(BlendedQuat);

		TargetLocation = NewLoc;
		LastAppliedCameraLocation = NewLoc;
		bLastAppliedCameraLocationInitialized = true;
	}
	else
	{
		ApplySmoothedCameraLocation(DeltaTime);
	}

	TickShortcuts();
	TickInput(DeltaTime);
	TickInteraction(DeltaTime);
}

void FMeshEditorViewportClient::SetSelectedBone(USkeletalMesh* Mesh, int32 BoneIndex)
{
	SelectedMesh = Mesh;
	SelectedSocketSkeleton = nullptr;
	SelectedBoneIndex = BoneIndex;
	SelectedSocketIndex = -1;
	ActivePhysicsGizmoKind = EPhysicsGizmoSelectionKind::None;
	ActivePhysicsGizmoAsset = nullptr;
	RenderOptions.WeightBoneHeatMapBoneIndex = BoneIndex;

	if (Gizmo && PreviewMeshComponent && BoneIndex >= 0)
	{
		BoneTarget.SetBone(PreviewMeshComponent, BoneIndex);
		Gizmo->SetTarget(&BoneTarget);
	}
	else if (Gizmo)
	{
		Gizmo->Deactivate();
	}

	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
		BoneDebugComponent->SetSelectedBoneIndex(BoneIndex);
		BoneDebugComponent->SetSelectedSocketIndex(-1);
	}
}

void FMeshEditorViewportClient::SetSelectedSocket(USkeletalMesh* Mesh, USkeleton* Skeleton, int32 SocketIndex)
{
	SelectedMesh = Mesh;
	SelectedSocketSkeleton = Skeleton;
	SelectedBoneIndex = -1;
	SelectedSocketIndex = SocketIndex;
	ActivePhysicsGizmoKind = EPhysicsGizmoSelectionKind::None;
	ActivePhysicsGizmoAsset = nullptr;
	RenderOptions.WeightBoneHeatMapBoneIndex = -1;

	if (Gizmo && PreviewMeshComponent && Skeleton && SocketIndex >= 0)
	{
		SocketTarget.SetSocket(PreviewMeshComponent, Skeleton, SocketIndex);
		Gizmo->SetTarget(&SocketTarget);
	}
	else if (Gizmo)
	{
		Gizmo->Deactivate();
	}

	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
		BoneDebugComponent->SetSelectedBoneIndex(-1);
		BoneDebugComponent->SetSelectedSocketIndex(SocketIndex);
	}
}

void FMeshEditorViewportClient::SetSelectedPhysicsAssetElement(
	UPhysicsAsset* PhysicsAsset,
	int32 BodyIndex,
	int32 ShapeIndex,
	int32 ConstraintIndex,
	EPhysicsAssetConstraintFrameTarget ConstraintFrameTarget)
{
	SelectedMesh = PreviewMeshComponent ? PreviewMeshComponent->GetSkeletalMesh() : nullptr;
	SelectedBoneIndex = -1;
	SelectedSocketIndex = -1;
	RenderOptions.WeightBoneHeatMapBoneIndex = -1;

	if (!Gizmo || !PreviewMeshComponent || !PhysicsAsset)
	{
		ClearPhysicsAssetGizmoTarget();
		return;
	}

	EPhysicsGizmoSelectionKind NewKind = EPhysicsGizmoSelectionKind::None;
	if (ConstraintIndex >= 0)
	{
		NewKind = EPhysicsGizmoSelectionKind::ConstraintFrame;
	}
	else if (BodyIndex >= 0 && ShapeIndex >= 0)
	{
		NewKind = EPhysicsGizmoSelectionKind::Shape;
	}
	else if (BodyIndex >= 0)
	{
		NewKind = EPhysicsGizmoSelectionKind::Body;
	}

	if (NewKind == EPhysicsGizmoSelectionKind::None)
	{
		ClearPhysicsAssetGizmoTarget();
		return;
	}

	const bool bSameTarget =
		ActivePhysicsGizmoKind == NewKind &&
		ActivePhysicsGizmoAsset == PhysicsAsset &&
		ActivePhysicsGizmoBodyIndex == BodyIndex &&
		ActivePhysicsGizmoShapeIndex == ShapeIndex &&
		ActivePhysicsGizmoConstraintIndex == ConstraintIndex &&
		ActivePhysicsGizmoConstraintFrame == ConstraintFrameTarget;

	ActivePhysicsGizmoKind = NewKind;
	ActivePhysicsGizmoAsset = PhysicsAsset;
	ActivePhysicsGizmoBodyIndex = BodyIndex;
	ActivePhysicsGizmoShapeIndex = ShapeIndex;
	ActivePhysicsGizmoConstraintIndex = ConstraintIndex;
	ActivePhysicsGizmoConstraintFrame = ConstraintFrameTarget;

	IGizmoTransformTarget* Target = nullptr;
	switch (NewKind)
	{
	case EPhysicsGizmoSelectionKind::Body:
		PhysicsBodyTarget.SetTarget(PreviewMeshComponent, PhysicsAsset, BodyIndex);
		Target = &PhysicsBodyTarget;
		break;
	case EPhysicsGizmoSelectionKind::Shape:
		PhysicsShapeTarget.SetTarget(PreviewMeshComponent, PhysicsAsset, BodyIndex, ShapeIndex);
		Target = &PhysicsShapeTarget;
		break;
	case EPhysicsGizmoSelectionKind::ConstraintFrame:
		PhysicsConstraintFrameTarget.SetTarget(PreviewMeshComponent, PhysicsAsset, ConstraintIndex, ConstraintFrameTarget);
		Target = &PhysicsConstraintFrameTarget;
		break;
	default:
		break;
	}

	if (!Target || !Target->IsValid())
	{
		ClearPhysicsAssetGizmoTarget();
		return;
	}

	if (!bSameTarget || Gizmo->GetTarget() != Target)
	{
		Gizmo->SetTarget(Target);
	}
	else if (!Gizmo->IsHolding())
	{
		Gizmo->UpdateGizmoTransform();
	}
}

void FMeshEditorViewportClient::ClearPhysicsAssetGizmoTarget()
{
	ActivePhysicsGizmoKind = EPhysicsGizmoSelectionKind::None;
	ActivePhysicsGizmoAsset = nullptr;
	ActivePhysicsGizmoBodyIndex = -1;
	ActivePhysicsGizmoShapeIndex = -1;
	ActivePhysicsGizmoConstraintIndex = -1;
	ActivePhysicsGizmoConstraintFrame = EPhysicsAssetConstraintFrameTarget::Child;
	bHasPendingPhysicsAssetViewportPick = false;
	PendingPhysicsAssetPickBodyIndex = -1;
	PendingPhysicsAssetPickShapeIndex = -1;
	PendingPhysicsAssetPickConstraintIndex = -1;
	if (Gizmo)
	{
		Gizmo->Deactivate();
	}
}

bool FMeshEditorViewportClient::ConsumeSocketGizmoModified()
{
	const bool bModified = SocketTarget.ConsumeModified();
	if (bModified)
	{
		RefreshBoneDebug();
		USkinnedMeshComponent::RefreshSocketAttachedChildrenForSkeleton(SelectedSocketSkeleton);
	}
	return bModified;
}

bool FMeshEditorViewportClient::ConsumePhysicsAssetGizmoModified()
{
	const bool bModified =
		PhysicsBodyTarget.ConsumeModified() ||
		PhysicsShapeTarget.ConsumeModified() ||
		PhysicsConstraintFrameTarget.ConsumeModified();
	if (bModified)
	{
		RefreshBoneDebug();
	}
	return bModified;
}

bool FMeshEditorViewportClient::ConsumePhysicsAssetViewportPick(int32& OutBodyIndex, int32& OutShapeIndex, int32& OutConstraintIndex)
{
	OutBodyIndex = -1;
	OutShapeIndex = -1;
	OutConstraintIndex = -1;
	if (!bHasPendingPhysicsAssetViewportPick)
	{
		return false;
	}

	OutBodyIndex = PendingPhysicsAssetPickBodyIndex;
	OutShapeIndex = PendingPhysicsAssetPickShapeIndex;
	OutConstraintIndex = PendingPhysicsAssetPickConstraintIndex;
	bHasPendingPhysicsAssetViewportPick = false;
	PendingPhysicsAssetPickBodyIndex = -1;
	PendingPhysicsAssetPickShapeIndex = -1;
	PendingPhysicsAssetPickConstraintIndex = -1;
	return true;
}

void FMeshEditorViewportClient::RefreshBoneDebug()
{
	if (BoneDebugComponent)
	{
		BoneDebugComponent->MarkRenderStateDirty();
	}
}

const FBone* FMeshEditorViewportClient::GetSelectedBone() const
{
	if (!SelectedMesh) return nullptr;

	FSkeletalMesh* Asset = SelectedMesh->GetSkeletalMeshAsset();
	if (!Asset) return nullptr;

	if (SelectedBoneIndex < 0 || SelectedBoneIndex >= static_cast<int32>(Asset->Bones.size())) return nullptr;

	return &Asset->Bones[SelectedBoneIndex];
}

EBoneDebugDrawMode FMeshEditorViewportClient::GetBoneDebugDrawMode() const
{
	return BoneDebugComponent ? BoneDebugComponent->GetDrawMode() : EBoneDebugDrawMode::SelectedOnly;
}

void FMeshEditorViewportClient::SetBoneDebugDrawMode(EBoneDebugDrawMode InDrawMode)
{
	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetDrawMode(InDrawMode);
	}
}

void FMeshEditorViewportClient::TickShortcuts()
{
	if (!FSlateApplication::Get().DoesClientOwnKeyboardInput(this)) return;
	if (ImGui::GetIO().WantTextInput) return;

	InputSystem& Input = InputSystem::Get();

	if (!Input.GetKey(VK_CONTROL) && Input.GetKeyDown('X'))
	{
		ToggleCoordSystem();
		return;
	}

	if (Input.GetKeyDown('F'))
	{
		if (FocusSelectedPhysicsAssetElementImmediate())
		{
			return;
		}

		FVector TargetLoc = FVector::ZeroVector;
		bool bHasFocusTarget = false;

		if (GetSelectedBone())
		{
			TargetLoc = PreviewMeshComponent->GetBoneLocationByIndex(SelectedBoneIndex);
			bHasFocusTarget = true;
		}
		else if (SocketTarget.IsValid())
		{
			TargetLoc = SocketTarget.GetWorldLocation();
			bHasFocusTarget = true;
		}

		if (bHasFocusTarget)
		{
			FocusOnLocation(TargetLoc, true);
		}
	}
}

void FMeshEditorViewportClient::FocusOnLocation(const FVector& TargetLoc, bool bAnimate)
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	const FVector OriginalLoc = ViewTransform.ViewLocation;
	const FRotator OriginalRot = ViewTransform.ViewRotation;

	FBoundingBox Bounds = PreviewMeshComponent->GetWorldBoundingBox();
	float Radius = Bounds.GetExtent().Length();
	if (Radius < 0.1f)
	{
		Radius = 1.0f;
	}

	const float FocusDistance = Radius;
	const FVector CameraForward = ViewTransform.ViewRotation.GetForwardVector();
	const FVector NewCameraLoc = TargetLoc - CameraForward * FocusDistance;

	ViewTransform.ViewLocation = NewCameraLoc;
	ViewTransform.LookAt(TargetLoc);
	const FRotator TargetRot = ViewTransform.ViewRotation;

	if (bAnimate)
	{
		ViewTransform.ViewLocation = OriginalLoc;
		ViewTransform.ViewRotation = OriginalRot;

		bIsFocusAnimating = true;
		FocusAnimTimer = 0.0f;
		FocusStartLoc = OriginalLoc;
		FocusStartRot = OriginalRot;
		FocusEndLoc = NewCameraLoc;
		FocusEndRot = TargetRot;
		return;
	}

	bIsFocusAnimating = false;
	FocusAnimTimer = 0.0f;
	TargetLocation = NewCameraLoc;
	bTargetLocationInitialized = true;
	LastAppliedCameraLocation = NewCameraLoc;
	bLastAppliedCameraLocationInitialized = true;
}

bool FMeshEditorViewportClient::FocusSelectedPhysicsAssetElementImmediate()
{
	if (ActivePhysicsGizmoKind == EPhysicsGizmoSelectionKind::None || !Gizmo)
	{
		return false;
	}

	IGizmoTransformTarget* Target = Gizmo->GetTarget();
	if (!Target || !Target->IsValid())
	{
		return false;
	}

	FocusOnLocation(Target->GetWorldLocation(), false);
	return true;
}

void FMeshEditorViewportClient::SetPhysicsSimulationInteractionActive(bool bActive)
{
	if (bPhysicsSimulationInteractionActive == bActive)
	{
		return;
	}

	bPhysicsSimulationInteractionActive = bActive;
	if (bActive)
	{
		ClearPhysicsAssetGizmoTarget();
		if (Gizmo)
		{
			Gizmo->SetPressedOnHandle(false);
			if (Gizmo->IsHolding())
			{
				Gizmo->DragEnd();
			}
		}
	}
}

void FMeshEditorViewportClient::TickInput(float DeltaTime)
{
	if (!FSlateApplication::Get().DoesClientOwnMouseInput(this)) return;

	// 텍스트 입력 중에는 카메라 키/마우스 조작을 가로채지 않는다.
	if (ImGui::GetIO().WantTextInput) return;

	FViewportCameraControlSettings& ControlSettings = FEditorSettings::Get().MeshEditorViewportSettings.CameraControls;

	InputSystem& Input = InputSystem::Get();
	
	FVector LocalMove = FVector::ZeroVector;
	float WorldVerticalMove = 0.0f;
	float CameraSpeed = ControlSettings.MoveSpeed;

	if (Input.GetKey('W')) LocalMove.X += CameraSpeed;
	if (Input.GetKey('S')) LocalMove.X -= CameraSpeed;
	if (Input.GetKey('D')) LocalMove.Y += CameraSpeed;
	if (Input.GetKey('A')) LocalMove.Y -= CameraSpeed;
	if (Input.GetKey('Q')) WorldVerticalMove -= CameraSpeed;
	if (Input.GetKey('E')) WorldVerticalMove += CameraSpeed;

	const FVector Forward = ViewTransform.ViewRotation.GetForwardVector();
	const FVector Right = ViewTransform.ViewRotation.GetRightVector();
	const FVector Up = ViewTransform.ViewRotation.GetUpVector();

	FVector DeltaMove = (Forward * LocalMove.X + Right * LocalMove.Y) * DeltaTime;
	DeltaMove.Z += WorldVerticalMove * DeltaTime;
	TargetLocation += DeltaMove;

	FVector Rotation = FVector::ZeroVector;

	FVector MouseRotation = FVector::ZeroVector;
	float MouseRotationSpeed = 0.15f * ControlSettings.RotationSpeed;

	if (Input.GetKey(VK_RBUTTON))
	{
		float DeltaX = static_cast<float>(Input.MouseDeltaX());
		float DeltaY = static_cast<float>(Input.MouseDeltaY());

		MouseRotation.Y += DeltaX * MouseRotationSpeed;
		MouseRotation.Z += DeltaY * MouseRotationSpeed;
	}

	Rotation *= DeltaTime;
	ViewTransform.Rotate(Rotation.Y + MouseRotation.Y, Rotation.Z + MouseRotation.Z);

	if (Input.GetKeyUp(VK_SPACE) && Gizmo && Gizmo->HasTarget())
	{
		Gizmo->SetNextMode();
		ApplyTransformSettingsToGizmo();
	}
}

void FMeshEditorViewportClient::TickInteraction(float DeltaTime)
{
	if (!FSlateApplication::Get().DoesClientOwnMouseInput(this)) return;

	if (!Gizmo || !PreviewWorld) return;

	FViewportCameraControlSettings& ControlSettings = FEditorSettings::Get().MeshEditorViewportSettings.CameraControls;

	Gizmo->ApplyScreenSpaceScaling(ViewTransform.ViewLocation, ViewTransform.bIsOrtho, ViewTransform.OrthoZoom);
	Gizmo->SetAxisMask(UGizmoComponent::ComputeAxisMask(RenderOptions.ViewportType, Gizmo->GetMode()));

	const float ZoomSpeed = ControlSettings.ZoomSpeed;

	float ScrollNotches = InputSystem::Get().GetScrollNotches();
	if (ScrollNotches != 0.0f)
	{
		if (bool bIsRightButtonDown = InputSystem::Get().GetKey(VK_RBUTTON))
		{
			float& MoveSpeed = FEditorSettings::Get().MeshEditorViewportSettings.CameraControls.MoveSpeed;
			if (ScrollNotches < 0.0f)
			{
				MoveSpeed = MoveSpeed * 0.9f;
			}
			else
			{
				MoveSpeed = MoveSpeed * 1.1f;
			}

			if (MoveSpeed > 1000.0f)
			{
				MoveSpeed = 1000.0f;
			}
			if (MoveSpeed < 0.001f)
			{
				MoveSpeed = 0.001f;
			}
		}
		else
		{
			if (ViewTransform.bIsOrtho)
			{
				// D.2: ViewTransform 직접 갱신.
				float NewWidth = ViewTransform.OrthoZoom - ScrollNotches * ZoomSpeed * DeltaTime;
				ViewTransform.OrthoZoom = Clamp(NewWidth, 0.1f, 1000.0f);
			}
			else
			{
				//foot zoom 발줌은 절대 delta time를 곱하지 않음. 노치당 이동 거리가 일정해야 하기 때문.
				// Instead of moving directly, update TargetLocation for smooth zoom
				TargetLocation += ViewTransform.ViewRotation.GetForwardVector() * (ScrollNotches * ZoomSpeed * 0.015f);
				// UnrealEngine의 Mouse Scroll Camera Speed는 노치당 5
			}
		}
	}


	if (bPhysicsSimulationInteractionActive)
	{
		return;
	}

	ImVec2 MousePos = ImGui::GetIO().MousePos;
	float LocalMouseX = MousePos.x - ViewportScreenRect.X;
	float LocalMouseY = MousePos.y - ViewportScreenRect.Y;

	float VPWidth = Viewport ? static_cast<float>(Viewport->GetWidth()) : 1.0f;
	float VPHeight = Viewport ? static_cast<float>(Viewport->GetHeight()) : 1.0f;

	FMinimalViewInfo POV;
	GetCameraView(POV);
	FRay Ray = POV.DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);
	FHitResult HitResult;

	FRayUtils::RaycastComponent(Gizmo, Ray, HitResult);

	InputSystem& Input = InputSystem::Get();

	if (Input.GetKeyDown(VK_LBUTTON))
	{
		HandleDragStart(Ray);
	}
	else if (Input.GetLeftDragging())
	{
		if (Gizmo->IsPressedOnHandle() && !Gizmo->IsHolding())
		{
			Gizmo->SetHolding(true);
		}

		if (Gizmo->IsHolding())
		{
			Gizmo->UpdateDrag(Ray);
		}
	}
	else if (Input.GetLeftDragEnd())
	{
		if (Gizmo->IsHolding())
		{
			Gizmo->DragEnd();
		}
	}
	else if (Input.GetKeyUp(VK_LBUTTON))
	{
		Gizmo->SetPressedOnHandle(false);
	}
}

void FMeshEditorViewportClient::SyncCameraSmoothingTarget()
{
	const FVector CurrentLocation = ViewTransform.ViewLocation;
	const bool bCameraMovedExternally = bLastAppliedCameraLocationInitialized
		&& FVector::DistSquared(CurrentLocation, LastAppliedCameraLocation) > 0.0001f;

	if (!bTargetLocationInitialized || bCameraMovedExternally)
	{
		TargetLocation = CurrentLocation;
		bTargetLocationInitialized = true;
	}

	LastAppliedCameraLocation = CurrentLocation;
	bLastAppliedCameraLocationInitialized = true;
}

void FMeshEditorViewportClient::ApplySmoothedCameraLocation(float DeltaTime)
{
	const FVector CurrentLocation = ViewTransform.ViewLocation;
	const float LerpAlpha = Clamp(DeltaTime * SmoothLocationSpeed, 0.0f, 1.0f);
	const FVector NewLocation = CurrentLocation + (TargetLocation - CurrentLocation) * LerpAlpha;
	ViewTransform.ViewLocation = NewLocation;

	LastAppliedCameraLocation = NewLocation;
	bLastAppliedCameraLocationInitialized = true;
}

void FMeshEditorViewportClient::SyncGizmo()
{
	if (!Gizmo || !PreviewActor) return;

	if (const FBone* SelectedBone = GetSelectedBone())
	{
	}
	else if (SocketTarget.IsValid())
	{
	}
	else if (ActivePhysicsGizmoKind != EPhysicsGizmoSelectionKind::None && Gizmo->GetTarget() && Gizmo->GetTarget()->IsValid())
	{
		Gizmo->UpdateGizmoTransform();
	}
	else
	{
		Gizmo->Deactivate();
	}
}

void FMeshEditorViewportClient::ApplyTransformSettingsToGizmo()
{
	if (!Gizmo) return;

	const FGizmoToolSettings& Settings = FEditorSettings::Get().MeshEditorViewportSettings.Gizmo;
	const bool bForceLocalForScale = Gizmo->GetMode() == EGizmoMode::Scale;

	Gizmo->SetWorldSpace(bForceLocalForScale ? false : Settings.CoordSystem == EEditorCoordSystem::World);
	Gizmo->SetSnapSettings(
		Settings.bEnableTranslationSnap, Settings.TranslationSnapSize,
		Settings.bEnableRotationSnap, Settings.RotationSnapSize,
		Settings.bEnableScaleSnap, Settings.ScaleSnapSize
	);
}

void FMeshEditorViewportClient::ToggleCoordSystem()
{
	FGizmoToolSettings& Settings = FEditorSettings::Get().MeshEditorViewportSettings.Gizmo;
	Settings.CoordSystem = (Settings.CoordSystem == EEditorCoordSystem::World)
		? EEditorCoordSystem::Local
		: EEditorCoordSystem::World;
	ApplyTransformSettingsToGizmo();
}

void FMeshEditorViewportClient::HandleDragStart(const FRay& Ray)
{
	if (bPhysicsSimulationInteractionActive)
	{
		return;
	}

	FHitResult Hit;
	if (Gizmo && FRayUtils::RaycastComponent(Gizmo, Ray, Hit))
	{
		Gizmo->SetPressedOnHandle(true);
		return;
	}

	if (TryPickPhysicsAssetPreviewShape(Ray) && Gizmo)
	{
		Gizmo->SetPressedOnHandle(false);
	}
}

bool FMeshEditorViewportClient::TryPickPhysicsAssetPreviewShape(const FRay& Ray)
{
	if (bPhysicsSimulationInteractionActive)
	{
		return false;
	}

	if (!PhysicsAssetPreviewComponent || !PhysicsAssetPreviewComponent->IsVisible())
	{
		return false;
	}

	FHitResult Hit;
	if (!FRayUtils::RaycastComponent(PhysicsAssetPreviewComponent, Ray, Hit))
	{
		return false;
	}

	int32 BodyIndex = -1;
	int32 ShapeIndex = -1;
	int32 ConstraintIndex = -1;
	if (UPhysicsAssetPreviewComponent::DecodeConstraintFaceIndex(Hit.FaceIndex, ConstraintIndex))
	{
		bHasPendingPhysicsAssetViewportPick = true;
		PendingPhysicsAssetPickBodyIndex = -1;
		PendingPhysicsAssetPickShapeIndex = -1;
		PendingPhysicsAssetPickConstraintIndex = ConstraintIndex;
		return true;
	}

	if (!UPhysicsAssetPreviewComponent::DecodeSelectionFaceIndex(Hit.FaceIndex, BodyIndex, ShapeIndex))
	{
		return false;
	}

	bHasPendingPhysicsAssetViewportPick = true;
	PendingPhysicsAssetPickBodyIndex = BodyIndex;
	PendingPhysicsAssetPickShapeIndex = ShapeIndex;
	PendingPhysicsAssetPickConstraintIndex = -1;
	return true;
}
