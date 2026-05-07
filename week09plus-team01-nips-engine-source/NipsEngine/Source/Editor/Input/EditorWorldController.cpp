#include "EditorWorldController.h"
#include "Editor/Selection/SelectionManager.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Engine/Component/GizmoComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Level.h"
#include "GameFramework/PrimitiveActors.h"
#include "Engine/Collision/RayCollision/RayCollision.h"
#include "Engine/Input/InputRouter.h"
#include "Math/Utils.h"
#include "Math/Vector4.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <windows.h>

namespace
{
	bool IsEditorActorSelectionTarget(const UPrimitiveComponent* Primitive)
	{
		if (Primitive == nullptr)
		{
			return false;
		}

		return Primitive->GetPrimitiveType() != EPrimitiveType::EPT_Decal;
	}
}

void FEditorWorldController::SetCamera(FViewportCamera* InCamera)
{
	if (!InCamera)
		return;
	Camera = InCamera;
	TargetLocation = InCamera->GetLocation();
	bTargetLocationInitialized = true;
}

void FEditorWorldController::SetCamera(FViewportCamera& InCamera)
{
	Camera = &InCamera;
	TargetLocation = InCamera.GetLocation();
	bTargetLocationInitialized = true;
}

void FEditorWorldController::Tick(float InDeltaTime)
{
	DeltaTime = InDeltaTime;
	if (!Camera)
		return;
	UpdateGizmoScreenScaling();
	if (!bTargetLocationInitialized)
	{
		TargetLocation = Camera->GetLocation();
		bTargetLocationInitialized = true;
	}
	constexpr float LocationLerpSpeed = 12.0f;
	const FVector   CurrentLocation = Camera->GetLocation();
	const float     LerpAlpha = MathUtil::Clamp(DeltaTime * LocationLerpSpeed, 0.0f, 1.0f);
	Camera->SetLocation(CurrentLocation + (TargetLocation - CurrentLocation) * LerpAlpha);
}

// X/Y parameters for mouse events are viewport-local pixel coordinates.
// DeltaX/DeltaY parameters are raw mouse movement deltas (pixels since last frame).
void FEditorWorldController::OnMouseMoveAbsolute(float X, float Y)
{
	if (!Gizmo || !Camera)
		return;

	// Update gizmo hover highlight each frame
	FRay       Ray = Camera->DeprojectScreenToWorld(X, Y, ViewportWidth, ViewportHeight);
	FHitResult HitResult;
	Gizmo->RaycastMesh(Ray, HitResult);
}

void FEditorWorldController::OnLeftMouseClick(float X, float Y)
{
	if (!Camera)
		return;

	if (bCtrlDown && bAltDown)
	{
		bBoxSelecting = true;
		BoxSelectStart = POINT{ static_cast<LONG>(X), static_cast<LONG>(Y) };
		BoxSelectEnd = BoxSelectStart;
		return;
	}

	FRay       Ray = Camera->DeprojectScreenToWorld(X, Y, ViewportWidth, ViewportHeight);
	FHitResult HitResult{};

	if (Gizmo && FRayCollision::RaycastComponent(Gizmo, Ray, HitResult))
	{
		Gizmo->SetPressedOnHandle(true);
		return;
	}
	else
	{
		if (Gizmo)
			Gizmo->SetPressedOnHandle(false);
	}

	// Actor selection
	if (!World || !SelectionManager)
		return;

	AActor* BestActor = nullptr;
	float   ClosestDist = FLT_MAX;

	FWorldSpatialIndex::FPrimitiveRayQueryScratch RayQueryScratch;
	TArray<UPrimitiveComponent*> CandidatePrimitives;
	TArray<float>                CandidateTs;
	World->GetSpatialIndex().RayQueryPrimitives(Ray, CandidatePrimitives, CandidateTs, RayQueryScratch);

	for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(CandidatePrimitives.size()); ++CandidateIndex)
	{
		if (CandidateTs[CandidateIndex] > ClosestDist)
		{
			break;
		}

		UPrimitiveComponent* PrimitiveComp = CandidatePrimitives[CandidateIndex];
		AActor*              Actor = (PrimitiveComp != nullptr) ? PrimitiveComp->GetOwner() : nullptr;
		if (Actor == nullptr || Actor->GetRootComponent() == nullptr || !IsEditorActorSelectionTarget(PrimitiveComp))
		{
			continue;
		}

		HitResult = {};
		if (PrimitiveComp->Raycast(Ray, HitResult) && HitResult.Distance < ClosestDist)
		{
			ClosestDist = HitResult.Distance;
			BestActor = Actor;
		}
	}

	if (!BestActor)
	{
		if (!bCtrlDown)
			SelectionManager->ClearSelection();
	}
	else
	{
		if (bCtrlDown)
			SelectionManager->ToggleSelect(BestActor);
		else
			SelectionManager->Select(BestActor);
	}
}

