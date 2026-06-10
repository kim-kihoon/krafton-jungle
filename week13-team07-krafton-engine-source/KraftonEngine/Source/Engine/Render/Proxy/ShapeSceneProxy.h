#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"

class UShapeComponent;

// Shape 컴포넌트용 에디터 프록시 — 콜리전 와이어는 CollisionDebugDraw 경로에서만 그린다.
class FShapeSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FShapeSceneProxy(UShapeComponent* InComponent);

	void UpdateVisibility() override;

private:
	bool bDrawOnlyIfSelected = false;
};
