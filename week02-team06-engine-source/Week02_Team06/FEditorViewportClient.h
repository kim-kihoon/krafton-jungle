#pragma once

struct FViewportCameraTransform
{
public:
	FViewportCameraTransform(FVector inViewLocation, FVector inViewRotation, float inDistance)
		: ViewLocation(inViewLocation), ViewRotation(inViewRotation), Distance(inDistance), OrthoSize(1000.0f) {
	}

protected:
	FVector ViewLocation;
	// TODO: Use FRotator instead of FVector
	FVector ViewRotation;
	float Distance;
	float OrthoSize; // vertical size of orthographic view

public:
	const FVector& GetLocation() const { return ViewLocation; }
	void SetLocation(const FVector& InLocation) { ViewLocation = InLocation; }
	const FVector& GetRotation() const { return ViewRotation; }
	void SetRotation(const FVector& InRotation) { ViewRotation = InRotation; }

	const float& GetDistance() const { return Distance; }
	void SetDistance(const float& InDistance) { Distance = InDistance; }
	const FVector& GetPivotLocation() const { return ViewLocation + GetForwardVector() * Distance; }
	const float& GetOrthoSize() const { return OrthoSize; }
	void SetOrthoSize(const float& InOrthoSize) { OrthoSize = InOrthoSize; }

	FVector GetRightVector() const;
	FVector GetUpVector() const;
	FVector GetForwardVector() const;
};

class FEditorViewportClient
{
private:
	class UGizmoComponent* MainGizmo = { nullptr };

public:
	FEditorViewportClient(FVector inViewLocation, FVector inViewRotation, float inAspectRatio, float FOVAngle)
		: ViewTransform(inViewLocation, inViewRotation, inViewLocation.Length()), AspectRatio(inAspectRatio), FOVAngle(FOVAngle)
	{
		float InitialDistance = ViewTransform.GetLocation().Length(); // same as InViewLocation.Length()
		float HalfFOVRadian = Math::ToRadians(FOVAngle) / 2.0f;
		float InitialOrthoSize = 2.0f * InitialDistance * tanf(HalfFOVRadian);

		ViewTransform.SetOrthoSize(InitialOrthoSize);
	}

public:
	float AspectRatio = 1.0f; // AspectRatio = width / height
	float FOVAngle = 60.0f; // horizontal field of view

protected:
	float FarPlane = 1000.0f;
	float NearPlane = 0.1f;
	float bIsPerspective = true;
	FViewportCameraTransform ViewTransform;

public:
	FMatrix GetViewMatrix() const; // WorldToView
	FMatrix GetProjectionMatrix() const; // ViewToClip

	const FVector& GetViewLocation() const { return ViewTransform.GetLocation(); }
	void SetViewLocation(const FVector& NewLocation) { ViewTransform.SetLocation(NewLocation); }
	const FVector& GetViewRotation() const { return ViewTransform.GetRotation(); }
	void SetViewRotation(const FVector& NewRotation) { ViewTransform.SetRotation(NewRotation); }
	FViewportCameraTransform& GetViewTransform() { return ViewTransform; }

	float GetAspectRatio() const { return AspectRatio; }
	void SetAspectRatio(float InAspectRatio) { AspectRatio = InAspectRatio; }
	float GetFOVAngle() const { return FOVAngle; }
	void SetFOVAngle(float InFOVAngle) { FOVAngle = InFOVAngle; }

	float GetFarPlane() const { return FarPlane; }
	void SetFarPlane(float InFarPlane) { FarPlane = InFarPlane; }
	float GetNearPlane() const { return NearPlane; }
	void SetNearPlane(float InNearPlane) { NearPlane = InNearPlane; }
	void SetPerspective(bool bInIsPerspective);
	const bool IsPerspective() const { return bIsPerspective; };
	void DeprojectScreenToWorld(const FVector2D& ScreenPos, FVector& OutWorldOrigin, FVector& OutWorldDirection);
	// Deprecated: Use DeprojectScreenToWorld
	FVector GetCameraRayDirection();
	void Tick(float DeltaTime);

protected:
	void HandleKeyboardMovement(float DeltaTime);
	void HandleMouseRightDrag();
	void HandleMouseWheel();
	void HandleMiddleMouseDrag();
};