void FEditorWorldController::OnLeftMouseDrag(float X, float Y)
{
	if (bBoxSelecting)
	{
		if (X < 0.f || Y < 0.f || X > ViewportWidth || Y > ViewportHeight)
		{
			bBoxSelecting = false;
			return;
		}

		BoxSelectEnd = POINT{ static_cast<LONG>(X), static_cast<LONG>(Y) };
		return;
	}

	if (!Gizmo || !Camera)
		return;

	FRay Ray = Camera->DeprojectScreenToWorld(X, Y, ViewportWidth, ViewportHeight);

	// First frame of drag: arm the gizmo hold
	if (Gizmo->IsPressedOnHandle() && !Gizmo->IsHolding())
		Gizmo->SetHolding(true);

	if (Gizmo->IsHolding())
		Gizmo->UpdateDrag(Ray);
}

void FEditorWorldController::OnLeftMouseDragEnd(float X, float Y)
{
	if (bBoxSelecting)
	{
		BoxSelectEnd = POINT{ static_cast<LONG>(X), static_cast<LONG>(Y) };
		HandleBoxSelection();
		bBoxSelecting = false;
		return;
	}

	if (Gizmo)
		Gizmo->DragEnd();
}

void FEditorWorldController::OnLeftMouseButtonUp(float X, float Y)
{
	(void)X;
	(void)Y;
	if (bBoxSelecting)
	{
		bBoxSelecting = false;
		return;
	}

	// LMB released without reaching drag threshold — disarm gizmo
	if (Gizmo)
		Gizmo->SetPressedOnHandle(false);
}

void FEditorWorldController::OnRightMouseClick(float DeltaX, float DeltaY)
{
	(void)DeltaX;
	(void)DeltaY;
	// Seed Yaw/Pitch from current camera orientation so the first drag doesn't snap
	if (!Camera)
		return;
	const FVector Forward = Camera->GetForwardVector().GetSafeNormal();
	Pitch = MathUtil::RadiansToDegrees(std::asin(MathUtil::Clamp(Forward.Z, -1.f, 1.f)));
	Yaw   = MathUtil::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
}

void FEditorWorldController::OnRightMouseDrag(float DeltaX, float DeltaY)
{
	if (!Camera)
		return;

	// 수식 키(Ctrl, Alt, Shift)가 눌려 있으면 뷰포트 이동을 차단합니다.
	if (bCtrlDown || bAltDown || bShiftDown)
		return;

	if (Camera->IsOrthographic())
	{
		// Pan: scale movement proportionally to current ortho zoom level
		const float     PanScale = Camera->GetOrthoHeight() * 0.002f;
		const FVector   Right    = Camera->GetEffectiveRight();
		const FVector   Up       = Camera->GetEffectiveUp();
		TargetLocation += Right * (-DeltaX * PanScale) + Up * (DeltaY * PanScale);
	}
	else
	{
		// Accumulate yaw/pitch and rebuild rotation quaternion
		const float RotationSpeed = 0.15f * RotateSensitivity;
		Yaw   += DeltaX * RotationSpeed;
		Pitch -= DeltaY * RotationSpeed;
		Pitch  = MathUtil::Clamp(Pitch, -89.f, 89.f);
		UpdateCameraRotation();
	}
}

