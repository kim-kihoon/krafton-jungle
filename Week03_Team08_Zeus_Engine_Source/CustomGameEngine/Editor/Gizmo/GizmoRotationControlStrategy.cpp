#include "GizmoRotationControlStrategy.h"
#include "Component/GizmoComponent.h"
#include "ResourceManager.h"
#include "Object.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderObject.h"
#include "Component/SceneComponent.h"
#include "Logger.h"

GizmoRotationControlStrategy::GizmoRotationControlStrategy(UGizmoComponent* controller)
{
	Controller = controller;
}

GizmoRotationControlStrategy::~GizmoRotationControlStrategy()
{
	SetDrawEnable(false);
	for (FGizmoHandle* handle : Handles)
	{
		delete handle;
	}
}

static FGizmoHandle* CreateHandle(FVector axisDirection, FVector planeNormal, GizmoControllerAxis axisType, FColor color, const FString& geometryName, const FMatrix& localMatrix)
{
	ResourceManager* resourceManager = ResourceManager::GetInstance();
	FGizmoHandle* handle = new FGizmoHandle();
	handle->AxisDirection = axisDirection;
	handle->PlaneNormal = planeNormal;
	handle->AxisType = axisType;
	handle->Color = color;
	handle->RenderObj = new RenderObject();
	handle->RenderObj->Geometry = &resourceManager->GetGeometry(geometryName);
	handle->RenderObj->Material = &resourceManager->GetMaterial(L"Asset/Shader/ShaderW0.hlsl");
	handle->RenderObj->bDepthEnabled = false;
	handle->RenderObj->bIsVisible = false;
	handle->RenderObj->Color = color;
	handle->RenderObj->ShowFlag = EShowFlag::Gizmo;
	handle->LocalMatrix = localMatrix;
	return handle;
}

void GizmoRotationControlStrategy::CreateRenderObjects()
{
	ResourceManager* resourceManager = ResourceManager::GetInstance();

	Handles.resize(3);

	Handles[0] = CreateHandle(FVector::Forward(), FVector::Up(), GizmoControllerAxis::X, FColor::Red(), "GizmoRotation", FMatrix::IdentityMatrix());
	Handles[1] = CreateHandle(FVector::Right(), FVector::Up(), GizmoControllerAxis::Y, FColor::Green(), "GizmoRotation", FMatrix::EulerRotationMatrix(0.0f, 0.0f, -90.0f));
	Handles[2] = CreateHandle(FVector::Up(), FVector::Right(), GizmoControllerAxis::Z, FColor::Blue(), "GizmoRotation", FMatrix::EulerRotationMatrix(0.0f, 90.0f, 0.0f));

	for (size_t i = 0; i < Handles.size(); i++)
	{
		FLevel::GetInstance().RegisterRenderObject(Handles[i]->RenderObj);
	}
}

void GizmoRotationControlStrategy::Update()
{
	if (Controller->bIsDragging()) return;

	UCameraComponent* camera = UCameraComponent::GetMainCamera();
	if (camera == nullptr) return;

	const FVector OScale = Controller->GetAttachedComponent()->GetRelativeScale3D();
	auto MakeTransform = [](const FVector& U, const FVector& V, const FVector& N) -> FMatrix
		{
			return FMatrix(
				U.x, U.y, U.z, 0.0f,
				V.x, V.y, V.z, 0.0f,
				N.x, N.y, N.z, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f
			);
		};

	FVector X = FVector::Forward();
	FVector Y = FVector::Right();
	FVector Z = FVector::Up();

	FVector DirToCamera;

	if (!camera->IsOrthographic)
	{
		FVector cameraPos = camera->GetRelativeLocation();
		FVector localCameraPos = Controller->GetRelativeMatrix().Inverse().TransformPoint(cameraPos);
		DirToCamera = localCameraPos.Normalize();
	}
	else
	{
		FVector cameraForward = camera->Direction;
		DirToCamera = Controller->GetRelativeMatrix().Inverse().TransformVector(cameraForward * -1);
	}

	const float Sx = (DirToCamera.Dot(X) >= 0) ? 1.0f : -1.0f;
	const float Sy = (DirToCamera.Dot(Y) >= 0) ? 1.0f : -1.0f;
	const float Sz = (DirToCamera.Dot(Z) >= 0) ? 1.0f : -1.0f;

	{
		FVector U = Y * Sy;
		FVector V = Z * Sz;
		FVector N = U.Cross(V).Normalize();

		Handles[0]->LocalMatrix = MakeTransform(U, V, N);
	}

	{
		FVector U = Z * Sz;
		FVector V = X * Sx;
		FVector N = U.Cross(V).Normalize();
		Handles[1]->LocalMatrix = MakeTransform(U, V, N);
	}

	{
		FVector U = X * Sx;
		FVector V = Y * Sy;
		FVector N = U.Cross(V).Normalize();
		Handles[2]->LocalMatrix = MakeTransform(U, V, N);
	}
}

