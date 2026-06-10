#include "SkeletalMeshSceneProxy.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Render/Command/DrawCommand.h"
#include "Runtime/Engine.h"
#include "Profiling/Time/Timer.h"
#include "Profiling/Stats/Stats.h"

#include <algorithm>
#include <cstring>

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::SkeletalMesh;
}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
	ReleaseSkinMatrixBuffer();
}   

USkeletalMeshComponent* FSkeletalMeshSceneProxy::GetSkeletalMeshComponent() const
{
	return static_cast<USkeletalMeshComponent*>(GetOwner());
}

void FSkeletalMeshSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
};

void FSkeletalMeshSceneProxy::UpdateMesh()
{
	MeshBuffer = GetOwner()->GetMeshBuffer();
	RebuildSectionDraws();

	CachedDynamicVertexCount = 0;
	UploadedSkinnedRevision = 0;
	UploadedBoneHeatMapRevision = 0;
	UploadedBoneHeatMapBoneIndex = -1;
	UploadedSkinMatrixRevision = 0;
	bDynamicBufferNeedsCreate = true;
	bBoneHeatMapBufferNeedsCreate = true;
	bClothMaxDistanceBufferNeedsCreate = true;
	ReleaseSkinMatrixBuffer();

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (Asset)
	{
		CachedDynamicVertexCount = static_cast<uint32>(Asset->Vertices.size());
	}
}

bool FSkeletalMeshSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC) return false;

	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	const TArray<FVertexPNCTT>& SkinnedVertices = SMC->GetSkinnedVertices();
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.size());
	if (VertexCount == 0) return false;

	if (bDynamicBufferNeedsCreate || !DynamicVertexBuffer.GetBuffer())
	{
		DynamicVertexBuffer.Create(Device, CachedDynamicVertexCount ? CachedDynamicVertexCount : VertexCount, sizeof(FVertexPNCTT));
		bDynamicBufferNeedsCreate = false;
	}

	DynamicVertexBuffer.EnsureCapacity(Device, VertexCount);

	const uint64 CurrentRevision = SMC->GetSkinnedRevision();
	if (UploadedSkinnedRevision != CurrentRevision)
	{
		if (!DynamicVertexBuffer.Update(Context, SkinnedVertices.data(), VertexCount))
		{
			return false;
		}
		UploadedSkinnedRevision = CurrentRevision;
	}

	OutBuffer = {};
	OutBuffer.VB = DynamicVertexBuffer.GetBuffer();
	OutBuffer.VBStride = DynamicVertexBuffer.GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

bool FSkeletalMeshSceneProxy::PrepareGpuSkinningDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	if (!UpdateSkinMatrixBuffer(Device, Context)) return false;

	OutBuffer = {};
	OutBuffer.VB = Asset->RenderBuffer->GetVertexBuffer().GetBuffer();
	OutBuffer.VBStride = Asset->RenderBuffer->GetVertexBuffer().GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

bool FSkeletalMeshSceneProxy::PrepareCpuBoneHeatMapDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, int32 SelectedBoneIndex, FDrawCommandBuffer& OutBuffer) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC) return false;

	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	const TArray<FVertexPNCTT>& SkinnedVertices = SMC->GetSkinnedVertices();
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.size());
	if (VertexCount == 0 || Asset->Vertices.size() != SkinnedVertices.size()) return false;

	if (bBoneHeatMapBufferNeedsCreate || !BoneHeatMapVertexBuffer.GetBuffer())
	{
		BoneHeatMapVertexBuffer.Create(Device, CachedDynamicVertexCount ? CachedDynamicVertexCount : VertexCount, sizeof(FVertexPNCTT));
		bBoneHeatMapBufferNeedsCreate = false;
	}

	BoneHeatMapVertexBuffer.EnsureCapacity(Device, VertexCount);

	const uint64 CurrentRevision = SMC->GetSkinnedRevision();
	if (UploadedBoneHeatMapRevision != CurrentRevision || UploadedBoneHeatMapBoneIndex != SelectedBoneIndex)
	{
		BoneHeatMapVertices = SkinnedVertices;

		for (uint32 i = 0; i < VertexCount; ++i)
		{
			float SelectedWeight = 0.0f;
			const FVertexPNCTBW& SourceVertex = Asset->Vertices[i];
			for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
			{
				if (SourceVertex.BoneIndices[InfluenceIndex] == SelectedBoneIndex)
				{
					SelectedWeight += SourceVertex.BoneWeights[InfluenceIndex];
				}
			}
			BoneHeatMapVertices[i].Color.W = SelectedWeight;
		}

		if (!BoneHeatMapVertexBuffer.Update(Context, BoneHeatMapVertices.data(), VertexCount))
		{
			return false;
		}

		UploadedBoneHeatMapRevision = CurrentRevision;
		UploadedBoneHeatMapBoneIndex = SelectedBoneIndex;
	}

	OutBuffer = {};
	OutBuffer.VB = BoneHeatMapVertexBuffer.GetBuffer();
	OutBuffer.VBStride = BoneHeatMapVertexBuffer.GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

