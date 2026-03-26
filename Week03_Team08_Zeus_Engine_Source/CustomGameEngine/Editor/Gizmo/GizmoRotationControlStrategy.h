#pragma once

#include "GizmoControlStrategy.h"
#include "GizmoHandle.h"
#include "Math/Vector.h"
#include "Math/Quaternion.h"
#include "Math/Matrix.h"

class UGizmoComponent;
struct RenderObject;

class GizmoRotationControlStrategy : public IGizmoControlStrategy
{
public:
	GizmoRotationControlStrategy(UGizmoComponent* controller);
	~GizmoRotationControlStrategy() override;

public:
	virtual void CreateRenderObjects() override;

	virtual void Update() override;
	virtual void UpdateRenderObjects() override;

	virtual TArray<FGizmoHandle*> GetHandles() override { return Handles; }

	virtual void BeginDrag(const Ray& ray) override;
	virtual void UpdateDrag(const Ray& ray) override;
	virtual void EndDrag() override;

	virtual void SetDrawEnable(bool bEnable) override;

	bool bEnableSnap = true;

private:
	UGizmoComponent* Controller;

	POINT InitialCursorPos;
	FQuat InitialObjectQuaternion;

	float AccumulatedAngle = 0.0f;
	float SnapStep = 15.0f;

	TArray<FGizmoHandle*> Handles;

	TArray<USceneComponent*> DragTargets;
	TArray<FQuat> InitialTargetQuaternions;
};
