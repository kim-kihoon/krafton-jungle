#pragma once

#include "EngineTypes.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"

struct Ray;
struct FMatrix;
struct RenderObject;
struct FGizmoHandle;

class IGizmoControlStrategy
{
public:
	virtual ~IGizmoControlStrategy() = default;

	virtual void CreateRenderObjects() = 0;

	virtual void Update() = 0;
	virtual void UpdateRenderObjects() = 0;

	virtual TArray<FGizmoHandle*> GetHandles() = 0;

	virtual void BeginDrag(const Ray& ray) = 0;
	virtual void UpdateDrag(const Ray& ray) = 0;
	virtual void EndDrag() = 0;

	virtual void SetDrawEnable(bool bEnable) = 0;
};

