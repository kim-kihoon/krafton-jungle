#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

class UClothComponent;
struct FDrawCommandBuffer;

class FClothSceneProxy : public FPrimitiveSceneProxy
{
public:
	FClothSceneProxy(UClothComponent* InComponent);

	void UpdateMaterial() override;
	void UpdateMesh() override;
	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;

private:
	void RebuildSectionDraws();
	UClothComponent* GetClothComponent() const;

private:
	mutable FDynamicVertexBuffer DynamicVertexBuffer;
	mutable FIndexBuffer IndexBuffer;
	mutable uint64 UploadedRenderRevision = 0;
	mutable uint64 UploadedTopologyRevision = 0;
	mutable bool bBuffersDirty = true;
	mutable uint32 CachedVertexCapacity = 0;
};
