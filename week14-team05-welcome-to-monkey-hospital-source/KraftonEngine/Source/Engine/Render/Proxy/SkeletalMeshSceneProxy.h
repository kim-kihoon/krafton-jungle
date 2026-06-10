#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Types/VertexTypes.h"
#include "Render/Types/ViewTypes.h"

class USkeletalMeshComponent;
struct FDrawCommandBuffer;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
	~FSkeletalMeshSceneProxy() override;

	void UpdateMaterial() override;
	void UpdateMesh() override;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;
	bool PrepareGpuSkinningDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const;
	bool PrepareCpuBoneHeatMapDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, int32 SelectedBoneIndex, FDrawCommandBuffer& OutBuffer) const;
	bool PrepareCpuClothMaxDistanceOverlayDrawBuffer(
		ID3D11Device* Device,
		ID3D11DeviceContext* Context,
		int32 LODIndex,
		int32 ClothIndex,
		FDrawCommandBuffer& OutBuffer,
		uint32& OutFirstIndex,
		uint32& OutIndexCount) const;
	ID3D11ShaderResourceView* GetSkinMatrixSRV(ID3D11Device* Device, ID3D11DeviceContext* Context) const;
	ESkinningMode GetEffectiveSkinningMode() const;
	
private:
	void RebuildSectionDraws();
	USkeletalMeshComponent* GetSkeletalMeshComponent() const;
	void ReleaseSkinMatrixBuffer() const;
	bool UpdateSkinMatrixBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const;

private:
	mutable FDynamicVertexBuffer DynamicVertexBuffer;
	mutable FDynamicVertexBuffer BoneHeatMapVertexBuffer;
	mutable FDynamicVertexBuffer ClothMaxDistanceVertexBuffer;
	mutable TArray<FVertexPNCTT> BoneHeatMapVertices;
	mutable TArray<FVertexPNCTT> ClothMaxDistanceVertices;
	mutable uint64 UploadedSkinnedRevision = 0;
	mutable uint64 UploadedBoneHeatMapRevision = 0;
	mutable int32 UploadedBoneHeatMapBoneIndex = -1;
	uint32 CachedDynamicVertexCount = 0;
	mutable bool bDynamicBufferNeedsCreate = true;
	mutable bool bBoneHeatMapBufferNeedsCreate = true;
	mutable bool bClothMaxDistanceBufferNeedsCreate = true;

	mutable ID3D11Buffer* SkinMatrixBuffer = nullptr;
	mutable ID3D11ShaderResourceView* SkinMatrixSRV = nullptr;
	mutable uint32 SkinMatrixCapacity = 0;
	mutable uint64 UploadedSkinMatrixRevision = 0;
};