bool FSkeletalMeshSceneProxy::PrepareCpuClothMaxDistanceOverlayDrawBuffer(
	ID3D11Device* Device,
	ID3D11DeviceContext* Context,
	int32 LODIndex,
	int32 ClothIndex,
	FDrawCommandBuffer& OutBuffer,
	uint32& OutFirstIndex,
	uint32& OutIndexCount) const
{
	OutFirstIndex = 0;
	OutIndexCount = 0;

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC || LODIndex < 0 || ClothIndex < 0) return false;

	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	const FSkeletalClothLODData* LODData = Asset->FindClothLOD(static_cast<uint32>(LODIndex));
	if (!LODData || ClothIndex >= static_cast<int32>(LODData->Cloths.size())) return false;

	const FSkeletalClothData& Cloth = LODData->Cloths[ClothIndex];
	if (Cloth.Paint.MaxDistanceValues.empty() || Cloth.ParticleToRenderVertex.empty()) return false;

	const TArray<FVertexPNCTT>& SkinnedVertices = SMC->GetSkinnedVertices();
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.size());
	if (VertexCount == 0 || Asset->Vertices.size() != SkinnedVertices.size()) return false;

	if (bClothMaxDistanceBufferNeedsCreate || !ClothMaxDistanceVertexBuffer.GetBuffer())
	{
		ClothMaxDistanceVertexBuffer.Create(Device, CachedDynamicVertexCount ? CachedDynamicVertexCount : VertexCount, sizeof(FVertexPNCTT));
		bClothMaxDistanceBufferNeedsCreate = false;
	}

	ClothMaxDistanceVertexBuffer.EnsureCapacity(Device, VertexCount);
	ClothMaxDistanceVertices = SkinnedVertices;
	for (FVertexPNCTT& Vertex : ClothMaxDistanceVertices)
	{
		Vertex.Color.W = 0.0f;
	}

	const float Denom = std::max(1.0f, Cloth.Paint.ViewMax - Cloth.Paint.ViewMin);
	const uint32 PaintCount = static_cast<uint32>(std::min(Cloth.ParticleToRenderVertex.size(), Cloth.Paint.MaxDistanceValues.size()));
	for (uint32 ParticleIndex = 0; ParticleIndex < PaintCount; ++ParticleIndex)
	{
		const uint32 RenderVertexIndex = Cloth.ParticleToRenderVertex[ParticleIndex];
		if (RenderVertexIndex >= VertexCount)
		{
			continue;
		}

		const float T = std::clamp(
			(Cloth.Paint.MaxDistanceValues[ParticleIndex] - Cloth.Paint.ViewMin) / Denom,
			0.0f,
			1.0f);
		ClothMaxDistanceVertices[RenderVertexIndex].Color.W = T;
	}

	if (!ClothMaxDistanceVertexBuffer.Update(Context, ClothMaxDistanceVertices.data(), VertexCount))
	{
		return false;
	}

	OutFirstIndex = Cloth.Binding.FirstIndex;
	OutIndexCount = Cloth.Binding.IndexCount;
	OutBuffer = {};
	OutBuffer.VB = ClothMaxDistanceVertexBuffer.GetBuffer();
	OutBuffer.VBStride = ClothMaxDistanceVertexBuffer.GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr && OutIndexCount > 0;
}

