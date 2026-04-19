#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"

class UDecalComponent;

class FDecalSceneProxy : public FPrimitiveSceneProxy
{
public:
	FDecalSceneProxy(UDecalComponent* InComponent);

	void UpdateMaterial() override;
	void UpdateMesh() override;
	void UpdatePerViewport(const FRenderBus& Bus) override;

private:
	UDecalComponent* GetDecalComponent() const;
};