void FEditorWorldController::OnKeyPressed(int VK)
{
	switch (VK)
	{
	case VK_F4:
		if (OnRequestStartPIE)
			OnRequestStartPIE();
		break;
	case 'D':
		if (bCtrlDown)
		{
			if (World && SelectionManager && !SelectionManager->IsEmpty())
			{
				TArray<AActor*> DuplicatedActors;
				const TArray<AActor*>& SelectedActors = SelectionManager->GetSelectedActors();
				for (AActor* OriginalActor : SelectedActors)
				{
					if (!OriginalActor) continue;

					AActor* NewActor = Cast<AActor>(OriginalActor->Duplicate());
					if (NewActor)
					{
						NewActor->SetWorld(World);
						if (World->HasBegunPlay())
						{
							NewActor->BeginPlay();
						}
						World->GetPersistentLevel()->AddActor(NewActor);
						DuplicatedActors.push_back(NewActor);
					}
				}

				if (!DuplicatedActors.empty())
				{
					SelectionManager->ClearSelection();
					for (AActor* NewActor : DuplicatedActors)
					{
						SelectionManager->AddSelect(NewActor);
					}
				}
			}
		}
		break;
	case 'A':
		if (bCtrlDown && !bAltDown && World && SelectionManager)
		{
			SelectionManager->ClearSelection();
			if (ULevel* Level = World->GetPersistentLevel())
			{
				for (AActor* Actor : Level->GetActors())
				{
					if (Actor)
						SelectionManager->AddSelect(Actor);
				}
			}
		}
		break;
	case VK_SPACE:
		if (Gizmo)
			Gizmo->SetNextMode();
		break;
	case VK_LEFT:
	case VK_RIGHT:
	case VK_UP:
	case VK_DOWN:
		if (Camera)
		{
			const FVector Forward = Camera->GetForwardVector().GetSafeNormal();
			Pitch = MathUtil::RadiansToDegrees(std::asin(MathUtil::Clamp(Forward.Z, -1.f, 1.f)));
			Yaw   = MathUtil::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
		}
		break;
	}
}

void FEditorWorldController::OnKeyDown(int VK)
{
	if (!Camera)
		return;

	// 수식 키(Ctrl, Alt, Shift)가 눌려 있으면 뷰포트 이동을 차단합니다.
	if (bCtrlDown || bAltDown || bShiftDown)
		return;

	if (Camera->IsOrthographic())
		return; // no WASD/arrow input in ortho views

	// WASD + QE movement — scale by current camera forward/right vectors
	FVector Move = FVector(0, 0, 0);
	const float ActualMoveSpeed = MoveSpeed * MoveSensitivity;
	switch (VK)
	{
	case 'W': Move += Camera->GetForwardVector() *  ActualMoveSpeed * DeltaTime; break;
	case 'S': Move += Camera->GetForwardVector() * -ActualMoveSpeed * DeltaTime; break;
	case 'D': Move += Camera->GetRightVector()   *  ActualMoveSpeed * DeltaTime; break;
	case 'A': Move += Camera->GetRightVector()   * -ActualMoveSpeed * DeltaTime; break;
	case 'E': Move += FVector(0, 0, 1)           *  ActualMoveSpeed * DeltaTime; break;
	case 'Q': Move += FVector(0, 0, 1)           * -ActualMoveSpeed * DeltaTime; break;
	}
	TargetLocation += Move;

	// Arrow key rotation
	constexpr float AngleVelocity = 60.f;
	const float ActualRotateSpeed = AngleVelocity * RotateSensitivity;
	bool bRotationChanged = false;
	switch (VK)
	{
	case VK_LEFT:  Yaw   -= ActualRotateSpeed * DeltaTime; bRotationChanged = true; break;
	case VK_RIGHT: Yaw   += ActualRotateSpeed * DeltaTime; bRotationChanged = true; break;
	case VK_UP:    Pitch += ActualRotateSpeed * DeltaTime; bRotationChanged = true; break;
	case VK_DOWN:  Pitch -= ActualRotateSpeed * DeltaTime; bRotationChanged = true; break;
	}
	if (bRotationChanged)
	{
		Pitch = MathUtil::Clamp(Pitch, -89.f, 89.f);
		UpdateCameraRotation();
	}
}

