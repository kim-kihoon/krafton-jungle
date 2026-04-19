#include "GizmoScaleControlStrategy.h"
#include "Component/GizmoComponent.h"
#include "Component/SceneComponent.h"
#include "ResourceManager.h"
#include "Object.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderObject.h"
#include "Component/SceneComponent.h"
#include "Editor/Picker.h"
#include "Logger.h"

GizmoScaleControlStrategy::GizmoScaleControlStrategy(UGizmoComponent* controller)
{
	Controller = controller;
}

GizmoScaleControlStrategy::~GizmoScaleControlStrategy()
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

void GizmoScaleControlStrategy::CreateRenderObjects()
{
	ResourceManager* resourceManager = ResourceManager::GetInstance();

	Handles.resize(7);

	Handles[0] = CreateHandle(FVector::Forward(), FVector::Up(), GizmoControllerAxis::X, FColor::Red(), "GizmoScale", FMatrix::RotationYMatrix(90.0f) * FMatrix::TranslationMatrix(0.5f, 0.0f, 0.0f));
	Handles[1] = CreateHandle(FVector::Right(), FVector::Up(), GizmoControllerAxis::Y, FColor::Green(), "GizmoScale", FMatrix::RotationXMatrix(-90.0f) * FMatrix::TranslationMatrix(0.0f, 0.5f, 0.0f));
	Handles[2] = CreateHandle(FVector::Up(), FVector::Right(), GizmoControllerAxis::Z, FColor::Blue(), "GizmoScale", FMatrix::TranslationMatrix(0.0f, 0.0f, 0.5f));

	Handles[3] = CreateHandle(FVector(1.0f, 1.0f, 0.0f).Normalize(), FVector::Up(), GizmoControllerAxis::XY, FColor(1.0f, 1.0f, 1.0f, 1.0f), "GizmoScaleLine", FMatrix::EulerRotationMatrix(-90.0f, 0.0f, 0.0f));
	Handles[4] = CreateHandle(FVector(1.0f, 0.0f, 1.0f).Normalize(), FVector::Right(), GizmoControllerAxis::XZ, FColor(1.0f, 0.0f, 1.0f, 1.0f), "GizmoScaleLine", FMatrix::IdentityMatrix());
	Handles[5] = CreateHandle(FVector(0.0f, 1.0f, 1.0f).Normalize(), FVector::Forward(), GizmoControllerAxis::YZ, FColor(0.0f, 1.0f, 1.0f, 1.0f), "GizmoScaleLine", FMatrix::RotationZMatrix(90.0f));

	Handles[6] = CreateHandle(FVector(1.0f, 1.0f, 1.0f).Normalize(), FVector::Zero(), GizmoControllerAxis::XYZ, FColor(1.0f, 1.0f, 1.0f, 1.0f), "GizmoScaleBox", FMatrix::IdentityMatrix());

	for (size_t i = 0; i < Handles.size(); i++)
	{
		FLevel::GetInstance().RegisterRenderObject(Handles[i]->RenderObj);
	}
}

void GizmoScaleControlStrategy::Update()
{
}

void GizmoScaleControlStrategy::UpdateRenderObjects()
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

void GizmoScaleControlStrategy::BeginDrag(const Ray& ray)
{
	InitialObjectScale = Controller->GetAttachedComponent()->GetRelativeScale3D();
	DragTargets = Controller->GetAttachedComponents();
	if (DragTargets.empty() && Controller->GetAttachedComponent())
	{
		DragTargets.push_back(Controller->GetAttachedComponent());
	}

	InitialTargetScales.clear();
	for (USceneComponent* target : DragTargets)
	{
		InitialTargetScales.push_back(target->GetRelativeScale3D());
	}

	FGizmoHandle* currHandle = GetCurrHandle();
	if (!currHandle) return;

	FVector axisDir = Controller->GetRelativeMatrix().TransformVector(currHandle->AxisDirection).Normalize();
	FVector worldOrigin = Controller->GetRelativeMatrix().TransformPoint(FVector::Zero());

	if (currHandle->AxisType == GizmoControllerAxis::XYZ)
	{
		float t;
		if (ray.IntersectsPlane(worldOrigin, Controller->GetRelativeMatrix().TransformVector(FVector::Up()), t))
		{
			FVector hitPoint = ray.Origin + ray.Direction * t;
			InitialDragOffset = hitPoint;
		}
	}
	else
	{
		InitialWorldPos = worldOrigin;
		InitialDragOffset.x = Controller->GetClosestPointOnAxis(ray, worldOrigin, axisDir);
	}
}

void GizmoScaleControlStrategy::ApplyScaleDelta(const FVector& deltaScale)
{
	if (DragTargets.size() != InitialTargetScales.size())
	{
		return;
	}

	for (size_t i = 0; i < DragTargets.size(); ++i)
	{
		DragTargets[i]->SetRelativeScale3D(InitialTargetScales[i] + deltaScale);
	}
}

void GizmoScaleControlStrategy::UpdateDrag(const Ray& ray)
{
	FGizmoHandle* currHandle = GetCurrHandle();
	if (!currHandle) return;

	FVector axisDir = Controller->GetRelativeMatrix().TransformVector(currHandle->AxisDirection).Normalize();
	FVector worldOrigin = Controller->GetRelativeMatrix().TransformPoint(FVector::Zero());

	float deltaT = 0.0f;
	if (currHandle->AxisType == GizmoControllerAxis::XYZ)
	{
		float t;
		if (ray.IntersectsPlane(worldOrigin, Controller->GetRelativeMatrix().TransformVector(FVector::Up()).Normalize(), t))
		{
			FVector hitPoint = ray.Origin + ray.Direction * t;
			FVector delta = hitPoint - InitialDragOffset;
			deltaT = delta.Length();
		}
	}
	else
	{
		float currentT = Controller->GetClosestPointOnAxis(ray, InitialWorldPos, axisDir);
		deltaT = currentT - InitialDragOffset.x;
	}

	FVector newScale = InitialObjectScale;

	switch (Controller->CurrentAxis)
	{
	case GizmoControllerAxis::X:
		newScale.x += deltaT;
		break;
	case GizmoControllerAxis::Y:
		newScale.y += deltaT;
		break;
	case GizmoControllerAxis::Z:
		newScale.z += deltaT;
		break;
	case GizmoControllerAxis::XY:
		newScale.x += deltaT;
		newScale.y += deltaT;
		break;
	case GizmoControllerAxis::XZ:
		newScale.x += deltaT;
		newScale.z += deltaT;
		break;
	case GizmoControllerAxis::YZ:
		newScale.y += deltaT;
		newScale.z += deltaT;
		break;
	case GizmoControllerAxis::XYZ:
		newScale.x += deltaT;
		newScale.y += deltaT;
		newScale.z += deltaT;
		break;
	default:
		break;
	}
	
	ApplyScaleDelta(newScale - InitialObjectScale);
}

void GizmoScaleControlStrategy::EndDrag()
{
	DragTargets.clear();
	InitialTargetScales.clear();
}

void GizmoScaleControlStrategy::SetDrawEnable(bool bEnable)
{
	for (FGizmoHandle* handle : Handles)
	{
		handle->RenderObj->bIsVisible = bEnable;
	}
}

FGizmoHandle* GizmoScaleControlStrategy::GetCurrHandle() const
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
