#include "pch.h"
#include "FEditorViewportClient.h"
#include "Input.h"

FMatrix FEditorViewportClient::GetViewMatrix() const
{
	FVector Eye = ViewTransform.GetLocation();
	FVector Forward = ViewTransform.GetForwardVector();
	FVector Up = ViewTransform.GetUpVector();

	FVector At = Eye + Forward;

	return FMatrix::MakeLookAt(Eye, At, Up);
}

FMatrix FEditorViewportClient::GetProjectionMatrix() const
{
	if (bIsPerspective)
	{
		return FMatrix::MakePerspective(FOVAngle, AspectRatio, NearPlane, FarPlane);
	}
	else
	{
		float OrthoHeight = ViewTransform.GetOrthoSize();
		float OrthoWidth = OrthoHeight * AspectRatio;
		return FMatrix::MakeOrthographic(OrthoWidth, OrthoHeight, NearPlane, FarPlane);
	}
}

void FEditorViewportClient::SetPerspective(bool bInIsPerspective)
{
	if (bIsPerspective == bInIsPerspective)
	{
		return;
	}

	// Orthographic to Perspective
	if (bInIsPerspective)
	{
		// D = S / (2 * tan(FOV / 2))
		float HalfFOVRadian = Math::ToRadians(FOVAngle) / 2.0f;
		float NewDistance = ViewTransform.GetOrthoSize() / (2.0f * tanf(HalfFOVRadian));
		FVector NewViewLocation = ViewTransform.GetPivotLocation() - ViewTransform.GetForwardVector() * NewDistance;
		ViewTransform.SetLocation(NewViewLocation);
		ViewTransform.SetDistance(NewDistance);
	}
	// Perspective to Orthographic
	else
	{
		// S = 2 * D * tan(FOV / 2)
		float HalfFOVRadian = Math::ToRadians(FOVAngle) / 2.0f;
		float NewOrthoSize = 2.0f * ViewTransform.GetDistance() * tanf(HalfFOVRadian);
		ViewTransform.SetOrthoSize(NewOrthoSize);
	}

	bIsPerspective = bInIsPerspective;
}

void FEditorViewportClient::DeprojectScreenToWorld(const FVector2D& ScreenPos, FVector& out_WorldOrigin, FVector& out_WorldDirection)
{	
	FVector2D ViewportSize = { ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y };
	FVector2D NormalizedScreenPos = { ScreenPos.X / ViewportSize.X - 0.5f, 0.5f - ScreenPos.Y / ViewportSize.Y };

	if (!bIsPerspective)
	{
		float OrthoHeight = ViewTransform.GetOrthoSize();
		float OrthoWidth = OrthoHeight * AspectRatio;

		FVector Forward = ViewTransform.GetForwardVector();
		FVector Right = ViewTransform.GetRightVector();
		FVector Up = ViewTransform.GetUpVector();

		out_WorldOrigin = ViewTransform.GetLocation()
			+ (Right * NormalizedScreenPos.X * OrthoWidth)
			+ (Up * NormalizedScreenPos.Y * OrthoHeight);

		out_WorldDirection = Forward;
	}
	else 
	{
		//if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
		//{
		//	out_WorldOrigin = ViewTransform.GetLocation();
		//	out_WorldDirection = ViewTransform.GetForwardVector();
		//	return;
		//}

		float NDCX = (2.0f * ScreenPos.X) / ViewportSize.X - 1.0f;
		float NDCY = 1.0f - (2.0f * ScreenPos.Y) / ViewportSize.Y;

		FMatrix InvViewProj = (GetViewMatrix() * GetProjectionMatrix()).Inverse();

		FVector NearNDC = { NDCX, NDCY, 0.0f };
		FVector FarNDC = { NDCX, NDCY, 1.0f };

		FVector WorldNear = FMatrix::TransformCoord(NearNDC, InvViewProj);
		FVector WorldFar = FMatrix::TransformCoord(FarNDC, InvViewProj);

		out_WorldOrigin = ViewTransform.GetLocation();
		out_WorldDirection = WorldFar - WorldNear;
	}
}