ID3D11ShaderResourceView* FSkeletalMeshSceneProxy::GetSkinMatrixSRV(ID3D11Device* Device, ID3D11DeviceContext* Context) const
{
	UpdateSkinMatrixBuffer(Device, Context);
	return SkinMatrixSRV;
}

ESkinningMode FSkeletalMeshSceneProxy::GetEffectiveSkinningMode() const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	return SMC ? SMC->GetEffectiveSkinningMode() : SkinningModeRuntime::Get();
}

void FSkeletalMeshSceneProxy::ReleaseSkinMatrixBuffer() const
{
	if (SkinMatrixSRV)
	{
		SkinMatrixSRV->Release();
		SkinMatrixSRV = nullptr;
	}

	if (SkinMatrixBuffer)
	{
		SkinMatrixBuffer->Release();
		SkinMatrixBuffer = nullptr;
	}

	SkinMatrixCapacity = 0;
}

bool FSkeletalMeshSceneProxy::UpdateSkinMatrixBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Device || !Context || !SMC || !Asset || Asset->Bones.empty()) return false;

	const uint32 MatrixCount = static_cast<uint32>(Asset->Bones.size());
	const uint64 CurrentRevision = SMC->GetSkinnedRevision();

	if (!SkinMatrixBuffer || !SkinMatrixSRV || SkinMatrixCapacity < MatrixCount)
	{
		ReleaseSkinMatrixBuffer();

		D3D11_BUFFER_DESC BufferDesc = {};
		BufferDesc.ByteWidth = sizeof(FMatrix) * MatrixCount;
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufferDesc.StructureByteStride = sizeof(FMatrix);

		if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &SkinMatrixBuffer)))
		{
			ReleaseSkinMatrixBuffer();
			return false;
		}

		SkinMatrixBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen("SkinMatrixBuffer")), "SkinMatrixBuffer");

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = MatrixCount;

		if (FAILED(Device->CreateShaderResourceView(SkinMatrixBuffer, &SRVDesc, &SkinMatrixSRV)))
		{
			ReleaseSkinMatrixBuffer();
			return false;
		}

		SkinMatrixSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen("SkinMatrixSRV")), "SkinMatrixSRV");
		SkinMatrixCapacity = MatrixCount;
		UploadedSkinMatrixRevision = 0;
	}

	if (UploadedSkinMatrixRevision == CurrentRevision)
	{
		return true;
	}

	TArray<FMatrix> SkinMatrices;
	SMC->BuildSkinMatrices(SkinMatrices);
	if (SkinMatrices.size() != MatrixCount) return false;

	{
		SCOPE_STAT_CAT("GPUSkinning_MatrixUpload", "Skinning");

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (FAILED(Context->Map(SkinMatrixBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			return false;
		}

		std::memcpy(Mapped.pData, SkinMatrices.data(), sizeof(FMatrix) * MatrixCount);
		Context->Unmap(SkinMatrixBuffer, 0);
	}

	UploadedSkinMatrixRevision = CurrentRevision;
	return true;
}

void FSkeletalMeshSceneProxy::RebuildSectionDraws()
{
	SectionDraws.clear();

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		MeshBuffer = nullptr;
		SectionDraws.clear();

		return;
	}

	SectionDraws.clear();

	const auto& Slots = Mesh->GetSkeletalMaterials();
	const auto& Overrides = SMC->GetOverrideMaterials();

	for (const FSkeletalMeshSection& Section : Mesh->GetSkeletalMeshAsset()->Sections)
	{
		FMeshSectionDraw Draw;
		Draw.Material = nullptr;
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.IndexCount;


		int32 i = Section.MaterialIndex;
		if (i >= 0 && i < static_cast<int32>(Slots.size()))
		{
			if (i < static_cast<int32>(Overrides.size()) && Overrides[i])
				Draw.Material = Overrides[i];
			else if (Slots[i].MaterialInterface)
				Draw.Material = Slots[i].MaterialInterface;
		}

		if (!Draw.Material)
		{
			Draw.Material = FMaterialManager::Get().GetOrCreateMaterial("None");
		}

		SectionDraws.push_back(Draw);
	}
}
