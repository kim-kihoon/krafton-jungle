#define NOMINMAX

#include "CameraComponent.h"
#include "InputManager.h"
#include "Math/MathHelper.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "World.h"
#include "TimerManager.h"
#include "Logger.h"
#include "Viewport.h"
#include "Editor/Editor.h"
#include "Editor/Gizmo.h"
#include "Component/GizmoComponent.h"

#include <Windows.h>
#include <algorithm>

UCameraComponent* UCameraComponent::mainCamera = nullptr;

UCameraComponent::UCameraComponent() : io(ImGui::GetIO())
{
	if (mainCamera != nullptr)
		UE_LOG("2 or more camera exist at the same time!\n");
	mainCamera = this;

	FViewport::OnResizeDelegate.AddRaw(this, &UCameraComponent::OnResize);
	Editor::OnEditorConfigLoaded.AddRaw(this, &UCameraComponent::ApplyConfig);
	Editor::OnEditorConfigSaveReady.AddRaw(this, &UCameraComponent::GatherConfig);
}

UCameraComponent::~UCameraComponent() {
	if (mainCamera == this)
		mainCamera = nullptr;

	FViewport::OnResizeDelegate.RemoveRaw(this);
	Editor::OnEditorConfigLoaded.RemoveRaw(this);
	Editor::OnEditorConfigSaveReady.RemoveRaw(this);
}

UCameraComponent* UCameraComponent::GetMainCamera()
{
	return mainCamera;
}

void UCameraComponent::Move(float DeltaTime) {
	FVector position = GetRelativeLocation();
	FVector deltaPos = FVector::Zero();

	if (InputManager::GetInstance().IsKeyHold('W')) {
		deltaPos = deltaPos + FVector::Forward() * MoveSpeed;
	}
	if (InputManager::GetInstance().IsKeyHold('S')) {
		deltaPos = deltaPos + FVector::Forward() * -MoveSpeed;
	}  
	if (InputManager::GetInstance().IsKeyHold('D')) {
		deltaPos = deltaPos + FVector::Right() * MoveSpeed;
	}
	if (InputManager::GetInstance().IsKeyHold('A')) {
		deltaPos = deltaPos + FVector::Right() * -MoveSpeed;
	}

	deltaPos = GetRelativeRotation().ToMatrix().TransformVector(deltaPos);

	if (InputManager::GetInstance().IsKeyHold('E')) {
		deltaPos = deltaPos + FVector::Up() * MoveSpeed;
	}
	if (InputManager::GetInstance().IsKeyHold('Q')) {
		deltaPos = deltaPos + FVector::Up() * -MoveSpeed;
	}

	deltaPos = deltaPos * DeltaTime;

	position = position + deltaPos;

	SetRelativeLocation(position);
}

void UCameraComponent::HandleRotate(float DeltaTime)
{
	FRotator eulerAngle = GetRelativeRotation();

	float newPitch = eulerAngle.Pitch + (InputManager::GetInstance().MouseDelta.y * RotateSensitivity);
	eulerAngle.Pitch = MathHelper::Clamp(newPitch, -89.9f, 89.9f);
	eulerAngle.Yaw = eulerAngle.Yaw + (InputManager::GetInstance().MouseDelta.x * RotateSensitivity);

	SetRelativeRotation(eulerAngle);
}

void UCameraComponent::HandleOrbitRotate(float DeltaTime)
{
	FRotator eulerAngle = GetRelativeRotation();

	float newPitch = eulerAngle.Pitch + (InputManager::GetInstance().MouseDelta.y * RotateSensitivity);
	newPitch = MathHelper::Clamp(newPitch, -89.9f, 89.9f);
	eulerAngle.Pitch = newPitch;
	eulerAngle.Yaw = eulerAngle.Yaw + (InputManager::GetInstance().MouseDelta.x * RotateSensitivity);

	SetRelativeRotation(eulerAngle);

	FMatrix RotMatrix = GetRelativeRotation().ToMatrix();
	FVector ForwardBase = FVector::Forward();
	FVector ForwardVec = RotMatrix.TransformVector(ForwardBase).Normalize();

	SetRelativeLocation(FocusPoint - ForwardVec * FocusLength);
}

void UCameraComponent::OnResize(int NewWidth, int NewHeight) {
	if (NewHeight == 0) return;
	ScreenWidth = static_cast<float>(NewWidth);
	ScreenHeight = static_cast<float>(NewHeight);
	AspectRatio = static_cast<float>(NewWidth) / static_cast<float>(NewHeight);
}

void UCameraComponent::Update(float deltaTime) {
	HandleCameraMove(deltaTime);
	UpdateCameraMatrices();
}

