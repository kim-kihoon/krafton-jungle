#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

class UPhotoPolaroidComponent;

class FPhotoPolaroidSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FPhotoPolaroidSceneProxy(UPhotoPolaroidComponent* InComponent);
	~FPhotoPolaroidSceneProxy() override;

	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void UpdateTransform() override;
	void UpdateMaterial() override;
	void UpdateMesh() override;
	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;

private:
	UPhotoPolaroidComponent* GetPhotoComponent() const;
	void RebuildMesh(ID3D11Device* Device) const;

	mutable FMeshBuffer MeshBufferStorage;
	mutable FConstantBuffer PhotoDevelopCB;
	UMaterial* FrameMaterial = nullptr;
	UMaterial* PhotoMaterial = nullptr;
	float CachedDisplayTime = -1.0f;
	float CachedDevelopTime = -1.0f;
};
