#include "Editor/Input/SkeletalMeshPreviewController.h"

#include "Component/GizmoComponent.h"
#include "Editor/Viewport/SkeletalMeshPreviewViewportClient.h"
#include "Engine/Input/InputRouter.h"
#include "SkeletalMesh/SkeletalMeshPreviewScene.h"

namespace
{
constexpr int VK_W = 'W';
constexpr int VK_S = 'S';
constexpr int VK_A = 'A';
constexpr int VK_D = 'D';
constexpr int VK_Q = 'Q';
constexpr int VK_E = 'E';

FRay MakeCameraOriginRay(FViewportCamera* Camera, float X, float Y, float ViewportWidth, float ViewportHeight)
{
	FRay Ray = Camera->DeprojectScreenToWorld(X, Y, ViewportWidth, ViewportHeight);
	if (Camera->IsOrthographic())
	{
		return Ray;
	}

	const FVector NearPoint = Ray.Origin;
	Ray.Origin = Camera->GetLocation();
	Ray.Direction = (NearPoint - Ray.Origin).GetSafeNormal();
	if (Ray.Direction.IsNearlyZero())
	{
		Ray.Direction = Camera->GetForwardVector().GetSafeNormal();
	}
	return Ray;
}
}

void FSkeletalMeshPreviewController::SetViewportClient(FSkeletalMeshPreviewViewportClient* InViewportClient)
{
	ViewportClient = InViewportClient;
}

void FSkeletalMeshPreviewController::Tick(float InDeltaTime)
{
	IInputController::Tick(InDeltaTime);

	if (!FInputRouter::GetKey(VK_RBUTTON))
	{
		bIsRMBDown = false;
	}

	if (!ViewportClient || !bInputEnabled || !bIsRMBDown)
	{
		return;
	}

	float Forward = 0.0f;
	float Right = 0.0f;
	float Up = 0.0f;

	if (bMoveForward) { Forward += 1.0f; }
	if (bMoveBackward) { Forward -= 1.0f; }
	if (bMoveRight) { Right += 1.0f; }
	if (bMoveLeft) { Right -= 1.0f; }
	if (bMoveUp) { Up += 1.0f; }
	if (bMoveDown) { Up -= 1.0f; }

	if (Forward != 0.0f || Right != 0.0f || Up != 0.0f)
	{
		ViewportClient->AddMoveInput(Forward, Right, Up, DeltaTime);
	}
}

void FSkeletalMeshPreviewController::OnRightMouseClick(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;

	bIsRMBDown = true;
	if (ViewportClient)
	{
		ViewportClient->BeginCameraControl();
	}
}

void FSkeletalMeshPreviewController::OnRightMouseButtonUp()
{
	bIsRMBDown = false;
}

void FSkeletalMeshPreviewController::OnRightMouseDrag(float DeltaX, float DeltaY)
{
	if (!ViewportClient || bCtrlDown || bAltDown || bShiftDown)
	{
		return;
	}

	ViewportClient->AddLookInput(DeltaX, DeltaY);
}

void FSkeletalMeshPreviewController::OnMiddleMouseDrag(float DeltaX, float DeltaY)
{
	if (!ViewportClient || bCtrlDown || bAltDown || bShiftDown)
	{
		return;
	}

	ViewportClient->AddPanInput(DeltaX, DeltaY, DeltaTime);
}

void FSkeletalMeshPreviewController::OnWheelScrolled(float Notch)
{
	if (!ViewportClient || Notch == 0.0f)
	{
		return;
	}

	if (bIsRMBDown)
	{
		ViewportClient->AdjustMoveSpeedScale(Notch);
	}
	else
	{
		ViewportClient->AddZoomInput(Notch, DeltaTime);
	}
}

void FSkeletalMeshPreviewController::OnKeyDown(int VK)
{
	if (bCtrlDown || bAltDown || bShiftDown)
	{
		return;
	}

	if (!bIsRMBDown && PreviewScene && PreviewScene->GetPreviewGizmo())
	{
		UGizmoComponent* Gizmo = PreviewScene->GetPreviewGizmo();
		if (VK == 'W') { Gizmo->SetTranslateMode(); return; }
		if (VK == 'E') { Gizmo->SetRotateMode(); return; }
		if (VK == 'R') { Gizmo->SetScaleMode(); return; }
	}

	switch (VK)
	{
	case VK_W: bMoveForward = true; break;
	case VK_S: bMoveBackward = true; break;
	case VK_A: bMoveLeft = true; break;
	case VK_D: bMoveRight = true; break;
	case VK_E: bMoveUp = true; break;
	case VK_Q: bMoveDown = true; break;
	default: break;
	}
}