void GizmoRotationControlStrategy::UpdateRenderObjects()
{
	for (size_t i = 0; i < Handles.size(); ++i)
	{
		Handles[i]->RenderObj->World = Handles[i]->LocalMatrix * Controller->GetRelativeMatrix();
		Handles[i]->RenderObj->Color = (Controller->HoverAxis == Handles[i]->AxisType) ? FColor::Yellow() : Handles[i]->Color;
	}
}

void GizmoRotationControlStrategy::BeginDrag(const Ray& ray)
{
	InitialObjectQuaternion = Controller->GetAttachedComponent()->GetRelativeQuaternion();
	AccumulatedAngle = 0.0f;
	GetCursorPos(&InitialCursorPos);
	ShowCursor(false);

	DragTargets = Controller->GetAttachedComponents();
	if (DragTargets.empty() && Controller->GetAttachedComponent())
	{
		DragTargets.push_back(Controller->GetAttachedComponent());
	}

	InitialTargetQuaternions.clear();
	for (USceneComponent* target : DragTargets)
	{
		InitialTargetQuaternions.push_back(target->GetRelativeQuaternion());
	}
}

void GizmoRotationControlStrategy::UpdateDrag(const Ray& ray)
{
	FVector worldAxis;
	switch (Controller->CurrentAxis)
	{
	case GizmoControllerAxis::X:
		worldAxis = FVector::Forward();
		break;
	case GizmoControllerAxis::Y:
		worldAxis = FVector::Right();
		break;
	case GizmoControllerAxis::Z:
		worldAxis = FVector::Up();
		break;
	}

	FVector axisDir = worldAxis;
	if (Controller->bLocalSpace)
	{
		axisDir = Controller->GetRelativeMatrix().TransformVector(worldAxis).Normalize();
	}

	POINT currPos;
	GetCursorPos(&currPos);

	int deltaX = currPos.x - InitialCursorPos.x;
	int deltaY = currPos.y - InitialCursorPos.y;

	UCameraComponent* camera = UCameraComponent::GetMainCamera();
	FVector screenOrigin = camera->WorldToScreen(Controller->GetAttachedComponent()->GetRelativeLocation());
	FVector screenAxisEnd = camera->WorldToScreen(Controller->GetAttachedComponent()->GetRelativeLocation() + axisDir);
	FVector screenAxisVector = (screenAxisEnd - screenOrigin).Normalize();

	FVector screenTangent = FVector(-screenAxisVector.y, screenAxisVector.x, 0.0f);

	if (deltaX == 0 && deltaY == 0) return;

	FQuat deltaQuat;

	float sensitivity = 0.01f;
	FVector mouseDeltaVec = FVector((float)deltaX, (float)deltaY, 0.0f);
	float effectiveDelta = mouseDeltaVec.Dot(screenTangent);

	float deltaAngle = -effectiveDelta * sensitivity;

	AccumulatedAngle += deltaAngle;

	float applyAngle = AccumulatedAngle;
	if (bEnableSnap)
	{
		float snapStepRad = SnapStep * (MathHelper::PI / 180.0f);
		applyAngle = roundf(AccumulatedAngle / snapStepRad) * snapStepRad;
	}

	deltaQuat = FQuat::FromAxisAngle(worldAxis, applyAngle);

	for (size_t i = 0; i < DragTargets.size(); ++i)
	{
		FQuat targetQuat = InitialTargetQuaternions[i];
		if (Controller->bLocalSpace)
		{
			targetQuat = targetQuat * deltaQuat;
		}
		else
		{
			targetQuat = deltaQuat * targetQuat;
		}
		DragTargets[i]->SetRelativeQuaternion(targetQuat);
	}

	SetCursorPos(InitialCursorPos.x, InitialCursorPos.y);
}

void GizmoRotationControlStrategy::EndDrag()
{
	ShowCursor(true);
	DragTargets.clear();
	DragTargets.shrink_to_fit();
	InitialTargetQuaternions.clear();
	InitialTargetQuaternions.shrink_to_fit();
}

void GizmoRotationControlStrategy::SetDrawEnable(bool bEnable)
{
	for (FGizmoHandle* handle : Handles)
	{
		handle->RenderObj->bIsVisible = bEnable;
	}
}