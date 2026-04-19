#pragma once
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Pipeline/RenderConstants.h"

class UMeshDecalComponent;

class FDecalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FDecalMeshSceneProxy(UMeshDecalComponent* InComponent);

	virtual void UpdateMaterial() override;
	virtual void UpdateMesh() override;
	virtual void UpdateVisibility() override;
	virtual void UpdateTransform() override;
	void UpdateOpacity();

	void UpdateFade();

private:
	UMeshDecalComponent* GetDecalComponent() const;
	FMeshDecalConstants Const;
};
