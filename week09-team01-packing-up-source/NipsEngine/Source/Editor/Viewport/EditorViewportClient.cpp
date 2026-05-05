#include "Editor/Viewport/EditorViewportClient.h"

#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/Settings/EditorSettings.h"
#include "Engine/Slate/SlateApplication.h"
#include "Editor/EditorEngine.h"
#include "Game/UI/GameUISystem.h"

#include "GameFramework/World.h"
#include "Component/GizmoComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Object/Object.h"
#include "Object/ActorIterator.h"
#include "Editor/Selection/SelectionManager.h"
#include "Runtime/SceneView.h"
#include "Editor/Utility/EditorUIUtils.h"
#include "Math/Vector4.h"
#include "Slate/SWidget.h"
#include <algorithm>
#include <unordered_set>
#include "GameFramework/PrimitiveActors.h"
#include "Component/StaticMeshComponent.h"
#include "Component/CameraComponent.h"

namespace
{
	bool IsActorPlacementRaycastTarget(const UPrimitiveComponent* Primitive)
	{
		if (Primitive == nullptr)
		{
			return false;
		}

		const EPrimitiveType PrimType = Primitive->GetPrimitiveType();
		return PrimType != EPrimitiveType::EPT_Decal &&
			PrimType != EPrimitiveType::EPT_Billboard &&
			PrimType != EPrimitiveType::EPT_Text &&
			PrimType != EPrimitiveType::EPT_SubUV;
	}

	APawnActor* EnsurePlayerPawn(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}

		if (APawnActor* ExistingPawn = World->FindPawn())
		{
			ExistingPawn->EnsureDefaultComponents();
			return ExistingPawn;
		}

		APawnActor* Pawn = World->SpawnActor<APawnActor>();
		Pawn->InitDefaultComponents();
		if (APlayerStartActor* PlayerStart = World->FindPlayerStart())
		{
			Pawn->SetActorLocation(PlayerStart->GetActorLocation());
			Pawn->SetActorRotation(PlayerStart->GetActorRotation());
		}
		return Pawn;
	}

	UCameraComponent* FindPawnCamera(APawnActor* Pawn)
	{
		if (Pawn == nullptr)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Pawn->GetComponents())
		{
			if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(Component))
			{
				return CameraComponent;
			}
		}

		return nullptr;
	}
}

void FEditorViewportClient::Initialize(FWindowsWindow* InWindow, UEditorEngine* InEditor)
{
	FViewportClient::Initialize(InWindow);
	Editor = InEditor;
	EditorWorldController.SetStartPIECallback([this]() { if (Editor) Editor->StartPlaySession(); });
	EditorWorldController.SetFocusSelectionCallback([this]() { FocusPrimarySelection(); });
	PIEController.SetToggleInputCaptureCallback([this]() { TogglePIEInputCapture(); });
	GamePlayerController.SetTogglePauseCallback(&GameUISystem::TogglePauseMenuIfInGame);
	InputRouter.SetEditorWorldController(&EditorWorldController);
	InputRouter.SetPIEController(&PIEController);
	InputRouter.SetGamePlayerController(&GamePlayerController);
	InputRouter.SetUIInputHandler(&GameUISystem::Get());
}

void FEditorViewportClient::SetWorld(UWorld* InWorld)
{
	World = InWorld;
	EditorWorldController.SetWorld(InWorld);
	GamePlayerController.SetWorld(InWorld);
}

void FEditorViewportClient::StartPIE(UWorld* InWorld)
{
	World = InWorld;
	APawnActor* Pawn = EnsurePlayerPawn(InWorld);
	UCameraComponent* PawnCamera = FindPawnCamera(Pawn);
	if (PawnCamera == nullptr)
	{
		if (APlayerStartActor* PlayerStart = InWorld ? InWorld->FindPlayerStart() : nullptr)
		{
			Camera.SetProjectionType(EViewportProjectionType::Perspective);
			Camera.ClearCustomLookDir();
			Camera.SetLocation(PlayerStart->GetActorLocation());
			Camera.SetRotation(FRotator::MakeFromEuler(PlayerStart->GetActorRotation()));
		}
	}

	GamePlayerController.SetWorld(InWorld);
	GamePlayerController.SetPlayer(Pawn);
	GamePlayerController.SetCamera(PawnCamera);
	if (PawnCamera != nullptr)
	{
		GamePlayerController.SetFreeCamera(nullptr);
		InWorld->SetActiveCameraComponent(PawnCamera);
	}
	else
	{
		GamePlayerController.SetFreeCamera(&Camera);
		if (bHasCameraSnapshot)
		{
			GamePlayerController.InitializeFreeCameraFromSnapshot(SavedCamera);
		}
	}
	FInputRouter::ResetMouseDelta(2);
}