void FEditorWorldController::OnKeyReleased(int VK)
{
	switch (VK)
	{
	case 'X':
		if (Gizmo)
			Gizmo->SetWorldSpace(!Gizmo->IsWorldSpace());
		break;
	case VK_DELETE:
		if (SelectionManager)
		{
			const TArray<AActor*> SelectedActors = SelectionManager->GetSelectedActors();
			for (AActor* Actor : SelectedActors)
			{
				if (!Actor)
					continue;
				if (UWorld* ActorWorld = Actor->GetFocusedWorld())
					ActorWorld->DestroyActor(Actor);
			}
			SelectionManager->ClearSelection();
		}
		break;
	case 'G':
		if (World)
		{
			constexpr int GridSizeX = 10;
			constexpr int GridSizeY = 10;
			constexpr int GridSizeZ = 10;
			constexpr float Spacing = 3.0f;

			for (int i = 0; i < GridSizeX; ++i)
			{
				for (int j = 0; j < GridSizeY; ++j)
				{
					for (int k = 0; k < GridSizeZ; ++k)
					{
						const float X = (i - GridSizeX * 0.5f) * Spacing;
						const float Y = (j - GridSizeY * 0.5f) * Spacing;
						const float Z = (k - GridSizeZ * 0.5f) * Spacing;
						const FVector Location(X, Y, Z);

						APointLightActor* LightActor = World->SpawnActor<APointLightActor>();
						LightActor->InitDefaultComponents();
						LightActor->SetActorLocation(Location);

						AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>();
						MeshActor->InitDefaultComponents();
						MeshActor->SetActorLocation(Location);
					}
				}
			}
		}
		break;
	case 'F':
		if (OnRequestFocusSelection)
			OnRequestFocusSelection();
		break;
	default:
		break;
	}
}

void FEditorWorldController::OnWheelScrolled(float Notch)
{
	if (!Camera || Notch == 0.f)
		return;

	if (Camera->IsOrthographic())
	{
		float NewWidth = Camera->GetOrthoHeight() - Notch * 300.f * MoveSensitivity * DeltaTime;
		Camera->SetOrthoHeight(MathUtil::Clamp(NewWidth, 0.1f, 1000.0f));
	}
	else
	{
		FVector Forward = Camera->GetForwardVector();
		FVector NewLocation = Camera->GetLocation() + Forward * Notch * ZoomSpeed;
		Camera->SetLocation(NewLocation);
		ResetTargetLocation();
	}
}

void FEditorWorldController::OnMiddleMouseDrag(float DeltaX, float DeltaY)
{
	if (!Camera)
		return;

	// 수식 키(Ctrl, Alt, Shift)가 눌려 있으면 뷰포트 이동을 차단합니다.
	if (bCtrlDown || bAltDown || bShiftDown)
		return;

	const float   PanScale = (Camera->IsOrthographic()
								 ? Camera->GetOrthoHeight() * 0.002f
								 : 20.0f) * MoveSensitivity;
	const FVector Right    = Camera->GetEffectiveRight();
	const FVector Up       = Camera->GetEffectiveUp();
	TargetLocation += Right * (-DeltaX * PanScale * DeltaTime) + Up * (DeltaY * PanScale * DeltaTime);
}

void FEditorWorldController::SetSelectionManager(FSelectionManager* InSM)
{
	if (InSM)
		SelectionManager = InSM;
	if (SelectionManager->GetGizmo())
		Gizmo = SelectionManager->GetGizmo();
}

void FEditorWorldController::SetSelectionManager(FSelectionManager& InSM)
{
	SelectionManager = &InSM;
	if (SelectionManager->GetGizmo())
		Gizmo = SelectionManager->GetGizmo();
}

void FEditorWorldController::SetGizmo(UGizmoComponent* InGizmo)
{
	if (InGizmo)
		Gizmo = InGizmo;
}

void FEditorWorldController::SetGizmo(UGizmoComponent& InGizmo) { Gizmo = &InGizmo; }

