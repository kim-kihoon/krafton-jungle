#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"

class UArrowComponent;

class FArrowSceneProxy : public FPrimitiveSceneProxy
{
public:
	FArrowSceneProxy(UArrowComponent* InComponent);
	void UpdateMesh() override;

private:
	UArrowComponent* GetArrowComponent() const;
};

