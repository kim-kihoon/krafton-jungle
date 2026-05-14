#pragma once

#include "Viewport/ViewportClient.h"
#include "Engine/Runtime/SceneView.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Editor/Viewport/EditorViewportClient.h"
#include "Editor/Utility/EditorUIUtils.h"
#include "Math/Utils.h"

class FSkeletalMeshPreviewScene;

class FSkeletalMeshPreviewViewportClient : public FViewportClient
{
public:
	FSkeletalMeshPreviewViewportClient();
	~FSkeletalMeshPreviewViewportClient() override = default;

	void Tick(float DeltaTime) override;
	void BuildSceneView(FSceneView& OutView) const override;

	void SetPreviewScene(FSkeletalMeshPreviewScene* InScene) { PreviewScene = InScene; }
	void SetViewportSize(float InWidth, float InHeight) override;
	FViewportCamera* GetCamera() { return &Camera; }
	const FViewportCamera* GetCamera() const { return &Camera; }

	EEditorViewportType GetViewportType() const { return ViewportType; }
	void SetViewportType(EEditorViewportType InType) { ViewportType = InType; }
	void ApplyCameraMode();

	FEditorViewportState* GetViewportState() { return State; }
	const FEditorViewportState* GetViewportState() const { return State; }
	void SetState(FEditorViewportState* InState) { State = InState; }

	void AddOrbitInput(float DeltaYaw, float DeltaPitch);
	void AddZoomInput(float Notch, float DeltaTime);
	void AddPanInput(float DeltaX, float DeltaY, float DeltaTime);
	void BeginCameraControl();
	void AddLookInput(float DeltaYaw, float DeltaPitch);
	void AddMoveInput(float Forward, float Right, float Up, float DeltaTime);
	void AdjustMoveSpeedScale(float Notch);
	void ResetCamera();

	void SetMoveSpeed(float InSpeed) { MoveSpeed = InSpeed; }
	void SetMoveSensitivity(float InSensitivity) { MoveSensitivity = InSensitivity; }
	void SetRotateSensitivity(float InSensitivity) { RotateSensitivity = InSensitivity; }
	void SetZoomSpeed(float InSpeed) { ZoomSpeed = InSpeed; }

private:
	void UpdateCameraTransform();
	void UpdateCameraRotation();

	FSkeletalMeshPreviewScene* PreviewScene = nullptr;
	FViewportCamera Camera;
	EEditorViewportType ViewportType = EVT_Perspective;
	FEditorViewportState* State = nullptr;

	FVector ViewTarget = FVector(0.0f, 0.0f, 1.0f);
	float OrbitPitch = 15.0f;
	float OrbitYaw = -45.0f;
	float OrbitDistance = 3.0f;
	float Yaw = 0.0f;
	float Pitch = 0.0f;
	float MoveSpeed = 15.0f;
	float MoveSensitivity = 1.0f;
	float RotateSensitivity = 1.0f;
	float ZoomSpeed = 15.0f;
	float MoveSpeedScale = 1.0f;
};