void FEditorWorldController::UpdateCameraRotation()
{
	if (!Camera)
		return;

	const float PitchRad = MathUtil::DegreesToRadians(Pitch);
	const float YawRad   = MathUtil::DegreesToRadians(Yaw);

	FVector Forward(std::cos(PitchRad) * std::cos(YawRad), std::cos(PitchRad) * std::sin(YawRad), std::sin(PitchRad));
	Forward = Forward.GetSafeNormal();

	FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	if (Right.IsNearlyZero())
		return;

	FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

	FMatrix RotMat = FMatrix::Identity;
	RotMat.SetAxes(Forward, Right, Up);

	FQuat NewRotation(RotMat);
	NewRotation.Normalize();
	Camera->SetRotation(NewRotation);
}

bool FEditorWorldController::IsActiveOperation() const
{
	return FInputRouter::GetRightDragging() || FInputRouter::GetMiddleDragging();
}

void FEditorWorldController::UpdateGizmoScreenScaling()
{
	if (!Camera || !Gizmo)
		return;

	if (Camera->IsOrthographic())
		Gizmo->ApplyScreenSpaceScalingOrtho(Camera->GetOrthoHeight());
	else
		Gizmo->ApplyScreenSpaceScaling(Camera->GetLocation());
}

bool FEditorWorldController::TryProjectWorldToViewport(const FVector& WorldPos, float& OutViewportX, float& OutViewportY, float& OutDepth) const
{
	if (!Camera)
		return false;

	const FVector4 Clip = FMatrix::Identity.TransformVector4(FVector4(WorldPos, 1.0f), Camera->GetViewProjectionMatrix());
	if (MathUtil::IsNearlyZero(Clip.W))
		return false;

	const float InvW = 1.0f / Clip.W;
	const float NdcX = Clip.X * InvW;
	const float NdcY = Clip.Y * InvW;
	const float NdcZ = Clip.Z * InvW;
	if (NdcX < -1.0f || NdcX > 1.0f || NdcY < -1.0f || NdcY > 1.0f)
		return false;

	OutViewportX = (NdcX * 0.5f + 0.5f) * ViewportWidth;
	OutViewportY = (1.0f - (NdcY * 0.5f + 0.5f)) * ViewportHeight;
	OutDepth = NdcZ;
	return true;
}

void FEditorWorldController::HandleBoxSelection()
{
	if (!SelectionManager || !World || !Camera)
		return;

	const int32 MinX = std::min(BoxSelectStart.x, BoxSelectEnd.x);
	const int32 MinY = std::min(BoxSelectStart.y, BoxSelectEnd.y);
	const int32 MaxX = std::max(BoxSelectStart.x, BoxSelectEnd.x);
	const int32 MaxY = std::max(BoxSelectStart.y, BoxSelectEnd.y);
	const int32 Width = MaxX - MinX;
	const int32 Height = MaxY - MinY;

	if (Width < 2 || Height < 2)
		return;

	if (!bShiftDown)
		SelectionManager->ClearSelection();

	TArray<UPrimitiveComponent*> CandidatePrimitives;
	World->GetSpatialIndex().FrustumQueryPrimitives(Camera->GetFrustum(), CandidatePrimitives, FrustumQueryScratch);

	std::unordered_set<AActor*> SeenActors;
	SeenActors.reserve(CandidatePrimitives.size());

	for (UPrimitiveComponent* Primitive : CandidatePrimitives)
	{
		AActor* Actor = (Primitive != nullptr) ? Primitive->GetOwner() : nullptr;
		if (!Actor || !Actor->GetRootComponent())
			continue;

		if (!SeenActors.insert(Actor).second)
			continue;

		float ViewportX = 0.f;
		float ViewportY = 0.f;
		float Depth = 0.f;
		if (!TryProjectWorldToViewport(Actor->GetActorLocation(), ViewportX, ViewportY, Depth))
			continue;

		if (Depth < 0.f || Depth > 1.f)
			continue;

		const int32 Px = static_cast<int32>(ViewportX);
		const int32 Py = static_cast<int32>(ViewportY);
		if (Px >= MinX && Px <= MaxX && Py >= MinY && Py <= MaxY)
			SelectionManager->AddSelect(Actor);
	}
}
