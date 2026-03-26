#pragma once

#include "GizmoControlStrategy.h"
#include "GizmoHandle.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"

class UGizmoComponent;
struct RenderObject;
class USceneComponent;

class GizmoTranslationControlStrategy : public IGizmoControlStrategy
{
public:
	GizmoTranslationControlStrategy(UGizmoComponent* controller);
	~GizmoTranslationControlStrategy() override;

public:
	virtual void CreateRenderObjects() override;

	virtual void Update() override;
	virtual void UpdateRenderObjects() override;

	virtual TArray<FGizmoHandle*> GetHandles() override { return Handles; }

	virtual void BeginDrag(const Ray& ray) override;
	virtual void UpdateDrag(const Ray& ray) override;
	virtual void EndDrag() override;

	virtual void SetDrawEnable(bool bEnable) override;

private:
	FGizmoHandle* GetCurrHandle() const;
	bool IsPlaneAxis(GizmoControllerAxis type) const;
	void ApplyTranslationDelta(const FVector& delta);

private:
	UGizmoComponent* Controller;

	FVector InitialObjectLocation;
	FVector InitialWorldPos;
	FVector InitialDragOffset;
	TArray<USceneComponent*> DragTargets;
	TArray<FVector> InitialTargetLocations;

	TArray<FGizmoHandle*> Handles;
};
