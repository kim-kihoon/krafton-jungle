#pragma once
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Component/GizmoComponent.h"

struct RenderObject;

struct FGizmoHandle
{
	RenderObject* RenderObj;
	FVector AxisDirection;
	FVector PlaneNormal;
	FColor Color;
	GizmoControllerAxis AxisType;
	FMatrix LocalMatrix;

	FGizmoHandle()
	{
	}

	~FGizmoHandle()
	{
		delete RenderObj;
	}
};