void FEditorViewportClient::EndPIE(UWorld* InWorld)
{
	World = InWorld;
	EditorWorldController.SetTargetLocation(Camera.GetLocation());
	EditorWorldController.SetWorld(InWorld);
	EditorWorldController.ResetTargetLocation();
	GamePlayerController.SetCamera(nullptr);
	GamePlayerController.SetFreeCamera(nullptr);
	GamePlayerController.SetWorld(nullptr);
	ClearEndPIECallback();
	FInputRouter::LockMouse(false);
	bControlLocked = false;
}

void FEditorViewportClient::SetSelectionManager(FSelectionManager* InSelectionManager)
{
	SelectionManager = InSelectionManager;
	EditorWorldController.SetSelectionManager(InSelectionManager);
}

void FEditorViewportClient::CreateCamera()
{
	bHasCamera = true;
	Camera = FViewportCamera();
	Camera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
	EditorWorldController.SetCamera(&Camera);
	GamePlayerController.SetFreeCamera(&Camera);
	EditorWorldController.ResetTargetLocation();
}

void FEditorViewportClient::DestroyCamera()
{
	bHasCamera = false;
	EditorWorldController.NullifyCamera();
	GamePlayerController.SetFreeCamera(nullptr);
}

void FEditorViewportClient::ResetCamera()
{
	if (!bHasCamera || !Settings)
		return;

	Camera.SetLocation(Settings->InitViewPos);

	const FVector Forward = (Settings->InitLookAt - Settings->InitViewPos).GetSafeNormal();
	if (!Forward.IsNearlyZero())
	{
		FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		if (!Right.IsNearlyZero())
		{
			FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();
			FMatrix RotationMatrix = FMatrix::Identity;
			RotationMatrix.SetAxes(Forward, Right, Up);

			FQuat NewRotation(RotationMatrix);
			NewRotation.Normalize();
			Camera.SetRotation(NewRotation);
		}
	}
	EditorWorldController.ResetTargetLocation();
}

void FEditorViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	FViewportClient::SetViewportSize(InWidth, InHeight);

	if (bHasCamera)
		Camera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
}

void FEditorViewportClient::Tick(float DeltaTime)
{
	if (State && !State->bHovered)
		return;

	if (bHasCamera && Settings)
	{
		FEditorWorldController& Controller = EditorWorldController;
		Controller.SetMoveSpeed(Settings->CameraSpeed);
		Controller.SetMoveSensitivity(Settings->CameraMoveSensitivity);
		Controller.SetRotateSensitivity(Settings->CameraRotateSensitivity);
		Controller.SetZoomSpeed(Settings->CameraZoomSpeed);
	}

	FInputRouteContext RouteContext;
	RouteContext.Window = Window;
	RouteContext.ViewportRect = Viewport ? Viewport->GetRect() : FViewportRect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	RouteContext.bHovered = State ? State->bHovered : true;
	const bool bUIWantsMouse = GameUISystem::Get().WantsMouseCursor();
	RouteContext.bControlLocked = bControlLocked || bUIWantsMouse;
	RouteContext.bInputActive = !bUIWantsMouse;
	RouteContext.bHasActiveCamera = bHasCamera;
	RouteContext.bUseCustomCursor = GameUISystem::Get().WantsCustomCursor();
	InputRouter.Tick(DeltaTime, RouteContext);
}

void FEditorViewportClient::LateTick(float DeltaTime)
{
	if (World && World->GetWorldType() == EWorldType::PIE)
	{
		GamePlayerController.LateTick(DeltaTime);
	}
}

