#pragma once
#include "Editor/Input/BaseEditorController.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Spatial/WorldSpatialIndex.h"
#include <functional>
#include <windows.h>

class FSelectionManager;
class UGizmoComponent;
class UWorld;

// Note: Editor Viewport uses FViewportCamera
class FEditorWorldController : public IBaseEditorController
{
  public:
	void Tick(float InDeltaTime) override;
	void OnMouseMove(float DeltaX, float DeltaY) override {}
	void OnMouseMoveAbsolute(float X, float Y) override;
	void OnLeftMouseClick(float X, float Y) override;    // LMB down
	void OnLeftMouseDragEnd(float X, float Y) override;  // LMB drag released
	void OnLeftMouseButtonUp(float X, float Y) override; // LMB up (no drag)
	void OnRightMouseClick(float DeltaX, float DeltaY) override;
	void OnLeftMouseDrag(float X, float Y) override; // X/Y = viewport-local pos
	void OnRightMouseDrag(float DeltaX, float DeltaY) override;
	void OnMiddleMouseDrag(float DeltaX, float DeltaY) override;
	void OnKeyPressed(int VK) override;
	void OnKeyDown(int VK) override;
	void OnKeyReleased(int VK) override;
	void OnWheelScrolled(float Notch) override;

	void SetSelectionManager(FSelectionManager* InSM);
	void SetSelectionManager(FSelectionManager& InSM);
	void NullifySelectionManager()
	{
		SelectionManager = nullptr;
		Gizmo = nullptr;
	}
	void SetGizmo(UGizmoComponent* InGizmo);
	void SetGizmo(UGizmoComponent& InGizmo);
	void NullifyGizmo() { Gizmo = nullptr; }
	void SetCamera(FViewportCamera* InCamera);
	void SetCamera(FViewportCamera& InCamera);
	void NullifyCamera() { Camera = nullptr; }

	float GetMoveSpeed() const { return MoveSpeed; }
	void  SetMoveSpeed(float InSpeed) { MoveSpeed = InSpeed; }
	float GetMoveSensitivity() const { return MoveSensitivity; }
	void  SetMoveSensitivity(float InSens) { MoveSensitivity = InSens; }
	float GetRotateSensitivity() const { return RotateSensitivity; }
	void  SetRotateSensitivity(float InSens) { RotateSensitivity = InSens; }
	float GetZoomSpeed() const { return ZoomSpeed; }
	void  SetZoomSpeed(float InSpeed) { ZoomSpeed = InSpeed; }
	FVector GetTargetLocation() const { return TargetLocation; }
	void  SetTargetLocation(FVector InTargetLoc) { TargetLocation = InTargetLoc; }
	void  ResetTargetLocation()
	{
		if (Camera)
			TargetLocation = Camera->GetLocation();
	}
	void SetWorld(UWorld* InWorld)
	{
		if (InWorld)
			World = InWorld;
	}
	void NullifyWorld() { World = nullptr; }
	void SetStartPIECallback(std::function<void()> Callback) { OnRequestStartPIE = std::move(Callback); }
	void ClearStartPIECallback() { OnRequestStartPIE = nullptr; }
	void SetFocusSelectionCallback(std::function<void()> Callback) { OnRequestFocusSelection = std::move(Callback); }
	void ClearFocusSelectionCallback() { OnRequestFocusSelection = nullptr; }

	bool  IsActiveOperation() const;
	bool  IsBoxSelecting() const { return bBoxSelecting; }
	POINT GetBoxSelectStart() const { return BoxSelectStart; }
	POINT GetBoxSelectEnd() const { return BoxSelectEnd; }

  private:
	void UpdateCameraRotation();
	void UpdateGizmoScreenScaling();
	void HandleBoxSelection();
	bool TryProjectWorldToViewport(const FVector& WorldPos, float& OutViewportX, float& OutViewportY, float& OutDepth) const;

  private:
	FSelectionManager* SelectionManager = nullptr;
	FViewportCamera*   Camera = nullptr;
	UGizmoComponent*   Gizmo = nullptr;
	UWorld*            World = nullptr;

	float   Yaw   = 0.f;
	float   Pitch = 0.f;
	float   MoveSpeed = 15.f;
	float   MoveSensitivity = 1.0f;
	float   RotateSensitivity = 1.0f;
	float   ZoomSpeed = 15.0f;
	FVector TargetLocation;
	bool    bTargetLocationInitialized = false;
	std::function<void()> OnRequestStartPIE;
	std::function<void()> OnRequestFocusSelection;

	bool  bBoxSelecting = false;
	POINT BoxSelectStart = { 0, 0 };
	POINT BoxSelectEnd = { 0, 0 };
	FWorldSpatialIndex::FPrimitiveFrustumQueryScratch FrustumQueryScratch;
};
