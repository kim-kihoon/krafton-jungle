#include "Picker.h"
#include "World.h"
#include "Renderer/Renderer.h"
#include "Renderer/Geometry.h"
#include "Renderer/RenderState.h"
#include "Component/CameraComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/GizmoComponent.h"
#include "Editor.h"
#include "Gizmo.h"
#include "Scene.h"
#include "Math/Vector4.h"
#include "Logger.h"
#include "InputManager.h"

#include <algorithm>

UPicker::UPicker()
{
}

UPicker::~UPicker()
{
}

void UPicker::Pick(int ScreenX, int ScreenY, UScene* Scene)
{
	Ray ray = CameraPtr->ScreenPointToRay(ScreenX, ScreenY);
	URenderer* Renderer = GetWorld().GetRenderer();
	uint32 ShowFlags = Renderer->GetShowFlags();

	UGizmoComponent* controller = Gizmo::GetInstance().GetController();

	bool bIsGizmoVisible = (ShowFlags & (uint32)EShowFlag::Gizmo);

	if (controller && bIsGizmoVisible)
	{
		GizmoControllerAxis selectedAxis = controller->TryPick(ray);
		if (selectedAxis != GizmoControllerAxis::None)
		{
			controller->CurrentAxis = selectedAxis;
			controller->BeginDrag(ray);
			return;
		}
	}

	const bool bCtrlDown = InputManager::GetInstance().CurrentState[VK_CONTROL];
	const bool bShiftDown = InputManager::GetInstance().CurrentState[VK_SHIFT];
	const bool bMultiSelect = bCtrlDown || bShiftDown;

	TArray<TPair<float, USceneComponent*>> RayHits;

	for (USceneComponent* sc : Scene->SceneComponents)
	{
		UPrimitiveComponent* Comp = Cast<UPrimitiveComponent>(sc);
		if (Comp == nullptr || !(Comp->Pickable) || !(Comp->bIsVisible))
			continue;

		if (!(ShowFlags & (uint32)EShowFlag::StaticMesh))
			continue;

		float Distance = 0.f;
		if (ray.Intersects(Comp, Distance))
		{
			RayHits.push_back({ Distance, Comp });
		}
	}

	if (!RayHits.empty())
	{
		std::sort(RayHits.begin(), RayHits.end());

		if (bCtrlDown)
		{
			EditorPtr->SelectComponent(RayHits.front().second, true, false);
		}
		else if (bShiftDown)
		{
			EditorPtr->SelectComponent(RayHits.front().second, false, true);
		}
		else
		{
			int nextIndex = 0;
			for (int i = 0; i < static_cast<int>(RayHits.size()); ++i)
			{
				if (RayHits[i].second == EditorPtr->GetSelectedComponent())
				{
					nextIndex = (i + 1) % static_cast<int>(RayHits.size());
					break;
				}
			}
			EditorPtr->SelectComponent(RayHits[nextIndex].second);
		}

		Scene->IsCompSelected = EditorPtr->HasSelection();
	}
	else
	{
		if (!bMultiSelect)
		{
			EditorPtr->SelectComponent(nullptr);
		}
		Scene->IsCompSelected = EditorPtr->HasSelection();
	}
}

REGISTER_CLASS(UPicker)
