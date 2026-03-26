#include "GizmoTranslationControlStrategy.h"
#include "ResourceManager.h"
#include "Object.h"
#include "Component/GizmoComponent.h"
#include "Component/SceneComponent.h"
#include "Component/CameraComponent.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderObject.h"
#include "Logger.h"

GizmoTranslationControlStrategy::GizmoTranslationControlStrategy(UGizmoComponent* controller)
{
	Controller = controller;
}

GizmoTranslationControlStrategy::~GizmoTranslationControlStrategy()
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

void GizmoTranslationControlStrategy::CreateRenderObjects()
{
	ResourceManager* resourceManager = ResourceManager::GetInstance();

	Handles.resize(7);

	Handles[0] = CreateHandle(FVector::Forward(), FVector::Up(), GizmoControllerAxis::X, FColor::Red(), "GizmoTranslation", FMatrix::RotationYMatrix(90.0f) * FMatrix::TranslationMatrix(0.5f, 0.0f, 0.0f));
	Handles[1] = CreateHandle(FVector::Right(), FVector::Up(), GizmoControllerAxis::Y, FColor::Green(), "GizmoTranslation", FMatrix::RotationXMatrix(-90.0f) * FMatrix::TranslationMatrix(0.0f, 0.5f, 0.0f));
	Handles[2] = CreateHandle(FVector::Up(), FVector::Right(), GizmoControllerAxis::Z, FColor::Blue(), "GizmoTranslation", FMatrix::TranslationMatrix(0.0f, 0.0f, 0.5f));

	Handles[3] = CreateHandle(FVector(1.0f, 1.0f, 0.0f).Normalize(), FVector::Up(), GizmoControllerAxis::XY, FColor(1.0f, 1.0f, 1.0f, 1.0f), "GizmoTranslationSquare", FMatrix::EulerRotationMatrix(-90.0f, 0.0f, 0.0f));
	Handles[4] = CreateHandle(FVector(1.0f, 0.0f, 1.0f).Normalize(), FVector::Right(), GizmoControllerAxis::XZ, FColor(1.0f, 0.0f, 1.0f, 1.0f), "GizmoTranslationSquare", FMatrix::IdentityMatrix());
	Handles[5] = CreateHandle(FVector(0.0f, 1.0f, 1.0f).Normalize(), FVector::Forward(), GizmoControllerAxis::YZ, FColor(0.0f, 1.0f, 1.0f, 1.0f), "GizmoTranslationSquare", FMatrix::RotationZMatrix(90.0f));
	
	Handles[6] = CreateHandle(FVector(1.0f, 1.0f, 1.0f).Normalize(), FVector::Zero(), GizmoControllerAxis::XYZ, FColor(1.0f, 1.0f, 1.0f, 1.0f), "GizmoTranslationBox", FMatrix::IdentityMatrix());

	for (size_t i = 0; i < Handles.size(); i++)
	{
		FLevel::GetInstance().RegisterRenderObject(Handles[i]->RenderObj);
	}
}

void GizmoTranslationControlStrategy::Update()
{
}

void GizmoTranslationControlStrategy::UpdateRenderObjects()
{
	uint32 hoverBit = static_cast<int32>(Controller->HoverAxis);

	for (size_t i = 0; i < Handles.size(); i++)
	{
		uint32 handleBit = static_cast<int32>(Handles[i]->AxisType);
		bool isHovered = false;

		if (hoverBit != 0)
		{
			if (handleBit == hoverBit)
			{
				isHovered = true;
			}
			else
			{
				isHovered = (hoverBit & handleBit) == handleBit;
			}
		}

		Handles[i]->RenderObj->World = Handles[i]->LocalMatrix * Controller->GetRelativeMatrix();
		Handles[i]->RenderObj->Color = isHovered ? FColor::Yellow() : Handles[i]->Color;
	}
}