FVector FEditorViewportClient::GetCameraRayDirection()
{
	if (!bIsPerspective)
	{
		return ViewTransform.GetForwardVector();
	}

	POINT MousePos = UInput::GetInstance().GetMousePosition();

	// 고정된 WindowSizeWidth/Height 상수 대신, 실시간으로 변하는 창 크기를 가져옵니다.
	float CurrentWidth = ImGui::GetIO().DisplaySize.x;
	float CurrentHeight = ImGui::GetIO().DisplaySize.y;

	// 최소화 등으로 창 크기가 0이 되었을 때의 0 나누기 방어코드
	if (CurrentWidth <= 0.0f || CurrentHeight <= 0.0f) return FVector(0.f, 0.f, 1.f);

	// 가져온 동적 크기를 사용하여 정밀한 NDC(정규화 장치 좌표)를 계산합니다.
	float NDCX = (2.0f * MousePos.x) / CurrentWidth - 1.0f;
	float NDCY = 1.0f - (2.0f * MousePos.y) / CurrentHeight;

	FMatrix InvViewProj = (GetViewMatrix() * GetProjectionMatrix()).Inverse();

	FVector NearNDC = { NDCX, NDCY, 0.0f };
	FVector FarNDC = { NDCX, NDCY, 1.0f };

	FVector WorldNear = FMatrix::TransformCoord(NearNDC, InvViewProj);
	FVector WorldFar = FMatrix::TransformCoord(FarNDC, InvViewProj);

	FVector WorldDirection = WorldFar - WorldNear;
	WorldDirection.Normalize();

	return WorldDirection;
}

void FEditorViewportClient::Tick(float DeltaTime) {
	HandleKeyboardMovement(DeltaTime);
	HandleMouseRightDrag();
	HandleMouseWheel();
	HandleMiddleMouseDrag();
}

void FEditorViewportClient::HandleKeyboardMovement(float DeltaTime)
{
	UInput& Input = UInput::GetInstance();

	FVector MovementDirection = { 0.0f, 0.0f, 0.0f };
	if (Input.IsKeyPressing('A'))
	{
		MovementDirection = MovementDirection - ViewTransform.GetRightVector();
	}
	if (Input.IsKeyPressing('D'))
	{
		MovementDirection = MovementDirection + ViewTransform.GetRightVector();
	}
	if (Input.IsKeyPressing('S'))
	{
		MovementDirection = MovementDirection - ViewTransform.GetForwardVector();
	}
	if (Input.IsKeyPressing('W'))
	{
		MovementDirection = MovementDirection + ViewTransform.GetForwardVector();
	}

	// 이동 방향이 없을 때의 불필요한 연산을 막기 위한 안전장치 (선택 사항)
	if (MovementDirection.Length() == 0.0f)
	{
		return;
	}

	MovementDirection.Normalize();

	constexpr float MovementSpeed = 12.0f;
	FVector Movement = ViewTransform.GetLocation() + MovementDirection * DeltaTime * MovementSpeed;
	ViewTransform.SetLocation(Movement);

	if (Input.IsKeyPressing('A'))
	{
		MovementDirection = MovementDirection - ViewTransform.GetRightVector();
	}
	if (Input.IsKeyPressing('D'))
	{
		MovementDirection = MovementDirection + ViewTransform.GetRightVector();
	}
	if (Input.IsKeyPressing('S'))
	{
		MovementDirection = MovementDirection - ViewTransform.GetForwardVector();
	}
	if (Input.IsKeyPressing('W'))
	{
		MovementDirection = MovementDirection + ViewTransform.GetForwardVector();
	}
	MovementDirection.Normalize();

}

