#include "Mesh/Static/StaticMesh.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Mesh/MeshManager.h"
#include "Serialization/WindowsArchive.h"
#include "Mesh/Importer/ObjImporter.h"
#include "Texture/Texture2D.h"
#include "Engine/Profiling/Stats/MemoryStats.h"
#include "Mesh/MeshSimplifier.h"

UStaticMesh::~UStaticMesh()
{
    ReleaseTrackedMemory();
}

uint32 UStaticMesh::CalculateTrackedMemorySize() const
{
    if (!StaticMeshAsset)
    {
        return 0;
    }

    return
            static_cast<uint32>(StaticMeshAsset->Vertices.size() * sizeof(FNormalVertex)) +
            static_cast<uint32>(StaticMeshAsset->Indices.size() * sizeof(uint32));
}

void UStaticMesh::ReleaseTrackedMemory()
{
    if (!bMemoryTracked)
    {
        return;
    }

    MemoryStats::SubStaticMeshCPUMemory(TrackedMemorySize);
    bMemoryTracked    = false;
    TrackedMemorySize = 0;
}

void UStaticMesh::CacheSectionMaterialIndices()
{
    if (!StaticMeshAsset)
    {
        return;
    }

    for (FStaticMeshSection& Section : StaticMeshAsset->Sections)
    {
        Section.MaterialIndex = -1;
        for (int32 i = 0; i < static_cast<int32>(StaticMaterials.size()); ++i)
        {
            if (StaticMaterials[i].MaterialSlotName == Section.MaterialSlotName)
            {
                Section.MaterialIndex = i;
                break;
            }
        }

        if (Section.MaterialIndex < 0 && !Section.MaterialSlotName.empty())
        {
            const FString& SectionSlot = Section.MaterialSlotName;
            for (int32 i = 0; i < static_cast<int32>(StaticMaterials.size()); ++i)
            {
                const FString& MaterialSlot = StaticMaterials[i].MaterialSlotName;
                if (MaterialSlot.size() > 0 && MaterialSlot.back() == '$'
                    && MaterialSlot.substr(0, MaterialSlot.size() - 1) == SectionSlot)
                {
                    Section.MaterialIndex = i;
                    break;
                }
            }
        }
	}
}

void UStaticMesh::Serialize(FArchive& Ar)
{
    if (Ar.IsLoading())
    {
        ReleaseTrackedMemory();
        if (!StaticMeshAsset)
        {
            StaticMeshAsset = std::make_unique<FStaticMesh>();
        }
    }
    else if (!StaticMeshAsset)
    {
        StaticMeshAsset = std::make_unique<FStaticMesh>();
	}

	// 1. 지오메트리 데이터 직렬화
	StaticMeshAsset->Serialize(Ar);

	// 2. 머티리얼 데이터 직렬화 (필수!)
	Ar << StaticMaterials;

	// 3. 로딩 시 Section → MaterialIndex 매핑 캐싱 (매 프레임 문자열 비교 방지)
	if (Ar.IsLoading())
	{
        CacheSectionMaterialIndices();
        StaticMeshAsset->bBoundsValid = false;
        MeshTrianglePickingBVH        = FMeshTriangleBVH();
	}
}

void UStaticMesh::InitResources(ID3D11Device* InDevice)
{
	if (!InDevice || !StaticMeshAsset) return;

    // CPU 메모리 추적은 실제 리소스 초기화가 발생한 메시만 1회 등록합니다.
    if (!bMemoryTracked)
    {
        TrackedMemorySize = CalculateTrackedMemorySize();
        MemoryStats::AddStaticMeshCPUMemory(TrackedMemorySize);
        bMemoryTracked = true;
    }

	// CPU → GPU 정점 버퍼 변환
	TMeshData<FVertexPNCTT> RenderMeshData;
	RenderMeshData.Vertices.reserve(StaticMeshAsset->Vertices.size());

	for (const FNormalVertex& RawVert : StaticMeshAsset->Vertices)
	{
		FVertexPNCTT RenderVert;
		RenderVert.Position = RawVert.pos;
		RenderVert.Normal = RawVert.normal;
		RenderVert.Color = RawVert.color;
		RenderVert.UV = RawVert.tex;
		RenderVert.Tangent = RawVert.tangent;
		RenderMeshData.Vertices.push_back(RenderVert);
	}
	RenderMeshData.Indices = StaticMeshAsset->Indices;

	StaticMeshAsset->RenderBuffer = std::make_unique<FMeshBuffer>();
	StaticMeshAsset->RenderBuffer->Create(InDevice, RenderMeshData);

	// ── LOD 생성 (LOD1: 90%, LOD2: 55%, LOD3: 15%) ──
	/*if (StaticMeshAsset->Vertices.size() >= 100)
	{
		static const float LODRatios[] = { 0.9f, 0.55f, 0.15f };
		for (int lod = 0; lod < 3; ++lod)
		{
			FSimplifiedMesh Simplified = FMeshSimplifier::Simplify(
				StaticMeshAsset->Vertices, StaticMeshAsset->Indices,
				StaticMeshAsset->Sections, LODRatios[lod]);

			AdditionalLODs[lod].Sections = std::move(Simplified.Sections);

			TMeshData<FVertexPNCT> LODRenderData;
			LODRenderData.Vertices.reserve(Simplified.Vertices.size());
			for (const FNormalVertex& RawVert : Simplified.Vertices)
			{
				FVertexPNCT RenderVert;
				RenderVert.Position = RawVert.pos;
				RenderVert.Normal = RawVert.normal;
				RenderVert.Color = RawVert.color;
				RenderVert.UV = RawVert.tex;
				LODRenderData.Vertices.push_back(RenderVert);
			}
			LODRenderData.Indices = std::move(Simplified.Indices);

			AdditionalLODs[lod].RenderBuffer = std::make_unique<FMeshBuffer>();
			AdditionalLODs[lod].RenderBuffer->Create(InDevice, LODRenderData);
		}
		bHasLOD = true;
	}*/
}