void FEditorViewportClient::BuildSceneView(FSceneView& OutView) const
{
	if (!bHasCamera)
		return;

	if (World && World->GetWorldType() == EWorldType::PIE)
	{
		const FViewportRect Rect = State && Viewport
									   ? Viewport->GetRect()
									   : FViewportRect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
		GamePlayerController.BuildSceneView(OutView, Rect, State ? State->ViewMode : EViewMode::Lit);
		return;
	}

	OutView.ViewMatrix = Camera.GetViewMatrix();
	OutView.ProjectionMatrix = Camera.GetProjectionMatrix();
	OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

	OutView.CameraPosition = Camera.GetLocation();
	OutView.CameraForward = Camera.GetForwardVector();
	OutView.CameraRight = Camera.GetRightVector();
	OutView.CameraUp = Camera.GetUpVector();

	OutView.NearPlane = Camera.GetNearPlane();
	OutView.FarPlane = Camera.GetFarPlane();

	OutView.bOrthographic = Camera.IsOrthographic();

	OutView.CameraOrthoHeight = Camera.GetOrthoHeight();

	OutView.CameraFrustum = Camera.GetFrustum();

	if (State)
	{
		OutView.ViewRect = Viewport->GetRect();
		OutView.ViewMode = State->ViewMode;
	}
}

void FEditorViewportClient::ApplyCameraMode()
{
	// Orthographic views reset rotation so the existing value doesn't interfere with LookAt.
	Camera.SetRotation(FRotator(0.f, 0.f, 0.f));

	switch (ViewportType)
	{
	case EVT_Perspective:
		Camera.SetProjectionType(EViewportProjectionType::Perspective);
		Camera.ClearCustomLookDir();
		Camera.SetLocation(FVector(5.f, 3.f, 5.f));
		Camera.SetLookAt(FVector(0.f, 0.f, 0.f));
		break;

		// Orthographic views (X=Forward, Y=Right, Z=Up)

	case EVT_OrthoTop: // top-down (-Z), screen-up = +X
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(0.f, 0.f, 1000.f));
		Camera.SetCustomLookDir(FVector(0.f, 0.f, -1.f), FVector(1.f, 0.f, 0.f));
		break;

	case EVT_OrthoBottom: // bottom-up (+Z), screen-up = +X
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(0.f, 0.f, -1000.f));
		Camera.SetCustomLookDir(FVector(0.f, 0.f, 1.f), FVector(1.f, 0.f, 0.f));
		break;

	case EVT_OrthoFront: // front (-X->+X), screen-up = +Z
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(1000.f, 0.f, 0.f));
		Camera.SetCustomLookDir(FVector(-1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoBack: // back (+X->-X), screen-up = +Z
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(-1000.f, 0.f, 0.f));
		Camera.SetCustomLookDir(FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoLeft: // left (-Y->+Y), screen-up = +Z
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(0.f, -1000.f, 0.f));
		Camera.SetCustomLookDir(FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	case EVT_OrthoRight: // right (+Y->-Y), screen-up = +Z
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(FVector(0.f, 1000.f, 0.f));
		Camera.SetCustomLookDir(FVector(0.f, -1.f, 0.f), FVector(0.f, 0.f, 1.f));
		break;

	default:
		break;
	}

	// Reset lerp target immediately so accumulated TargetLocation doesn't
	// move the camera on the next Tick after a mode switch.
	EditorWorldController.ResetTargetLocation();
}

// ── Input tick sub-steps ──────────────────────────────────────────────────────

void FEditorViewportClient::TogglePIEInputCapture()
{
	if (!bControlLocked)
	{
		bControlLocked = true;
		FInputRouter::SetCursorVisibility(true);
		FInputRouter::LockMouse(false);
		FInputRouter::ResetMouseDelta(1);
	}
	else
	{
		bControlLocked = false;
		FInputRouter::ResetMouseDelta(2);
	}
}