void UCameraComponent::HandleCameraMove(float deltaTime)
{
	UGizmoComponent* GizmoController = Gizmo::GetInstance().GetController();
	if (GizmoController && GizmoController->bIsDragging())
		return;
		
	if (!io.WantCaptureMouse && io.MouseWheel != 0 && !InputManager::GetInstance().IsMouseHold(VK_RBUTTON))
	{
		FVector Position = GetRelativeLocation();
		Position = Position + Direction * (io.MouseWheel * MoveSpeed * 0.2f);
		SetRelativeLocation(Position);
	}

	if (InputManager::GetInstance().IsMouseHold(VK_RBUTTON))
	{
		Move(deltaTime);
		HandleRotate(deltaTime);

		if (io.MouseWheel != 0)
		{
			MoveSpeed += io.MouseWheel * (MoveSpeed * 0.1f);
			MoveSpeed = (std::max)(0.1f, MoveSpeed);
		}
	}

	else if (InputManager::GetInstance().IsMouseHold(VK_MBUTTON))
	{
		FVector Position = GetRelativeLocation();
		float PanSpeed = MoveSpeed * 0.01f;
		FVector Delta = (SideDirection * (-InputManager::GetInstance().MouseDelta.x) +
			UpDirection * InputManager::GetInstance().MouseDelta.y) * PanSpeed;
		SetRelativeLocation(Position + Delta);
	}

	else if (InputManager::GetInstance().IsMouseHold(VK_LBUTTON) && !InputManager::GetInstance().IsKeyHold(VK_MENU))
	{
		// 1. 회전 로직 (Yaw): 마우스 좌우 이동에 따라 고개를 돌립니다.
		FRotator EulerAngle = GetRelativeRotation();
		EulerAngle.Yaw += (InputManager::GetInstance().MouseDelta.x * RotateSensitivity);
		SetRelativeRotation(EulerAngle);

		// 2. 이동 로직 (Walk): 마우스 상하 이동에 따라 높이(Z)를 유지하며 앞뒤로 걷습니다.
		// 카메라가 현재 바라보는 방향(Direction)에서 수평 성분(X, Y)만 추출하여 전진 벡터를 만듭니다.
		FVector HorizontalForward = FVector(Direction.x, Direction.y, 0.0f).Normalize();

		// 마우스를 위로 밀면(-y) 앞으로, 아래로 당기면(+y) 뒤로 이동합니다.
		float WalkDistance = -InputManager::GetInstance().MouseDelta.y * MoveSpeed * 0.01f;

		FVector Position = GetRelativeLocation();
		Position = Position + (HorizontalForward * WalkDistance);
		SetRelativeLocation(Position);
	}

	else if (InputManager::GetInstance().IsMouseHold(VK_LBUTTON) && InputManager::GetInstance().IsKeyHold(VK_MENU))
	{
		HandleOrbitRotate(deltaTime);
	}
	if (InputManager::GetInstance().IsKeyHold('F') && GetWorld().GetActiveScene()->IsCompSelected)
	{
		// TODO: 오브젝트 포커싱 재구현
		// SetPosition(EditorInst->GetSelectedComponent()->GetRelativeLocation() - Direction * FocusLength);
	}
}

void UCameraComponent::UpdateCameraMatrices()
{
	FMatrix RotMatrix = GetRelativeRotation().ToMatrix();

	Direction = RotMatrix.TransformVector(FVector::Forward()).Normalize();
	SideDirection = FVector::Up().Cross(Direction);
	UpDirection = Direction.Cross(SideDirection);

	auto position = GetRelativeLocation();
	FocusPoint = position + Direction * FocusLength;
	View = FMatrix::ViewMatrix(position, FocusPoint, UpDirection);
	if (!IsOrthographic)
		Projection = FMatrix::PerspectiveMatrix(Fov, AspectRatio, NearPlane, FarPlane);
	else
		Projection = FMatrix::OrthographicMatrix(OrthoHeight, AspectRatio, NearPlane, FarPlane);

	ViewProjection = View * Projection;
}

FVector UCameraComponent::WorldToScreen(const FVector& worldPos) const
{
	FVector clipSpacePos = ViewProjection.TransformPoint(worldPos);
	if (clipSpacePos.z == 0.0f) return FVector(-1.0f, -1.0f, -1.0f);

	FVector ndcSpacePos = clipSpacePos / clipSpacePos.z;
	float screenX = (ndcSpacePos.x + 1.0f) * 0.5f * ScreenWidth;
	float screenY = (1.0f - (ndcSpacePos.y + 1.0f) * 0.5f) * ScreenHeight;

	return FVector(screenX, screenY, ndcSpacePos.z);
}

Ray UCameraComponent::ScreenPointToRay(int x, int y)
{
	//Screen Space -> NDC Space
	float ndcX = (2.0f * (static_cast<float>(x) + 0.5f)) / ScreenWidth - 1.0f;
	float ndcY = 1.0f - (2.0f * (static_cast<float>(y) + 0.5f)) / ScreenHeight;
	//NDC -> View Space
	FVector4 Near = FVector4(ndcX, ndcY, 1.f, 1.f);
	FVector4 Far = FVector4(ndcX, ndcY, 0.f, 1.f);
	FMatrix InvProjMat = GetProjectionMatrix().Inverse();
	FVector4 NearView = Near * InvProjMat;
	FVector4 FarView = Far * InvProjMat;
	NearView = NearView * (1.0f / NearView.w);
	FarView = FarView * (1.0f / FarView.w);
	//View Space -> World Space
	FMatrix InvView = GetViewMatrix().Inverse();
	FVector4 NearWorld4 = NearView * InvView;
	FVector4 FarWorld4 = FarView * InvView;
	FVector NearWorld = FVector(NearWorld4.x, NearWorld4.y, NearWorld4.z);
	FVector FarWorld = FVector(FarWorld4.x, FarWorld4.y, FarWorld4.z);
	return Ray(NearWorld, FarWorld - NearWorld);
}

void UCameraComponent::ApplyConfig(const EditorConfig& config)
{
	MoveSpeed = config.CameraMoveSpeed;
	RotateSensitivity = config.CameraRotationSpeed;
	Fov = config.CameraFov;
	IsOrthographic = config.CameraOrthographic;
}

void UCameraComponent::GatherConfig(EditorConfig& config)
{
	config.CameraMoveSpeed = MoveSpeed;
	config.CameraRotationSpeed = RotateSensitivity;
	config.CameraFov = Fov;
	config.CameraOrthographic = IsOrthographic;
}

REGISTER_CLASS(UCameraComponent);