#include "MeshEditorViewportClient.h"

#include "Render/Types/MinimalViewInfo.h"
#include "Viewport/Viewport.h"
#include "Math/MathUtils.h"
#include "Input/InputSystem.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/Debug/GizmoComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Mesh/MeshManager.h"
#include "Runtime/Engine.h"
#include "Component/Debug/BoneDebugComponent.h"
#include "Collision/Ray/RayUtils.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"

#include <algorithm>
#include <imgui.h>

void FMeshEditorViewportClient::Initialize(ID3D11Device* Device, uint32 Width, uint32 Height)
{
	Viewport = new FViewport();
	Viewport->Initialize(Device, Width, Height);
	Viewport->SetClient(this);

	bIsRenderable = true;
}


void FMeshEditorViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SelectedMesh);
	Collector.AddReferencedObject(Gizmo);
	Collector.AddReferencedObject(PreviewMeshComponent);
	Collector.AddReferencedObject(BoneDebugComponent);
	for (auto& Pair : SocketPreviewMeshes)
	{
		Collector.AddReferencedObject(Pair.second);
	}
	Collector.AddReferencedObject(PreviewWorld);
	Collector.AddReferencedObject(PreviewActor);
}

void FMeshEditorViewportClient::Release()
{
	if (Viewport)
	{
		Viewport->Release();
		delete Viewport;
		Viewport = nullptr;
	}

	ClearSocketPreviewMeshes();

	PreviewWorld = nullptr;
	PreviewActor = nullptr;

	UObjectManager::Get().DestroyObject(Gizmo);
	Gizmo = nullptr;
	BoneDebugComponent = nullptr;

	bIsRenderable = false;

	SetSelectedBone(nullptr, -1);
}

void FMeshEditorViewportClient::CreatePreviewGizmo()
{
	Gizmo = UObjectManager::Get().CreateObject<UGizmoComponent>();
	Gizmo->SetScene(&PreviewWorld->GetScene());
	Gizmo->CreateRenderState();
	Gizmo->Deactivate();

	SocketTarget.SetOnSocketChanged([this](const FName&)
	{
		bSocketTransformEdited = true;
	});
}

void FMeshEditorViewportClient::CreateBoneDebugComponent()
{
	BoneDebugComponent = PreviewActor->AddComponent<UBoneDebugComponent>();
	BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
	BoneDebugComponent->SetSelectedBoneIndex(SelectedBoneIndex);
	BoneDebugComponent->CreateRenderState();
}


void FMeshEditorViewportClient::SetSocketPreviewMesh(const FName& SocketName, const FString& StaticMeshPath)
{
	if (SocketName == FName::None || !PreviewActor || !PreviewMeshComponent)
	{
		return;
	}

	if (StaticMeshPath.empty() || StaticMeshPath == "None")
	{
		ClearSocketPreviewMesh(SocketName);
		return;
	}

	UStaticMeshComponent* PreviewComp = FindSocketPreviewMesh(SocketName);
	if (!PreviewComp)
	{
		PreviewComp = PreviewActor->AddComponent<UStaticMeshComponent>();
		PreviewComp->AttachToComponent(PreviewMeshComponent, SocketName);
		PreviewComp->SetRelativeTransform(FTransform());
		SocketPreviewMeshes[SocketName] = PreviewComp;
	}

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	UStaticMesh* StaticMesh = FMeshManager::LoadStaticMesh(StaticMeshPath, Device);
	PreviewComp->SetStaticMesh(StaticMesh);
	PreviewComp->MarkTransformDirty();
}

void FMeshEditorViewportClient::ClearSocketPreviewMesh(const FName& SocketName)
{
	auto It = SocketPreviewMeshes.find(SocketName);
	if (It == SocketPreviewMeshes.end())
	{
		return;
	}

	if (PreviewActor && IsValid(It->second))
	{
		PreviewActor->RemoveComponent(It->second);
	}
	SocketPreviewMeshes.erase(It);
}

void FMeshEditorViewportClient::ClearSocketPreviewMeshes()
{
	TArray<FName> Names;
	Names.reserve(SocketPreviewMeshes.size());
	for (auto& Pair : SocketPreviewMeshes)
	{
		Names.push_back(Pair.first);
	}

	for (const FName& Name : Names)
	{
		ClearSocketPreviewMesh(Name);
	}
}

void FMeshEditorViewportClient::RefreshSocketPreviewMeshes(USkeletalMesh* Mesh)
{
	ClearSocketPreviewMeshes();
	if (!Mesh)
	{
		return;
	}

	USkeleton* Skeleton = Mesh->GetSkeleton();
	if (!Skeleton)
	{
		return;
	}

	for (const FSkeletonSocket& Socket : Skeleton->GetSockets())
	{
		if (!Socket.PreviewStaticMeshPath.empty() && Socket.PreviewStaticMeshPath != "None")
		{
			SetSocketPreviewMesh(Socket.Name, Socket.PreviewStaticMeshPath);
		}
	}
}

UStaticMeshComponent* FMeshEditorViewportClient::FindSocketPreviewMesh(const FName& SocketName) const
{
	auto It = SocketPreviewMeshes.find(SocketName);
	return It != SocketPreviewMeshes.end() ? It->second : nullptr;
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

void FMeshEditorViewportClient::SetViewportRect(float X, float Y, float Width, float Height)
{
	ViewportScreenRect = { X, Y, Width, Height };
	if (Width > 0.0f && Height > 0.0f)
	{
		ViewTransform.AspectRatio = Width / Height;
	}
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
	SelectedBoneIndex = BoneIndex;
	SelectedSocketName = FName::None;
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
	}
}

