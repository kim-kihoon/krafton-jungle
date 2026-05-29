#include "SkeletalMesh.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeleton.h"
#include "Mesh/SkeletonManager.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"


static const FString EmptyPath;

USkeletalMesh::~USkeletalMesh()
{
	if (SkeletalMeshAsset)
	{
		delete SkeletalMeshAsset;
		SkeletalMeshAsset = nullptr;
	}

	auto It = FMeshManager::SkeletalMeshCache.find(AssetPathFileName);
	if (It != FMeshManager::SkeletalMeshCache.end() && It->second == this)
	{
		FMeshManager::SkeletalMeshCache.erase(It);
	}
}

void USkeletalMesh::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() && !SkeletalMeshAsset)
	{
		SkeletalMeshAsset = new FSkeletalMesh();
	}

	Ar << SkeletalMeshAsset->PathFileName;
	Ar << SkeletalMeshAsset->SkeletonPath;
	SerializeSkeletonMatrix(Ar, SkeletalMeshAsset->MeshBindGlobal);
	Ar << SkeletalMeshAsset->Vertices;
	Ar << SkeletalMeshAsset->Indices;
	Ar << SkeletalMeshAsset->Sections;
	Ar << SkeletalMaterials;

	if (Ar.IsLoading())
	{
		ResolveSkeleton();
		CacheSectionMaterialIndices();
		SkeletalMeshAsset->bBoundsValid = false;
	}
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InMesh)
{
	SkeletalMeshAsset = InMesh;
	ResolveSkeleton();
	CacheSectionMaterialIndices();
}

FSkeletalMesh* USkeletalMesh::GetSkeletalMeshAsset() const
{
	return SkeletalMeshAsset;
}

void USkeletalMesh::SetSkeleton(USkeleton* InSkeleton)
{
	Skeleton = InSkeleton;
	if (SkeletalMeshAsset && Skeleton)
	{
		SkeletalMeshAsset->SkeletonPath = Skeleton->GetAssetPathFileName();
	}
}

USkeleton* USkeletalMesh::GetSkeleton() const
{
	return Skeleton;
}

FSkeletonAsset* USkeletalMesh::GetSkeletonAsset() const
{
	return Skeleton ? Skeleton->GetSkeletonAsset() : nullptr;
}

bool USkeletalMesh::ResolveSkeleton()
{
	if (!SkeletalMeshAsset)
	{
		Skeleton = nullptr;
		return false;
	}

	if (Skeleton && Skeleton->GetAssetPathFileName() == SkeletalMeshAsset->SkeletonPath)
	{
		return true;
	}

	Skeleton = nullptr;
	if (SkeletalMeshAsset->SkeletonPath.empty())
	{
		return false;
	}

	Skeleton = FSkeletonManager::Get().Load(SkeletalMeshAsset->SkeletonPath);
	return Skeleton != nullptr;
}

void USkeletalMesh::SetSkeletalMaterials(TArray<FSkeletalMaterial>&& InMaterials)
{
	SkeletalMaterials = InMaterials;
	CacheSectionMaterialIndices();
}

const TArray<FSkeletalMaterial>& USkeletalMesh::GetSkeletalMaterials() const
{
	return SkeletalMaterials;
}

void USkeletalMesh::InitResources(ID3D11Device* InDevice)
{
	if (!InDevice || !SkeletalMeshAsset) return;

	const uint32 CPUSize =
		static_cast<uint32>(SkeletalMeshAsset->Vertices.size() * sizeof(FVertexPNCTBW)) +
		static_cast<uint32>(SkeletalMeshAsset->Indices.size() * sizeof(uint32));
	MemoryStats::AddSkeletalMeshCPUMemory(CPUSize);

	TMeshData<FVertexPNCTBW> RenderMeshData;
	RenderMeshData.Vertices.reserve(SkeletalMeshAsset->Vertices.size());

	for (const FVertexPNCTBW& RawVert : SkeletalMeshAsset->Vertices)
	{
		FVertexPNCTBW RenderVert;
		RenderVert.Position = RawVert.Position;
		RenderVert.Normal = RawVert.Normal;
		RenderVert.Color = RawVert.Color;
		RenderVert.UV = RawVert.UV;
		RenderVert.Tangent = RawVert.Tangent;
		std::copy(std::begin(RawVert.BoneIndices), std::end(RawVert.BoneIndices), std::begin(RenderVert.BoneIndices));
		std::copy(std::begin(RawVert.BoneWeights), std::end(RawVert.BoneWeights), std::begin(RenderVert.BoneWeights));
		RenderMeshData.Vertices.push_back(RenderVert);
	}
	RenderMeshData.Indices = SkeletalMeshAsset->Indices;

	SkeletalMeshAsset->RenderBuffer = std::make_unique<FMeshBuffer>();
	SkeletalMeshAsset->RenderBuffer->Create(InDevice, RenderMeshData);
}

void USkeletalMesh::CacheSectionMaterialIndices()
{
	if (!SkeletalMeshAsset)
	{
		return;
	}

	for (FSkeletalMeshSection& Section : SkeletalMeshAsset->Sections)
	{
		Section.MaterialIndex = -1;
		for (int32 i = 0; i < static_cast<int32>(SkeletalMaterials.size()); ++i)
		{
			if (SkeletalMaterials[i].MaterialSlotName == Section.MaterialSlotName)
			{
				Section.MaterialIndex = i;
				break;
			}
		}
	}
}
