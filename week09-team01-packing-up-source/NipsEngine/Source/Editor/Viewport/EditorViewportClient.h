#pragma once

#include "Render/Common/RenderTypes.h"
#include "Engine/Geometry/Ray.h"
#include "Core/CollisionTypes.h"
#include "Engine/Input/InputRouter.h"
#include "Viewport/ViewportClient.h"
#include "Editor/Input/EditorWorldController.h"
#include "Editor/Input/PIEController.h"
#include "Game/Input/GamePlayerController.h"
#include "Spatial/WorldSpatialIndex.h"
#include "Editor/Utility/EditorUIUtils.h"
#include "Engine/Viewport/ViewportCamera.h"

enum EEditorViewportType
{
	EVT_Perspective = 0,		// Perspective
	EVT_OrthoXY = 1,			// Top
	EVT_OrthoXZ = 2,			// Right
	EVT_OrthoYZ = 3,			// Back
	EVT_OrthoNegativeXY = 4,	// Bottom
	EVT_OrthoNegativeXZ = 5,	// Left
	EVT_OrthoNegativeYZ = 6,	// Front

	EVT_OrthoTop    = EVT_OrthoXY,			// TOP
	EVT_OrthoLeft   = EVT_OrthoXZ,			// Left
	EVT_OrthoFront  = EVT_OrthoNegativeYZ,	// Front
	EVT_OrthoBack   = EVT_OrthoYZ,			// Back
	EVT_OrthoBottom = EVT_OrthoNegativeXY,	// Bottom
	EVT_OrthoRight  = EVT_OrthoNegativeXZ,	// Right
	LVT_MAX = 7,
};

class UEditorEngine;
class UWorld;
class UGizmoComponent;
class FEditorSettings;
class FWindowsWindow;
class FSelectionManager;
class FSceneViewport;
class FViewportCamera;
struct FEditorViewportState;

/*
* Per-viewport camera / view mode / input / picking / gizmo.
* BuildSceneView, orthographic/perspective branching, and gizmo axis visibility
* branching all live here.
*/

class FEditorViewportClient : public FViewportClient
{
public:
	void Initialize(FWindowsWindow* InWindow, UEditorEngine* InEditor);
	UWorld* GetFocusedWorld() const { return World; }
	void SetWorld(UWorld* InWorld);
	void StartPIE(UWorld* InWorld);
	void EndPIE(UWorld* InWorld);

	// PIE 상태 (뷰포트별 독립)
	EViewportPlayState GetPlayState() const { return PlayState; }
	void SetPlayState(EViewportPlayState InState) { PlayState = InState; }

	// PIE 시작 전 카메라 상태 저장 / 정지 시 복원
	void SaveCameraSnapshot();
	void RestoreCameraSnapshot();

	void SetGizmo(UGizmoComponent* InGizmo)
	{
		Gizmo = InGizmo;
		EditorWorldController.SetGizmo(InGizmo);
	}
	void SetSettings(const FEditorSettings* InSettings) { Settings = InSettings; }
	void SetSelectionManager(FSelectionManager* InSelectionManager);

	UGizmoComponent* GetGizmo() { return Gizmo; }

	/** Override to also resize the camera. */
	void SetViewportSize(float InWidth, float InHeight) override;

	float GetMoveSpeed() { return EditorWorldController.GetMoveSpeed(); }
	void  SetMoveSpeed(float InSpeed) { EditorWorldController.SetMoveSpeed(InSpeed); }
	void  FocusSelection() { FocusPrimarySelection(); }

	// Camera lifecycle
	void CreateCamera();
	void DestroyCamera();
	void ResetCamera();
	FViewportCamera*       GetCamera()       { return bHasCamera ? &Camera : nullptr; }
	const FViewportCamera* GetCamera() const { return bHasCamera ? &Camera : nullptr; }
	// 외부에서 카메라 위치를 변경한 후 컨트롤러의 TargetLocation을 동기화할 때 호출
	void SyncCameraTarget() { EditorWorldController.ResetTargetLocation(); }

	void Tick(float DeltaTime) override;
	void LateTick(float DeltaTime);
	void BuildSceneView(FSceneView& OutView) const override;

	// Get / Set
	EEditorViewportType  GetViewportType() const          { return ViewportType; }
	void                 SetViewportType(EEditorViewportType InType) { ViewportType = InType; }

	FSceneViewport*       GetViewport()       { return Viewport; }
	const FSceneViewport* GetViewport() const { return Viewport; }
	void                  SetViewport(FSceneViewport* InViewport) { Viewport = InViewport; }
	FGamePlayerController& GetGamePlayerController() { return GamePlayerController; }
	const FGamePlayerController& GetGamePlayerController() const { return GamePlayerController; }

	FEditorViewportState*       GetViewportState()       { return State; }
	const FEditorViewportState* GetViewportState() const { return State; }
	void                        SetState(FEditorViewportState* InState) { State = InState; }

	/** Initialise camera position/orientation for the current ViewportType. */
	void ApplyCameraMode();

	/**
	 * Returns true while a drag operation (RMB or MMB) is in progress for this viewport.
	 * Used by the renderer to determine gizmo axis highlight behaviour.
	 */
	bool  IsActiveOperation() const { return EditorWorldController.IsActiveOperation(); }
	bool  IsBoxSelecting()    const { return EditorWorldController.IsBoxSelecting(); }
	POINT GetBoxSelectStart() const { return EditorWorldController.GetBoxSelectStart(); }
	POINT GetBoxSelectEnd()   const { return EditorWorldController.GetBoxSelectEnd(); }
	bool  HasPendingActorPlacement() const { return bPendingActorPlacement; }
	const FVector& GetPendingActorPlacementLocation() const { return PendingActorPlacementLocation; }
	POINT GetPendingActorPlacementPopupPos() const { return PendingActorPlacementPopupPos; }
	void  ClearPendingActorPlacement() { bPendingActorPlacement = false; }

	void SetEndPIECallback(std::function<void()> Callback) { PIEController.SetEndPIECallback(std::move(Callback)); }
	void ClearEndPIECallback()                             { PIEController.ClearEndPIECallback(); }

private:
	// ── Tick sub-steps ───────────────────────────────────────────────────────
	void TogglePIEInputCapture();

	// ── Selection helpers ────────────────────────────────────────────────────
	bool RequestActorPlacement(float X, float Y, float PopupX, float PopupY);
	void FocusPrimarySelection();

private:
	// Window / Viewport — Window is inherited from FViewportClient
	UEditorEngine*		   Editor			= nullptr;
	FSceneViewport*		   Viewport			= nullptr;

	EEditorViewportType    ViewportType		= EVT_Perspective;
	FEditorViewportState*  State			= nullptr;

	UWorld*				   World            = nullptr;
	UGizmoComponent*	   Gizmo            = nullptr;
	const FEditorSettings* Settings			= nullptr;
	FSelectionManager*     SelectionManager = nullptr;

	FViewportCamera		   Camera;
	FEditorWorldController EditorWorldController;
	FPIEController		   PIEController;
	FGamePlayerController  GamePlayerController;
	FInputRouter		   InputRouter;
	bool				   bHasCamera		= false;

	EViewportPlayState     PlayState       = EViewportPlayState::Editing;
	FCameraSnapshot        SavedCamera;
	bool                   bHasCameraSnapshot = false;

	bool  bControlLocked = false;
	bool  bPendingActorPlacement = false;
	FVector PendingActorPlacementLocation = FVector::ZeroVector;
	POINT PendingActorPlacementPopupPos = { 0, 0 };
};