void FMeshEditorViewportClient::SetSelectedSocket(USkeletalMesh* Mesh, const FName& SocketName)
{
	SelectedMesh = Mesh;
	SelectedSocketName = SocketName;

	int32 SocketBoneIndex = -1;
	if (Mesh)
	{
		if (USkeleton* Skeleton = Mesh->GetSkeleton())
		{
			if (const FSkeletonSocket* Socket = Skeleton->FindSocket(SocketName))
			{
				SocketBoneIndex = Skeleton->ResolveSocketBoneIndex(*Socket);
			}
		}
	}

	SelectedBoneIndex = SocketBoneIndex;
	RenderOptions.WeightBoneHeatMapBoneIndex = SocketBoneIndex;

	if (Gizmo && PreviewMeshComponent && SocketName != FName::None)
	{
		SocketTarget.SetSocket(PreviewMeshComponent, SocketName);
		Gizmo->SetTarget(&SocketTarget);
	}
	else if (Gizmo)
	{
		Gizmo->Deactivate();
	}

	if (BoneDebugComponent)
	{
		BoneDebugComponent->SetTargetMeshComponent(PreviewMeshComponent);
		BoneDebugComponent->SetSelectedBoneIndex(SocketBoneIndex);
	}
}

bool FMeshEditorViewportClient::ConsumeSocketTransformEdited()
{
	const bool bEdited = bSocketTransformEdited;
	bSocketTransformEdited = false;
	return bEdited;
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

	if (InputSystem::Get().GetKeyDown('F'))
	{
		if (const FBone* SelectedBone = GetSelectedBone())
		{
			FVector TargetLoc = PreviewMeshComponent->GetBoneLocationByIndex(SelectedBoneIndex);

			FVector OriginalLoc = ViewTransform.ViewLocation;
			FRotator OriginalRot = ViewTransform.ViewRotation;

			FBoundingBox Bounds = PreviewMeshComponent->GetWorldBoundingBox();
			FVector Center = Bounds.GetCenter();
			float Radius = Bounds.GetExtent().Length();

			if (Radius < 0.1f)
			{
				Radius = 1.0f;
			}

			float FocusDistance = Radius;
			FVector CameraForward = ViewTransform.ViewRotation.GetForwardVector();
			FVector NewCameraLoc = TargetLoc - CameraForward * FocusDistance;

			ViewTransform.ViewLocation = NewCameraLoc;
			ViewTransform.LookAt(TargetLoc);
			FRotator TargetRot = ViewTransform.ViewRotation;

			ViewTransform.ViewLocation = OriginalLoc;
			ViewTransform.ViewRotation = OriginalRot;

			bIsFocusAnimating = true;
			FocusAnimTimer = 0.0f;
			FocusStartLoc = OriginalLoc;
			FocusStartRot = OriginalRot;
			FocusEndLoc = NewCameraLoc;
			FocusEndRot = TargetRot;
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

	if (Input.GetKey(VK_RBUTTON) && !Input.GetKey(VK_CONTROL))
	{
		float DeltaX = static_cast<float>(Input.MouseDeltaX());
		float DeltaY = static_cast<float>(Input.MouseDeltaY());

		MouseRotation.Y += DeltaX * MouseRotationSpeed;
		MouseRotation.Z += DeltaY * MouseRotationSpeed;
	}

	Rotation *= DeltaTime;
	ViewTransform.Rotate(Rotation.Y + MouseRotation.Y, Rotation.Z + MouseRotation.Z);

	if (Input.GetKeyUp(VK_SPACE))
	{
		Gizmo->SetNextMode();
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


	ImVec2 MousePos = ImGui::GetIO().MousePos;
	float LocalMouseX = MousePos.x - ViewportScreenRect.X;
	float LocalMouseY = MousePos.y - ViewportScreenRect.Y;

	float VPWidth = (std::max)(ViewportScreenRect.Width, 1.0f);
	float VPHeight = (std::max)(ViewportScreenRect.Height, 1.0f);

	FMinimalViewInfo POV;
	GetCameraView(POV);
	POV.AspectRatio = VPWidth / VPHeight;
	FRay Ray = POV.DeprojectScreenToWorld(LocalMouseX, LocalMouseY, VPWidth, VPHeight);
	FHitResult HitResult;

	FRayUtils::RaycastComponent(Gizmo, Ray, HitResult);

	InputSystem& Input = InputSystem::Get();

	if (Input.GetKey(VK_CONTROL)
		&& (Input.GetKeyDown(VK_RBUTTON) || Input.GetKey(VK_RBUTTON) || Input.GetKeyUp(VK_RBUTTON))
		&& PreviewRagdollInteractionHandler)
	{
		if (PreviewRagdollInteractionHandler(
			Ray,
			Input.GetKeyDown(VK_RBUTTON),
			Input.GetKey(VK_RBUTTON),
			Input.GetKeyUp(VK_RBUTTON),
			DeltaTime))
		{
			return;
		}
	}

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

void FMeshEditorViewportClient::HandleDragStart(const FRay& Ray)
{
	FHitResult Hit;
	if (FRayUtils::RaycastComponent(Gizmo, Ray, Hit))
	{
		Gizmo->SetPressedOnHandle(true);
		return;
	}

	if (PreviewPickHandler)
	{
		PreviewPickHandler(Ray);
	}
}
