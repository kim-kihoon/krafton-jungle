#include "GizmoComponent.h"
#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "EngineTypes.h"
#include "Editor/Gizmo/GizmoTranslationControlStrategy.h"
#include "Editor/Gizmo/GizmoRotationControlStrategy.h"
#include "Editor/Gizmo/GizmoScaleControlStrategy.h"
#include "InputManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderObject.h"
#include "ResourceManager.h"

UGizmoComponent::UGizmoComponent()
{
	TranslationStrategy = new GizmoTranslationControlStrategy(this);
	RotationStrategy = new GizmoRotationControlStrategy(this);
	ScaleStrategy = new GizmoScaleControlStrategy(this);
	CurrentStrategy = TranslationStrategy;
}

UGizmoComponent::~UGizmoComponent()
{
	delete ScaleStrategy;
	delete RotationStrategy;
	delete TranslationStrategy;
}

void UGizmoComponent::SetRelativeLocation(const FVector& newLocation)
{
	USceneComponent::SetRelativeLocation(newLocation);
	MarkRenderStateDirty();
}

void UGizmoComponent::SetRelativeScale3D(const FVector& newScale)
{
	USceneComponent::SetRelativeScale3D(newScale);
	MarkRenderStateDirty();
}

void UGizmoComponent::OnComponentAdded()
{
	CreateRenderObjects();
}

void UGizmoComponent::Update(float deltaTime)
{
	if (AttachedComponent == nullptr) return;

	UCameraComponent* camera = UCameraComponent::GetMainCamera();

	FVector mousePos = InputManager::GetInstance().MousePos;
	Ray ray = camera->ScreenPointToRay(mousePos.x, mousePos.y);

	if (InputManager::GetInstance().IsMouseHold(VK_LBUTTON))
		if (bDragging) UpdateDrag(ray);

	if (InputManager::GetInstance().IsMouseUp(VK_LBUTTON))
		if (bDragging) EndDrag();

	if (!InputManager::GetInstance().IsMouseHold(VK_LBUTTON))
		HoverAxis = TryPick(ray);

	if (!InputManager::GetInstance().IsMouseHold(VK_LBUTTON) &&
		!InputManager::GetInstance().IsMouseHold(VK_RBUTTON))
	{
		if (InputManager::GetInstance().IsKeyDown('W')) SetControlStrategy(GizmoControllerType::Translation);
		else if (InputManager::GetInstance().IsKeyDown('E')) SetControlStrategy(GizmoControllerType::Rotation);
		else if (InputManager::GetInstance().IsKeyDown('R')) SetControlStrategy(GizmoControllerType::Scale);
		else if (InputManager::GetInstance().IsKeyDown(VK_SPACE))
		{
			Type = static_cast<GizmoControllerType>((static_cast<int32>(Type) + 1) % static_cast<int32>(GizmoControllerType::End));
			SetControlStrategy(Type);
		}
	}

	SetRelativeLocation(AttachedComponent->GetRelativeLocation());
	SetRelativeQuaternion(AttachedComponent->GetRelativeQuaternion());

	float Scale = BaseScale;
	if (!camera->IsOrthographic)
	{
		FVector CamPos = camera->GetRelativeLocation();
		FVector CompPos = AttachedComponent->GetRelativeLocation();
		float Dist = (CamPos - CompPos).Length();
		Scale *= Dist;
	}
	else
	{
		Scale *= 15.0f;
	}

	SetRelativeScale3D(FVector(Scale, Scale, Scale));

	if (!bLocalSpace && Type != GizmoControllerType::Scale)
	{
		SetRelativeQuaternion(FQuat());
	}

	CurrentStrategy->Update();

	UpdateRenderObjects();
}

void UGizmoComponent::CreateRenderObjects()
{
	TranslationStrategy->CreateRenderObjects();
	RotationStrategy->CreateRenderObjects();
	ScaleStrategy->CreateRenderObjects();
}