bool FEditorViewportClient::RequestActorPlacement(float X, float Y, float PopupX, float PopupY)
{
	if (!World || !bHasCamera)
		return false;

	FRay Ray = Camera.DeprojectScreenToWorld(X, Y, WindowWidth, WindowHeight);

	FHitResult BestHit{};
	bool bHasHit = false;
	float ClosestDist = FLT_MAX;

	FWorldSpatialIndex::FPrimitiveRayQueryScratch RayQueryScratch;
	TArray<UPrimitiveComponent*> CandidatePrimitives;
	TArray<float> CandidateTs;
	World->GetSpatialIndex().RayQueryPrimitives(Ray, CandidatePrimitives, CandidateTs, RayQueryScratch);

	for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(CandidatePrimitives.size()); ++CandidateIndex)
	{
		if (CandidateTs[CandidateIndex] > ClosestDist)
			break;

		UPrimitiveComponent* PrimitiveComp = CandidatePrimitives[CandidateIndex];
		if (!IsActorPlacementRaycastTarget(PrimitiveComp))
			continue;

		FHitResult HitResult{};
		if (PrimitiveComp->Raycast(Ray, HitResult) && HitResult.Distance < ClosestDist)
		{
			ClosestDist = HitResult.Distance;
			BestHit = HitResult;
			bHasHit = true;
		}
	}

	if (!bHasHit)
		return false;

	PendingActorPlacementLocation = BestHit.Location + BestHit.Normal;
	PendingActorPlacementPopupPos = { static_cast<LONG>(PopupX), static_cast<LONG>(PopupY) };
	bPendingActorPlacement = true;
	return true;
}

void FEditorViewportClient::FocusPrimarySelection()
{
	if (!SelectionManager || !bHasCamera)
		return;

	AActor* Primary = SelectionManager->GetPrimarySelection();
	if (!Primary)
		return;

	const FVector Target = Primary->GetActorLocation();

	if (Camera.IsOrthographic())
	{
		const FVector Forward = Camera.GetEffectiveForward().GetSafeNormal();
		float Distance = FVector::DotProduct(Camera.GetLocation() - Target, Forward);
		if (MathUtil::IsNearlyZero(Distance))
			Distance = 1000.f;
		Camera.SetLocation(Target + Forward * Distance);
	}
	else
	{
		const FVector Forward = Camera.GetForwardVector().GetSafeNormal();
		Camera.SetLocation(Target - Forward * 5.f);
		Camera.SetLookAt(Target);
	}

	EditorWorldController.ResetTargetLocation();
}

void FEditorViewportClient::SaveCameraSnapshot()
{
	const FViewportCamera* SnapshotSource = GetCamera();
	if (!SnapshotSource)
	{
		bHasCameraSnapshot = false;
		return;
	}

	SavedCamera.Location = SnapshotSource->GetLocation();
	SavedCamera.Rotation = SnapshotSource->GetRotation();
	SavedCamera.ProjectionType = SnapshotSource->GetProjectionType();
	SavedCamera.Width = SnapshotSource->GetWidth();
	SavedCamera.Height = SnapshotSource->GetHeight();
	SavedCamera.FOV = SnapshotSource->GetFOV();
	SavedCamera.NearPlane = SnapshotSource->GetNearPlane();
	SavedCamera.FarPlane = SnapshotSource->GetFarPlane();
	SavedCamera.OrthoHeight = SnapshotSource->GetOrthoHeight();
	bHasCameraSnapshot = true;
}

void FEditorViewportClient::RestoreCameraSnapshot()
{
	if (!bHasCamera || !bHasCameraSnapshot)
	{
		return;
	}

	Camera.ClearCustomLookDir();
	Camera.SetLocation(SavedCamera.Location);
	Camera.SetRotation(SavedCamera.Rotation);
	Camera.SetProjectionType(SavedCamera.ProjectionType);
	Camera.OnResize(SavedCamera.Width, SavedCamera.Height);
	Camera.SetFOV(SavedCamera.FOV);
	Camera.SetNearPlane(SavedCamera.NearPlane);
	Camera.SetFarPlane(SavedCamera.FarPlane);
	Camera.SetOrthoHeight(SavedCamera.OrthoHeight);
	EditorWorldController.ResetTargetLocation();
}