void FSkeletalMeshPreviewController::OnKeyReleased(int VK)
{
	switch (VK)
	{
	case VK_W: bMoveForward = false; break;
	case VK_S: bMoveBackward = false; break;
	case VK_A: bMoveLeft = false; break;
	case VK_D: bMoveRight = false; break;
	case VK_E: bMoveUp = false; break;
	case VK_Q: bMoveDown = false; break;
	default: break;
	}
}

void FSkeletalMeshPreviewController::OnMouseMove(float DeltaX, float DeltaY) { (void)DeltaX; (void)DeltaY; }

void FSkeletalMeshPreviewController::OnMouseMoveAbsolute(float X, float Y) 
{ 
	if (!ViewportClient || !PreviewScene) return;

	UGizmoComponent* GizmoComponent = PreviewScene->GetPreviewGizmo();
	if (!GizmoComponent || !GizmoComponent->HasTarget()) return;

	if (!bIsRMBDown)
	{
	    if (FViewportCamera* Camera = ViewportClient->GetCamera())
	    {
			FRay Ray = MakeCameraOriginRay(Camera, X, Y, ViewportWidth, ViewportHeight);
			FHitResult HitResult;
			GizmoComponent->RaycastMesh(Ray, HitResult);
	    }
	}
}

void FSkeletalMeshPreviewController::OnLeftMouseClick(float X, float Y) 
{ 
	if (!ViewportClient || !PreviewScene) return;

	UGizmoComponent* Gizmo = PreviewScene->GetPreviewGizmo();

	if (FViewportCamera* Camera = ViewportClient->GetCamera())
	{
		FRay Ray = MakeCameraOriginRay(Camera, X, Y, ViewportWidth, ViewportHeight);
		FHitResult HitResult;

		// 기즈모를 맞췄다면 조작 시작
		if (Gizmo && Gizmo->HasTarget() && Gizmo->RaycastMesh(Ray, HitResult))
		{
			Gizmo->SetPressedOnHandle(true);
			return; 
		}

		if (Gizmo)
		{
			Gizmo->SetPressedOnHandle(false);
		}

		int32 PickedBoneIndex = -1;
		if (PreviewScene->IsFullSkeletonVisible() && PreviewScene->PickBoneJoint(Ray, PickedBoneIndex))
		{
			PreviewScene->SelectBone(PickedBoneIndex);
			return;
		}

		PreviewScene->SelectBone(-1);
	}
}

void FSkeletalMeshPreviewController::OnLeftMouseDragEnd(float X, float Y) 
{ 
	if (PreviewScene && PreviewScene->GetPreviewGizmo())
	{
		PreviewScene->GetPreviewGizmo()->DragEnd();
	}
}

void FSkeletalMeshPreviewController::OnLeftMouseButtonUp(float X, float Y) 
{ 
	if (PreviewScene && PreviewScene->GetPreviewGizmo())
	{
		PreviewScene->GetPreviewGizmo()->DragEnd();
	}
}

void FSkeletalMeshPreviewController::OnLeftMouseDrag(float X, float Y) 
{ 
	if (!ViewportClient || !PreviewScene) return;

	UGizmoComponent* Gizmo = PreviewScene->GetPreviewGizmo();
	if (!Gizmo || !Gizmo->HasTarget()) return;

	if (Gizmo->IsPressedOnHandle() && !Gizmo->IsHolding())
	{
		Gizmo->SetHolding(true);
	}

	if (Gizmo->IsHolding())
	{
		if (FViewportCamera* Camera = ViewportClient->GetCamera())
		{
			FRay Ray = MakeCameraOriginRay(Camera, X, Y, ViewportWidth, ViewportHeight);
			Gizmo->UpdateDrag(Ray);
		}
	}
}

void FSkeletalMeshPreviewController::OnKeyPressed(int VK)
{
	if (VK == 0x20 
		&& !bIsRMBDown
		&& PreviewScene && PreviewScene->GetPreviewGizmo())
	{
		PreviewScene->GetPreviewGizmo()->SetNextMode();
		return;
	}
}