void FEditorViewportClient::HandleMouseRightDrag()
{
	UInput& Input = UInput::GetInstance();

	if (!Input.IsKeyPressing(VK_RBUTTON))
	{
		return;
	}

	POINT MouseDelta = Input.GetMousePositionDelta();
	float DeltaMouseY = static_cast<float>(MouseDelta.y); // Pitch
	float DeltaMouseX = static_cast<float>(MouseDelta.x); // Yaw

	constexpr float RotationSpeed = 0.2f;
	FVector Rotation = ViewTransform.GetRotation();
	Rotation.X = std::clamp(Rotation.X + (DeltaMouseY * RotationSpeed), -89.9f, 89.9f);
	Rotation.Y = Rotation.Y + (DeltaMouseX * RotationSpeed);

	bool bIsOrbiting = Input.IsKeyPressing(VK_LMENU);
	if (bIsOrbiting)
	{
		FVector OldPivotLocation = ViewTransform.GetPivotLocation();
		ViewTransform.SetRotation(Rotation);
		FVector NewPivotLocation = ViewTransform.GetPivotLocation();
		ViewTransform.SetLocation(ViewTransform.GetLocation() + (OldPivotLocation - NewPivotLocation));
	}
	else
	{
		ViewTransform.SetRotation(Rotation);
	}
}

void FEditorViewportClient::HandleMouseWheel()
{
	UInput& Input = UInput::GetInstance();
	float MouseWheelDelta = Input.GetMouseWheelDelta();

	if (MouseWheelDelta == 0.0f)
	{
		return;
	}

	if (bIsPerspective)
	{
		constexpr float PerspectiveZoomSpeed = 2.0f;
		float NewDistance = ViewTransform.GetDistance() - MouseWheelDelta * PerspectiveZoomSpeed;
		NewDistance = (std::max)(NewDistance, 1.0f);
		FVector NewViewLocation = ViewTransform.GetPivotLocation() - ViewTransform.GetForwardVector() * NewDistance;
		ViewTransform.SetLocation(NewViewLocation);
		ViewTransform.SetDistance(NewDistance);
	}
	else
	{
		constexpr float OrthoZoomSpeed = 2.0f;
		float NewOrthoSize = ViewTransform.GetOrthoSize() - MouseWheelDelta * OrthoZoomSpeed;
		NewOrthoSize = (std::max)(NewOrthoSize, 1.0f);
		ViewTransform.SetOrthoSize(NewOrthoSize);
	}
}

void FEditorViewportClient::HandleMiddleMouseDrag()
{
	UInput& Input = UInput::GetInstance();

	if (!Input.IsKeyPressing(VK_MBUTTON))
	{
		return;
	}

	POINT MouseDelta = Input.GetMousePositionDelta();
	float DeltaMouseY = static_cast<float>(MouseDelta.y);
	float DeltaMouseX = static_cast<float>(MouseDelta.x);

	int WindowSizeHeight = ImGui::GetIO().DisplaySize.y;

	float PanSpeed = 1.0f;
	if (bIsPerspective)
	{
		float HalfFOVRadian = Math::ToRadians(FOVAngle) / 2.0f;
		float WorldHeight = 2.0f * ViewTransform.GetDistance() * tanf(HalfFOVRadian);
		PanSpeed = WorldHeight / WindowSizeHeight;
	}
	else
	{
		PanSpeed = ViewTransform.GetOrthoSize() / WindowSizeHeight;
	}
	FVector Movement = (ViewTransform.GetRightVector() * -DeltaMouseX + ViewTransform.GetUpVector() * DeltaMouseY) * PanSpeed;
	ViewTransform.SetLocation(ViewTransform.GetLocation() + Movement);
}

FVector FViewportCameraTransform::GetRightVector() const
{
	FMatrix RotationMatrix = FMatrix::MakeRotation(ViewRotation);
	FVector Right = { RotationMatrix.M[0][0], RotationMatrix.M[0][1], RotationMatrix.M[0][2] };
	// TODO: Normalize가 실제로 필요한지 확인 필요
	Right.Normalize();
	return Right;
}

FVector FViewportCameraTransform::GetUpVector() const
{
	FMatrix RotationMatrix = FMatrix::MakeRotation(ViewRotation);
	FVector Up = { RotationMatrix.M[1][0], RotationMatrix.M[1][1], RotationMatrix.M[1][2] };
	Up.Normalize();
	return Up;
}

FVector FViewportCameraTransform::GetForwardVector() const
{
	FMatrix RotationMatrix = FMatrix::MakeRotation(ViewRotation);
	FVector Forward = { RotationMatrix.M[2][0], RotationMatrix.M[2][1], RotationMatrix.M[2][2] };
	Forward.Normalize();
	return Forward;
}
