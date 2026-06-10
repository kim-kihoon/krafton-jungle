#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

class UPhysicsAssetPreviewComponent;

class FPhysicsAssetPreviewSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FPhysicsAssetPreviewSceneProxy(UPhysicsAssetPreviewComponent* InComponent);
	~FPhysicsAssetPreviewSceneProxy() override;

	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void UpdateMesh() override;

private:
	UMaterial* PreviewMaterial = nullptr;
	FConstantBuffer PreviewMaterialCB;
};
