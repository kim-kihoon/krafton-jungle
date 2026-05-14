#pragma once

#include "Input/IInputController.h"

class FSkeletalMeshPreviewViewportClient;
class FSkeletalMeshPreviewScene;

class FSkeletalMeshPreviewController : public IInputController
{
public:
	FSkeletalMeshPreviewController() = default;
	~FSkeletalMeshPreviewController() override = default;

	void SetViewportClient(FSkeletalMeshPreviewViewportClient* InViewportClient);

	void Tick(float InDeltaTime) override;
	void OnMouseMove(float DeltaX, float DeltaY) override;
	void OnMouseMoveAbsolute(float X, float Y) override;
	void OnLeftMouseClick(float X, float Y) override;
	void OnLeftMouseDragEnd(float X, float Y) override;
	void OnLeftMouseButtonUp(float X, float Y) override;
	void OnRightMouseClick(float DeltaX, float DeltaY) override;
	void OnLeftMouseDrag(float X, float Y) override;
	void OnRightMouseDrag(float DeltaX, float DeltaY) override;
	void OnMiddleMouseDrag(float DeltaX, float DeltaY) override;
	void OnKeyPressed(int VK) override;
	void OnKeyDown(int VK) override;
	void OnKeyReleased(int VK) override;
	void OnWheelScrolled(float Notch) override;

	void OnRightMouseButtonUp();

	void SetPreviewScene(FSkeletalMeshPreviewScene* InPreviewScene) { PreviewScene = InPreviewScene; }

private:
	FSkeletalMeshPreviewViewportClient* ViewportClient = nullptr;
	FSkeletalMeshPreviewScene* PreviewScene = nullptr;

	bool bIsRMBDown = false;
	bool bMoveForward = false;
	bool bMoveBackward = false;
	bool bMoveRight = false;
	bool bMoveLeft = false;
	bool bMoveUp = false;
	bool bMoveDown = false;
};