void UGizmoComponent::UpdateRenderObjects()
{
	if (AttachedComponent == nullptr) return;
	if (CurrentStrategy == nullptr) return;

	CurrentStrategy->UpdateRenderObjects();

	return;
}

float UGizmoComponent::GetClosestPointOnAxis(const Ray& ray, const FVector& axisOrigin, const FVector& axisDir)
{
	FVector u = ray.Direction;
	FVector v = axisDir;
	FVector w = ray.Origin - axisOrigin;

	float a = Dot(u, u);
	float b = Dot(u, v);
	float c = Dot(v, v);
	float d = Dot(u, w);
	float e = Dot(v, w);

	float denominator = a * c - b * b;
	if (denominator < 1e-6f) return 0.0f;

	return (a * e - b * d) / denominator;
}

void UGizmoComponent::BeginDrag(const Ray& ray)
{
	if (bDragging) return;
	if (AttachedComponent == nullptr) return;

	bDragging = true;

	CurrentStrategy->BeginDrag(ray);
}

void UGizmoComponent::UpdateDrag(const Ray& ray)
{
	if (!bDragging) return;
	if (AttachedComponent == nullptr) return;

	CurrentStrategy->UpdateDrag(ray);
}

void UGizmoComponent::EndDrag()
{
	if (!bDragging) return;

	bDragging = false;

	CurrentStrategy->EndDrag();
}

void UGizmoComponent::SetControlStrategy(GizmoControllerType type)
{
	Type = type;
	CurrentStrategy->SetDrawEnable(false);
	switch (Type)
	{
	case GizmoControllerType::Translation:
		CurrentStrategy = TranslationStrategy;
		break;
	case GizmoControllerType::Rotation:
		CurrentStrategy = RotationStrategy;
		break;
	case GizmoControllerType::Scale:
		CurrentStrategy = ScaleStrategy;
		break;
	}
	CurrentStrategy->SetDrawEnable(bDrawEnabled);
}

GizmoControllerAxis UGizmoComponent::TryPick(Ray& ray)
{
	if (!bDrawEnabled)
	{
		return GizmoControllerAxis::None;
	}

	TArray<FGizmoHandle*> handles = CurrentStrategy->GetHandles();

	ResourceManager* resourceManager = ResourceManager::GetInstance();

	bool bHit = false;
	float closestT = FLT_MAX;

	for (size_t i = 0; i < handles.size(); ++i)
	{
		Ray localRay;
		localRay.Direction = (handles[i]->LocalMatrix * GetRelativeMatrix()).Inverse().TransformVector(ray.Direction).Normalize();
		localRay.Origin = (handles[i]->LocalMatrix * GetRelativeMatrix()).Inverse().TransformPoint(ray.Origin);

		FGeometry& geometry = *handles[i]->RenderObj->Geometry;

		if (geometry.IndexCount > 0)
		{
			for (int j = 0; j + 2 < geometry.IndexCount; j += 3)
			{
				const FVector& v0 = geometry.Vertices[geometry.Indices[j]];
				const FVector& v1 = geometry.Vertices[geometry.Indices[j + 1]];
				const FVector& v2 = geometry.Vertices[geometry.Indices[j + 2]];
				float t;
				if (ray.IntersectsTriangle(v0, v1, v2, t, localRay))
				{
					if (t < closestT)
					{
						closestT = t;
						bHit = true;
					}
				}
			}
		}
		else
		{
			for (int j = 0; j < geometry.Vertices.size(); j += 3)
			{
				const FVector& v0 = geometry.Vertices[j];
				const FVector& v1 = geometry.Vertices[j + 1];
				const FVector& v2 = geometry.Vertices[j + 2];

				float t;

				if (ray.IntersectsTriangle(v0, v1, v2, t, localRay))
				{
					if (t < closestT)
					{
						closestT = t;
						bHit = true;
					}
				}
			}
		}

		if (bHit)
		{
			return handles[i]->AxisType;
		}
	}
	return GizmoControllerAxis::None;
}

REGISTER_CLASS(UGizmoComponent)