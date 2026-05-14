#include "Editor/Viewport/SkeletalMeshPreviewViewportClient.h"

namespace
{
bool BuildRotationFromForward(const FVector& InForward, FQuat& OutRotation)
{
	FVector Forward = InForward.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return false;
	}

	FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector;
	}

	FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();
	FMatrix RotationMatrix = FMatrix::Identity;
	RotationMatrix.SetAxes(Forward, Right, Up);

	OutRotation = FQuat(RotationMatrix);
	OutRotation.Normalize();
	return true;
}
}

FSkeletalMeshPreviewViewportClient::FSkeletalMeshPreviewViewportClient()
{
	Camera.SetProjectionType(EViewportProjectionType::Perspective);
	Camera.SetFOV(MathUtil::DegreesToRadians(90.0f));
	Camera.SetNearPlane(0.1f);
	Camera.SetFarPlane(1000.0f);

	ResetCamera();
}

void FSkeletalMeshPreviewViewportClient::Tick(float DeltaTime)
{
	(void)DeltaTime;
}

void FSkeletalMeshPreviewViewportClient::BuildSceneView(FSceneView& OutView) const
{
	const FViewportRect Rect(0, 0, static_cast<int32>(WindowWidth), static_cast<int32>(WindowHeight));
	Camera.BuildSceneView(OutView, Rect, State ? State->ViewMode : EViewMode::Lit);
}

void FSkeletalMeshPreviewViewportClient::SetViewportSize(float InWidth, float InHeight)
{
	FViewportClient::SetViewportSize(InWidth, InHeight);
	Camera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
}

void FSkeletalMeshPreviewViewportClient::AddOrbitInput(float DeltaYaw, float DeltaPitch)
{
	if (ViewportType != EVT_Perspective)
	{
		return;
	}

	OrbitYaw += DeltaYaw;
	OrbitPitch = MathUtil::Clamp(OrbitPitch + DeltaPitch, -85.0f, 85.0f);
	UpdateCameraTransform();
}

void FSkeletalMeshPreviewViewportClient::AddZoomInput(float Notch, float DeltaTime)
{
	if (Notch == 0.0f)
	{
		return;
	}

	if (ViewportType == EVT_Perspective)
	{
		const FVector Forward = Camera.GetForwardVector().GetSafeNormal();
		const FVector NewLocation = Camera.GetLocation() + Forward * Notch * ZoomSpeed * DeltaTime;
		Camera.SetLocation(NewLocation);
		ViewTarget = NewLocation + Forward * OrbitDistance;
	}
	else
	{
		const float NewHeight = Camera.GetOrthoHeight() - Notch * 300.0f * MoveSensitivity * DeltaTime;
		Camera.SetOrthoHeight(MathUtil::Clamp(NewHeight, 0.1f, 1000.0f));
	}
}

void FSkeletalMeshPreviewViewportClient::AddPanInput(float DeltaX, float DeltaY, float DeltaTime)
{
	const float PanScale = (Camera.IsOrthographic() ? Camera.GetOrthoHeight() * 0.002f : 20.0f) * MoveSensitivity;
	const FVector Right = Camera.GetEffectiveRight().GetSafeNormal();
	const FVector Up = Camera.GetEffectiveUp().GetSafeNormal();
	const FVector PanDelta = Right * (-DeltaX * PanScale * DeltaTime) + Up * (DeltaY * PanScale * DeltaTime);

	ViewTarget += PanDelta;
	Camera.SetLocation(Camera.GetLocation() + PanDelta);

	if (ViewportType != EVT_Perspective)
	{
		ApplyCameraMode();
	}
}

void FSkeletalMeshPreviewViewportClient::BeginCameraControl()
{
	if (ViewportType != EVT_Perspective)
	{
		return;
	}

	const FVector Forward = Camera.GetForwardVector().GetSafeNormal();
	Pitch = MathUtil::RadiansToDegrees(std::asin(MathUtil::Clamp(Forward.Z, -1.0f, 1.0f)));
	Yaw = MathUtil::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
}

void FSkeletalMeshPreviewViewportClient::AddLookInput(float DeltaYaw, float DeltaPitch)
{
	if (ViewportType != EVT_Perspective)
	{
		return;
	}

	Camera.ClearCustomLookDir();
	const float RotationSpeed = 0.15f * RotateSensitivity;
	Yaw += DeltaYaw * RotationSpeed;
	Pitch -= DeltaPitch * RotationSpeed;
	Pitch = MathUtil::Clamp(Pitch, -89.0f, 89.0f);
	UpdateCameraRotation();
	ViewTarget = Camera.GetLocation() + Camera.GetForwardVector().GetSafeNormal() * OrbitDistance;
}