void UStaticMesh::SetStaticMeshAsset(FStaticMesh* InMesh)
{
    if (StaticMeshAsset.get() == InMesh)
    {
        CacheSectionMaterialIndices();
        return;
    }

    SetStaticMeshAsset(std::unique_ptr<FStaticMesh>(InMesh));
}

void UStaticMesh::SetStaticMeshAsset(std::unique_ptr<FStaticMesh> InMesh)
{
    if (StaticMeshAsset.get() == InMesh.get())
    {
        InMesh.release();
        CacheSectionMaterialIndices();
        return;
    }

    ReleaseTrackedMemory();
    StaticMeshAsset = std::move(InMesh);

    for (FLODMeshData& LOD : AdditionalLODs)
    {
        LOD.Sections.clear();
        LOD.RenderBuffer.reset();
    }
    bHasLOD                = false;
    MeshTrianglePickingBVH = FMeshTriangleBVH();

    CacheSectionMaterialIndices();
	if (StaticMeshAsset)
	{
		EnsureMeshTrianglePickingBVHBuilt();
	}
}

FStaticMesh* UStaticMesh::GetStaticMeshAsset() const
{
    return StaticMeshAsset.get();
}

void UStaticMesh::SetStaticMaterials(TArray<FStaticMaterial>&& InMaterials)
{
    StaticMaterials = std::move(InMaterials);
    CacheSectionMaterialIndices();
}

const TArray<FStaticMaterial>& UStaticMesh::GetStaticMaterials() const
{
	return StaticMaterials;
}

void UStaticMesh::EnsureMeshTrianglePickingBVHBuilt() const
{
	if (!StaticMeshAsset)
	{
		return;
	}

	MeshTrianglePickingBVH.EnsureBuilt(*StaticMeshAsset);
}

bool UStaticMesh::RaycastMeshTrianglesWithBVHLocal(const FVector& LocalOrigin, const FVector& LocalDirection, FHitResult& OutHitResult) const
{
	if (!StaticMeshAsset)
	{
		return false;
	}

	EnsureMeshTrianglePickingBVHBuilt();
	return MeshTrianglePickingBVH.RaycastLocal(LocalOrigin, LocalDirection, *StaticMeshAsset, OutHitResult);
}

FMeshBuffer* UStaticMesh::GetLODMeshBuffer(uint32 LODLevel) const
{
	if (LODLevel == 0 && StaticMeshAsset)
		return StaticMeshAsset->RenderBuffer.get();
	if (LODLevel >= 1 && LODLevel <= 3 && bHasLOD)
		return AdditionalLODs[LODLevel - 1].RenderBuffer.get();
	return StaticMeshAsset ? StaticMeshAsset->RenderBuffer.get() : nullptr;
}

static const TArray<FStaticMeshSection> EmptySections;

const TArray<FStaticMeshSection>& UStaticMesh::GetLODSections(uint32 LODLevel) const
{
	if (LODLevel == 0 && StaticMeshAsset)
		return StaticMeshAsset->Sections;
	if (LODLevel >= 1 && LODLevel <= 3 && bHasLOD)
		return AdditionalLODs[LODLevel - 1].Sections;
	return StaticMeshAsset ? StaticMeshAsset->Sections : EmptySections;
}
