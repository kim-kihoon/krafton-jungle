#include "SkeletalMeshSceneProxy.h"
#include "Component/SkeletalMeshComponent.h"
#include "Mesh/SkeletalMesh.h"
#include "Render/Command/DrawCommand.h"
#include "Runtime/Engine.h"
#include "Profiling/Timer.h"
#include "Profiling/Stats.h"
#include <Profiling/SkinningStats.h>

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::SkeletalMesh;
}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
	DynamicVertexBuffer.Release();
	SkinMatrixBuffer.Release();
	SkeletalRenderCB.Release();
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
	bDynamicBufferNeedsCreate = true;

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (Asset)
	{
		CachedDynamicVertexCount = static_cast<uint32>(Asset->Vertices.size());
	}
}

const char* FSkeletalMeshSceneProxy::GetVertexShaderEntryName() const
{
	return "VS_Skeletal";
}

bool FSkeletalMeshSceneProxy::WantsGpuSkinning(const FPrimitiveDrawOptions& Options) const
{
	return Options.SkinningMode == ESkinningMode::GPU;
}

bool FSkeletalMeshSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	SCOPE_STAT_CAT("Prepare Buffer(CPU)", "Skinning");

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC) return false;

	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	SMC->EnsureCPUSkinnedVertices();

	const TArray<FVertexPNCTBW>& SkinnedVertices = SMC->GetSkinnedVertices();
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.size());
	if (VertexCount == 0) return false;

	SkinningStats::RecordCPUSkinnedMeshVertices(Mesh, VertexCount, sizeof(FVertexPNCTBW));

	if (bDynamicBufferNeedsCreate || !DynamicVertexBuffer.GetBuffer())
	{
		DynamicVertexBuffer.Create(Device, CachedDynamicVertexCount ? CachedDynamicVertexCount : VertexCount, sizeof(FVertexPNCTBW));
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
	SCOPE_STAT_CAT("Prepare Buffer(GPU)", "Skinning");

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC) return false;

	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	OutBuffer = {};
	OutBuffer.VB = Asset->RenderBuffer->GetVertexBuffer().GetBuffer();
	OutBuffer.VBStride = Asset->RenderBuffer->GetVertexBuffer().GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

bool FSkeletalMeshSceneProxy::PrepareDrawCommandBindings(ID3D11Device* Device, ID3D11DeviceContext* Context,
	const FPrimitiveDrawOptions& Options, FDrawCommand& OutCommand, int32) const
{
	if (!Device || !Context)
	{
		return false;
	}

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC)
	{
		return false;
	}

	if (!SkeletalRenderCB.GetBuffer())
	{
		SkeletalRenderCB.Create(Device, sizeof(FSkeletalRenderConstants), "SkeletalRenderCB");
	}

	FSkeletalRenderConstants Constants = {};
	Constants.SkinningMode = static_cast<uint32>(Options.SkinningMode);
	Constants.HeatmapMode = Options.bBoneWeightHeatmap ? 1u : 0u;
	Constants.SelectedBoneIndex = Options.BoneWeightHeatmapBoneIndex;
	SkeletalRenderCB.Update(Context, &Constants, sizeof(Constants));

	OutCommand.Skinning.SkeletalRenderCB = &SkeletalRenderCB;
	OutCommand.Skinning.SkinMatrixSRV = nullptr;
	OutCommand.Skinning.bEnabled = true;

	if (Options.SkinningMode == ESkinningMode::GPU)
	{
		SCOPE_STAT_CAT("Prepare Buffer(GPU)", "Skinning");
		const TArray<FMatrix>& SkinMatrices = SMC->GetCurrentSkinMatrices();
		if (SkinMatrices.empty())
		{
			return false;
		}

		const uint32 MatrixCount = static_cast<uint32>(SkinMatrices.size());
		SkinMatrixBuffer.EnsureCapacity(Device, MatrixCount, sizeof(FMatrix));

		// Skinning Stat
		SkinningStats::RecordSkinMatrixStructuredBuffer(
			SMC,
			MatrixCount,
			SkinMatrixBuffer.GetStride()
		);

		const uint64 CurrentRevision = SMC->GetSkinMatrixRevision();
		if (UploadedSkinMatrixRevision != CurrentRevision)
		{
			if (!SkinMatrixBuffer.Update(Context, SkinMatrices.data(), MatrixCount))
			{
				return false;
			}

			UploadedSkinMatrixRevision = CurrentRevision;
		}

		OutCommand.Skinning.SkinMatrixSRV = SkinMatrixBuffer.GetSRV();
	}

	return true;
}

void FSkeletalMeshSceneProxy::RebuildSectionDraws()
{
	SectionDraws.clear();

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
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