void FSkeletalMeshPreviewViewportClient::AddMoveInput(float Forward, float Right, float Up, float DeltaTime)
{
	if (ViewportType != EVT_Perspective)
	{
		return;
	}

	FVector MoveDelta = FVector::ZeroVector;
	MoveDelta += Camera.GetEffectiveForward().GetSafeNormal() * Forward;
	MoveDelta += Camera.GetEffectiveRight().GetSafeNormal() * Right;
	MoveDelta += FVector::UpVector * Up;

	if (MoveDelta.IsNearlyZero())
	{
		return;
	}

	MoveDelta = MoveDelta.GetSafeNormal() * MoveSpeed * MoveSpeedScale * MoveSensitivity * DeltaTime;
	Camera.SetLocation(Camera.GetLocation() + MoveDelta);
	ViewTarget += MoveDelta;
}

void FSkeletalMeshPreviewViewportClient::AdjustMoveSpeedScale(float Notch)
{
	if (Notch == 0.0f)
	{
		return;
	}

	MoveSpeedScale *= std::pow(1.25f, Notch);
	MoveSpeedScale = MathUtil::Clamp(MoveSpeedScale, 0.05f, 20.0f);
}

void FSkeletalMeshPreviewViewportClient::ResetCamera()
{
	OrbitPitch = 15.0f;
	OrbitYaw = 135.0f;
	OrbitDistance = 3.0f;
	ViewTarget = FVector(0.0f, 0.0f, 1.0f);
	Camera.SetOrthoHeight(10.0f);
	MoveSpeedScale = 1.0f;
	UpdateCameraTransform();
}

void FSkeletalMeshPreviewViewportClient::ApplyCameraMode()
{
	Camera.SetRotation(FRotator(0.0f, 0.0f, 0.0f));

	switch (ViewportType)
	{
	case EVT_Perspective:
		Camera.SetProjectionType(EViewportProjectionType::Perspective);
		Camera.ClearCustomLookDir();
		UpdateCameraTransform();
		break;
	case EVT_OrthoTop:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(0.0f, 0.0f, OrbitDistance));
		Camera.SetCustomLookDir(FVector(0.0f, 0.0f, -1.0f), FVector(1.0f, 0.0f, 0.0f));
		break;
	case EVT_OrthoBottom:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(0.0f, 0.0f, -OrbitDistance));
		Camera.SetCustomLookDir(FVector(0.0f, 0.0f, 1.0f), FVector(1.0f, 0.0f, 0.0f));
		break;
	case EVT_OrthoFront:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(OrbitDistance, 0.0f, 0.0f));
		Camera.SetCustomLookDir(FVector(-1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
		break;
	case EVT_OrthoBack:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(-OrbitDistance, 0.0f, 0.0f));
		Camera.SetCustomLookDir(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
		break;
	case EVT_OrthoLeft:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(0.0f, -OrbitDistance, 0.0f));
		Camera.SetCustomLookDir(FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
		break;
	case EVT_OrthoRight:
		Camera.SetProjectionType(EViewportProjectionType::Orthographic);
		Camera.SetLocation(ViewTarget + FVector(0.0f, OrbitDistance, 0.0f));
		Camera.SetCustomLookDir(FVector(0.0f, -1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
		break;
	default:
		break;
	}
}

void FSkeletalMeshPreviewViewportClient::UpdateCameraTransform()
{
	FQuat PitchQuat(FVector::RightVector, MathUtil::DegreesToRadians(OrbitPitch));
	FQuat YawQuat(FVector::UpVector, MathUtil::DegreesToRadians(OrbitYaw));
	const FQuat OrbitRot = YawQuat * PitchQuat;
	const FVector Forward = OrbitRot.GetForwardVector().GetSafeNormal();

	FQuat CameraRot;
	if (!BuildRotationFromForward(Forward, CameraRot))
	{
		return;
	}

	Camera.SetRotation(CameraRot);
	Camera.SetLocation(ViewTarget - Forward * OrbitDistance);
}

void FSkeletalMeshPreviewViewportClient::UpdateCameraRotation()
{
	const float PitchRad = MathUtil::DegreesToRadians(Pitch);
	const float YawRad = MathUtil::DegreesToRadians(Yaw);

	FVector Forward(std::cos(PitchRad) * std::cos(YawRad), std::cos(PitchRad) * std::sin(YawRad), std::sin(PitchRad));
	Forward = Forward.GetSafeNormal();

	FQuat NewRotation;
	if (!BuildRotationFromForward(Forward, NewRotation))
	{
		return;
	}
	Camera.SetRotation(NewRotation);
}
