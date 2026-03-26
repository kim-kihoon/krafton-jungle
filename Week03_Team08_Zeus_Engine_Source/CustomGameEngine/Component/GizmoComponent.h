#pragma once

#include "EngineTypes.h"
#include "Editor/Gizmo/GizmoControlStrategy.h"
#include "Math/Matrix.h"
#include "Editor/Picker.h"
#include "Component/PrimitiveComponent.h"

enum class GizmoControllerAxis : int32
{
	None = 0,
	X = 1 << 0,
	Y = 1 << 1,
	Z = 1 << 2,
	XY = X | Y,
	XZ = X | Z,
	YZ = Y | Z,
	XYZ = X | Y | Z
};

enum class GizmoControllerType : int32
{
	Translation,
	Rotation,
	Scale,
	End
};

class URenderer;

class UGizmoComponent : public UPrimitiveComponent
{
	DECLARE_OBJECT(UGizmoComponent, UPrimitiveComponent)
public:
	UGizmoComponent();
	virtual ~UGizmoComponent() override;

public:
	void OnComponentAdded() override;
	void Update(float deltaTime) override;
	void CreateRenderObjects() override;
	void UpdateRenderObjects() override;
	void SetRelativeLocation(const FVector& newLocation) override;
	void SetRelativeScale3D(const FVector& newScale) override;

	void BeginDrag(const Ray& ray);
	void UpdateDrag(const Ray& ray);
	void EndDrag();

	bool bIsDragging() const { return bDragging;  }

	GizmoControllerType GetControlStrategyType() const { return Type; }
	IGizmoControlStrategy* GetControlStrategy() const { return CurrentStrategy; }
	void SetControlStrategy(GizmoControllerType type);

	float GetClosestPointOnAxis(const Ray& ray, const FVector& axisOrigin, const FVector& axisDir);

	GizmoControllerAxis TryPick(Ray& ray);

	void AttachTo(USceneComponent* comp, const TArray<USceneComponent*>& selectedComponents = {}) {
		AttachedComponent = comp;
		AttachedComponents = selectedComponents;

		if (comp)
			SetDrawEnable(true);
		else
			SetDrawEnable(false);
	}
	USceneComponent* GetAttachedComponent() const { return AttachedComponent; }
	const TArray<USceneComponent*>& GetAttachedComponents() const { return AttachedComponents; }

	void SetDrawEnable(bool bEnable)
	{
		bDrawEnabled = bEnable;

		if (CurrentStrategy) CurrentStrategy->SetDrawEnable(bEnable);
	}

public:
	GizmoControllerAxis HoverAxis = GizmoControllerAxis::None;
	GizmoControllerAxis CurrentAxis = GizmoControllerAxis::None;

	bool bLocalSpace = true;

private:
	USceneComponent* AttachedComponent = nullptr;
	TArray<USceneComponent*> AttachedComponents;

	GizmoControllerType Type = GizmoControllerType::Translation;

	bool bDragging = false;

	IGizmoControlStrategy* CurrentStrategy = nullptr;

	IGizmoControlStrategy* TranslationStrategy = nullptr;
	IGizmoControlStrategy* RotationStrategy = nullptr;
	IGizmoControlStrategy* ScaleStrategy = nullptr;

	float BaseScale = 0.1f;

	bool bDrawEnabled = false;
};
