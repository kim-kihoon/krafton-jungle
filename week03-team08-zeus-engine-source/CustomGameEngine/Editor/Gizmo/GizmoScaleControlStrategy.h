#pragma once

#include "GizmoControlStrategy.h"
#include "GizmoHandle.h"
#include "Math/Vector.h"

class UGizmoComponent;
struct RenderObject;
class USceneComponent;

class GizmoScaleControlStrategy : public IGizmoControlStrategy
{
public:
	GizmoScaleControlStrategy(UGizmoComponent* controller);
	~GizmoScaleControlStrategy() override;

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
	void ApplyScaleDelta(const FVector& deltaScale);

private:
	UGizmoComponent* Controller;

	FVector InitialWorldPos;
	FVector InitialObjectScale;
	FVector InitialDragOffset = 0.0f;
	TArray<USceneComponent*> DragTargets;
	TArray<FVector> InitialTargetScales;

	TArray<FGizmoHandle*> Handles;
};
