#pragma once

#include "Object.h"
#include "SceneComponent.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "ImGui/imgui.h"
#include "Ray.h"
#include "Editor/EditorConfig.h"

class UCameraComponent : public USceneComponent {
	DECLARE_OBJECT(UCameraComponent, USceneComponent)
public:
	float AspectRatio = 16.0f / 9.0f;
	float NearPlane = 0.01f;
	float FarPlane = 2000.0f;
	float ScreenWidth = 0.f;
	float ScreenHeight = 0.f;

	float MoveSpeed = 10.0f;
	float RotateSensitivity = 0.5f;

	float Fov = 90.0f;
	bool IsOrthographic = false;
	float OrthoHeight = 10.0f;

	float FocusLength = 5.0f;
	FVector FocusPoint;

	FVector Direction = FVector(1.0f, 0.0f, 0.0f);
	FVector SideDirection = FVector(0.0f, 1.0f, 0.0f);
	FVector UpDirection = FVector(0.0f, 0.0f, 1.0f);

	FMatrix View;
	FMatrix Projection;
	FMatrix ViewProjection;

public:
	UCameraComponent();
	virtual ~UCameraComponent() override;

	static UCameraComponent* GetMainCamera();

	void Update(float deltaTime) override;

	void Move(float DeltaTime);
	void HandleRotate(float DeltaTime);
	void HandleOrbitRotate(float DeltaTime);

	void OnResize(int NewWidth, int NewHeight);

	void SetFOV(const float NewFOV) { Fov = std::clamp(NewFOV, 10.0f, 170.0f); }
	float GetFOV() const { return Fov; }
	float GetAspectRatio() const { return AspectRatio; }
	void SetOrthographic(bool isOrthographic) { IsOrthographic = isOrthographic; }

	FMatrix GetViewMatrix() const { return View; }
	FMatrix GetProjectionMatrix() const { return Projection;  }
	FMatrix GetViewProjectionMatrix() const { return ViewProjection; }

	FVector WorldToScreen(const FVector& worldPos) const;
	Ray ScreenPointToRay(int x, int y);

	void SetMoveSpeed(float newSpeed) { MoveSpeed = newSpeed; }
	void SetRotateSensitivity(float newSensitivity) { RotateSensitivity = newSensitivity; }

	void ApplyConfig(const EditorConfig& config);
	void GatherConfig(EditorConfig& config);

private:
	ImGuiIO& io;
	void HandleCameraMove(float deltaTime);
	void UpdateCameraMatrices();
	static UCameraComponent* mainCamera;
};