void GizmoTranslationControlStrategy::BeginDrag(const Ray& ray)
{
	UCameraComponent* camera = UCameraComponent::GetMainCamera();
	if (camera == nullptr) return;

	InitialObjectLocation = Controller->GetAttachedComponent()->GetRelativeLocation();
	DragTargets = Controller->GetAttachedComponents();
	if (DragTargets.empty() && Controller->GetAttachedComponent())
	{
		DragTargets.push_back(Controller->GetAttachedComponent());
	}

	InitialTargetLocations.clear();
	for (USceneComponent* target : DragTargets)
	{
		InitialTargetLocations.push_back(target->GetRelativeLocation());
	}

	FGizmoHandle* currHandle = GetCurrHandle();
	if (!currHandle) return;

	FVector axisDir = currHandle->AxisDirection;
	if (Controller->bLocalSpace)
	{
		axisDir = Controller->GetRelativeMatrix().TransformVector(axisDir).Normalize();
	}
	FVector worldOrigin = Controller->GetRelativeMatrix().TransformPoint(FVector::Zero());

	if (currHandle->AxisType == GizmoControllerAxis::XYZ)
	{
		float t;
		if (ray.IntersectsPlane(worldOrigin, camera->Direction, t))
		{
			FVector hitPoint = ray.Origin + ray.Direction * t;
			InitialDragOffset = hitPoint;
		}
	}
	else if (IsPlaneAxis(currHandle->AxisType))
	{
		FVector planeNormal = currHandle->PlaneNormal;
		if (Controller->bLocalSpace)
		{
			planeNormal = Controller->GetRelativeMatrix().TransformVector(planeNormal).Normalize();
		}
		FVector worldOrigin = Controller->GetRelativeMatrix().TransformPoint(FVector(0, 0, 0));
		float t;
		if (ray.IntersectsPlane(worldOrigin, planeNormal, t))
		{
			InitialDragOffset = ray.Origin + ray.Direction * t;
		}
	}
	else
	{
		InitialWorldPos = worldOrigin;
		InitialDragOffset.x = Controller->GetClosestPointOnAxis(ray, worldOrigin, axisDir);
	}
}

void GizmoTranslationControlStrategy::ApplyTranslationDelta(const FVector& delta)
{
	if (DragTargets.size() != InitialTargetLocations.size())
	{
		return;
	}

	for (size_t i = 0; i < DragTargets.size(); ++i)
	{
		DragTargets[i]->SetRelativeLocation(InitialTargetLocations[i] + delta);
	}
}

void GizmoTranslationControlStrategy::UpdateDrag(const Ray& ray)
{
	UCameraComponent* camera = UCameraComponent::GetMainCamera();
	if (camera == nullptr) return;

	FGizmoHandle* currHandle = GetCurrHandle();
	if (!currHandle) return;

	if (currHandle->AxisType == GizmoControllerAxis::XYZ)
	{
		FVector worldOrigin = Controller->GetRelativeMatrix().TransformPoint(FVector(0, 0, 0));
		float t;
		if (ray.IntersectsPlane(worldOrigin, camera->Direction, t))
		{
			FVector hitPoint = ray.Origin + ray.Direction * t;
			FVector delta = hitPoint - InitialDragOffset;
			ApplyTranslationDelta(delta);
		}
	}
	else if (IsPlaneAxis(currHandle->AxisType))
	{
		FVector planeNormal = currHandle->PlaneNormal;
		if (Controller->bLocalSpace)
		{
			planeNormal = Controller->GetRelativeMatrix().TransformVector(planeNormal).Normalize();
		}
		FVector worldOrigin = Controller->GetRelativeMatrix().TransformPoint(FVector(0, 0, 0));
		float t;
		if (ray.IntersectsPlane(worldOrigin, planeNormal, t))
		{
			FVector hitPoint = ray.Origin + ray.Direction * t;
			FVector delta = hitPoint - InitialDragOffset;
			ApplyTranslationDelta(delta);
		}
	}
	else
	{
		FVector axisDir = currHandle->AxisDirection;
		if (Controller->bLocalSpace)
		{
			axisDir = Controller->GetRelativeMatrix().TransformVector(axisDir).Normalize();
		}

		float currentT = Controller->GetClosestPointOnAxis(ray, InitialWorldPos, axisDir);
		float deltaT = currentT - InitialDragOffset.x;
		ApplyTranslationDelta(axisDir * deltaT);
	}
}

void GizmoTranslationControlStrategy::EndDrag()
{
	DragTargets.clear();
	InitialTargetLocations.clear();
}

void GizmoTranslationControlStrategy::SetDrawEnable(bool bEnable)
{
	for (FGizmoHandle* handle : Handles)
	{
		handle->RenderObj->bIsVisible = bEnable;
	}
}

FGizmoHandle* GizmoTranslationControlStrategy::GetCurrHandle() const
{
	for (FGizmoHandle* handle : Handles)
	{
		if (handle->AxisType == Controller->CurrentAxis)
		{
			return handle;
		}
	}
	return nullptr;
}

bool GizmoTranslationControlStrategy::IsPlaneAxis(GizmoControllerAxis type) const
{
	return type == GizmoControllerAxis::XY || type == GizmoControllerAxis::XZ || type == GizmoControllerAxis::YZ;
}
