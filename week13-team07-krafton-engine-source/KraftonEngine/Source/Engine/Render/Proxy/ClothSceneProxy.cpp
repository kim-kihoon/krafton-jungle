#include "Render/Proxy/ClothSceneProxy.h"

#include "Component/Primitive/ClothComponent.h"
#include "Materials/MaterialManager.h"
#include "Object/Object.h"
#include "Render/Command/DrawCommand.h"

FClothSceneProxy::FClothSceneProxy(UClothComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	RebuildSectionDraws();
}

void FClothSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
}

void FClothSceneProxy::UpdateMesh()
{
	RebuildSectionDraws();
	bBuffersDirty = true;
	UploadedRenderRevision = 0;
	UploadedTopologyRevision = 0;
}

bool FClothSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	UClothComponent* ClothComponent = GetClothComponent();
	if (!ClothComponent)
	{
		return false;
	}

	const TArray<FVertexPNCTT>& Vertices = ClothComponent->GetRenderVertices();
	const TArray<uint32>& Indices = ClothComponent->GetRenderIndices();
	if (Vertices.empty() || Indices.empty())
	{
		return false;
	}

	if (bBuffersDirty || UploadedTopologyRevision != ClothComponent->GetTopologyRevision())
	{
		IndexBuffer.Release();
		IndexBuffer.Create(Device, Indices.data(), static_cast<uint32>(Indices.size()), static_cast<uint32>(Indices.size() * sizeof(uint32)));
		UploadedTopologyRevision = ClothComponent->GetTopologyRevision();
		bBuffersDirty = false;
	}

	if (!DynamicVertexBuffer.GetBuffer() || CachedVertexCapacity < static_cast<uint32>(Vertices.size()))
	{
		CachedVertexCapacity = static_cast<uint32>(Vertices.size());
		DynamicVertexBuffer.Release();
		DynamicVertexBuffer.Create(Device, CachedVertexCapacity, sizeof(FVertexPNCTT));
		UploadedRenderRevision = 0;
	}

	if (UploadedRenderRevision != ClothComponent->GetRenderRevision())
	{
		if (!DynamicVertexBuffer.Update(Context, Vertices.data(), static_cast<uint32>(Vertices.size())))
		{
			return false;
		}

		UploadedRenderRevision = ClothComponent->GetRenderRevision();
	}

	OutBuffer = {};
	OutBuffer.VB = DynamicVertexBuffer.GetBuffer();
	OutBuffer.VBStride = DynamicVertexBuffer.GetStride();
	OutBuffer.IB = IndexBuffer.GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

void FClothSceneProxy::RebuildSectionDraws()
{
	SectionDraws.clear();

	UClothComponent* ClothComponent = GetClothComponent();
	if (!ClothComponent)
	{
		return;
	}

	FMeshSectionDraw Draw;
	Draw.FirstIndex = 0;
	Draw.IndexCount = static_cast<uint32>(ClothComponent->GetRenderIndices().size());
	Draw.Material = ClothComponent->GetClothMaterial();
	if (!Draw.Material)
	{
		Draw.Material = FMaterialManager::Get().GetOrCreateMaterial("None");
	}

	SectionDraws.push_back(Draw);
}

UClothComponent* FClothSceneProxy::GetClothComponent() const
{
	return Cast<UClothComponent>(GetOwner());
}
