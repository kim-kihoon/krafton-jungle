#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"

class USkeletalMeshComponent;
struct FDrawCommandBuffer;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
	~FSkeletalMeshSceneProxy() override;

	void UpdateMaterial() override;
	void UpdateMesh() override;

	const char* GetVertexShaderEntryName() const override;
	bool WantsGpuSkinning(const FPrimitiveDrawOptions& Options) const override;
	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;
	bool PrepareGpuSkinningDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;
	bool PrepareDrawCommandBindings(ID3D11Device* Device, ID3D11DeviceContext* Context,
		const FPrimitiveDrawOptions& Options, FDrawCommand& OutCommand, int32) const override;
	
private:
	void RebuildSectionDraws();
	USkeletalMeshComponent* GetSkeletalMeshComponent() const;

private:
	mutable FDynamicVertexBuffer DynamicVertexBuffer;
	mutable FStructuredBuffer SkinMatrixBuffer;
	mutable FConstantBuffer SkeletalRenderCB;
	mutable uint64 UploadedSkinnedRevision = 0;
	mutable uint64 UploadedSkinMatrixRevision = 0;
	uint32 CachedDynamicVertexCount = 0;
	mutable bool bDynamicBufferNeedsCreate = true;
};
