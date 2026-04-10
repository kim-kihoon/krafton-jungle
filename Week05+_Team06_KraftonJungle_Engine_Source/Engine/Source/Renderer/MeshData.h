#pragma once
#include <d3d11.h>

#include "Renderer/RenderMesh.h"
#include "CoreMinimal.h"


struct ENGINE_API FStaticMesh : public FRenderMesh
{
	virtual bool UpdateVertexAndIndexBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) override;
	virtual bool CreateVertexAndIndexBuffer(ID3D11Device* Device) override;
};

struct ENGINE_API FDynamicMesh : public FRenderMesh
{
	virtual bool UpdateVertexAndIndexBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) override;
	virtual bool CreateVertexAndIndexBuffer(ID3D11Device* Device) override;

private:
	uint32 MaxVertexCapacity = 0;
	uint32 MaxIndexCapacity = 0;
};

class ENGINE_API UStaticMesh : public UObject
{
public:
	DECLARE_RTTI(UStaticMesh, UObject)
	GENERATE_SHALLOW_CLONE(UStaticMesh);
	virtual ~UStaticMesh() = default;

	FBoxSphereBounds LocalBounds;
	const FString& GetAssetPathFileName() const;

	void SetStaticMeshAsset(std::shared_ptr<FStaticMesh> InStaticMesh) { StaticMeshAsset = InStaticMesh; }
	FStaticMesh* GetRenderData() const { return StaticMeshAsset.get(); }
	int32 GetNumSections() const { return StaticMeshAsset ? StaticMeshAsset->GetNumSection() : 0; }

	const TArray<std::shared_ptr<FMaterial>>& GetDefaultMaterials() const { return DefaultMaterials; }
	void AddDefaultMaterial(const std::shared_ptr<FMaterial>& InMaterial) { DefaultMaterials.push_back(InMaterial); }

private:
	std::shared_ptr<FStaticMesh> StaticMeshAsset;
	TArray<std::shared_ptr<FMaterial>> DefaultMaterials;
};
