#include "MeshBufferManager.h"

#include "Asset/SkeletalMesh.h"
#include "Asset/StaticMesh.h"
#include "Component/ProceduralMeshComponent.h"
#include "Core/Logging/Stats.h"
#include "Render/Skinning/SkinningRuntimeSettings.h"

#include <string>

namespace
{
	FMeshData CreateBillboardQuadMeshData()
	{
		FMeshData QuadMeshData;
		FColor DefaultColor(1.0f, 1.0f, 1.0f, 1.0f);

		QuadMeshData.Vertices.push_back({ FVector(0.0f, -0.5f,  0.5f), DefaultColor, 0 });
		QuadMeshData.Vertices.push_back({ FVector(0.0f,  0.5f,  0.5f), DefaultColor, 0 });
		QuadMeshData.Vertices.push_back({ FVector(0.0f,  0.5f, -0.5f), DefaultColor, 0 });
		QuadMeshData.Vertices.push_back({ FVector(0.0f, -0.5f, -0.5f), DefaultColor, 0 });

		QuadMeshData.Indices = { 0, 1, 2, 0, 2, 3 };
		return QuadMeshData;
	}

	bool UpdateSkeletalMeshBufferVertices(ID3D11Device* Device, FMeshBuffer& Buffer, const TArray<FSkeletalMeshVertex>& Vertices)
	{
		if (!Device)
		{
			return false;
		}

		ID3D11DeviceContext* DeviceContext = nullptr;
		Device->GetImmediateContext(&DeviceContext);
		if (!DeviceContext)
		{
			return false;
		}

		Buffer.UpdateDynamicVertices(DeviceContext, Vertices);
		DeviceContext->Release();

		return true;
	}
}

void FMeshBufferManager::Create(ID3D11Device* InDevice)
{
	Device = InDevice;
	const FMeshData QuadMeshData = CreateBillboardQuadMeshData();

	MeshBufferMap[EPrimitiveType::EPT_TransGizmo].Create(InDevice, FEditorMeshLibrary::GetTranslationGizmo());
	MeshBufferMap[EPrimitiveType::EPT_RotGizmo].Create(InDevice, FEditorMeshLibrary::GetRotationGizmo()); 
	MeshBufferMap[EPrimitiveType::EPT_ScaleGizmo].Create(InDevice, FEditorMeshLibrary::GetScaleGizmo());
	MeshBufferMap[EPrimitiveType::EPT_Billboard].Create(InDevice, QuadMeshData);
	MeshBufferMap[EPrimitiveType::EPT_SubUV].Create(InDevice, QuadMeshData);
	MeshBufferMap[EPrimitiveType::EPT_Text].Create(InDevice, QuadMeshData);
    MeshBufferMap[EPrimitiveType::EPT_ProceduralMesh].Create(InDevice, QuadMeshData);
}


void FMeshBufferManager::Release()
{
	for (auto& pair : MeshBufferMap)
    {
        pair.second.Release();
    }
    MeshBufferMap.clear();

    for (int32 i = 0; i < MAX_LOD; ++i) 
    {
        for (auto& pair : StaticMeshBufferMap[i])
        {
            pair.second.Release();
        }
        StaticMeshBufferMap[i].clear();
    }

	for (auto& pair : ProcMeshBufferMap)
	{
        pair.second.Release();
	}

	ProcMeshBufferMap.clear();

	for (auto& pair : SkeletalMeshBufferMap)
	{
		pair.second.Release();
	}

	SkeletalMeshBufferMap.clear();
	SkeletalMeshSourceMap.clear();

	for (auto& pair : SkeletalBindPoseBufferMap)
	{
		pair.second.Release();
	}
	SkeletalBindPoseBufferMap.clear();
	SkeletalBindPoseLastUsedFrameMap.clear();

	for (auto& pair : SkeletalBoneMatrixBufferMap)
	{
		pair.second.Release();
	}
	SkeletalBoneMatrixBufferMap.clear();
	SkeletalBoneMatrixCapacityMap.clear();
	SkeletalBoneMatrixLastUsedFrameMap.clear();
	ResourceTrackingFrame = 0;
	bResourceTrackingFrameActive = false;
    
    Device = nullptr;
}

void FMeshBufferManager::BeginFrameResourceTracking()
{
	++ResourceTrackingFrame;
	bResourceTrackingFrameActive = true;
}

void FMeshBufferManager::EndFrameResourceTracking()
{
	if (!bResourceTrackingFrameActive)
	{
		return;
	}

	for (auto It = SkeletalBindPoseBufferMap.begin(); It != SkeletalBindPoseBufferMap.end();)
	{
		const USkeletalMesh* Mesh = It->first;
		auto LastUsedIt = SkeletalBindPoseLastUsedFrameMap.find(Mesh);
		const bool bUsedThisFrame =
			LastUsedIt != SkeletalBindPoseLastUsedFrameMap.end() &&
			LastUsedIt->second == ResourceTrackingFrame;

		if (bUsedThisFrame)
		{
			++It;
			continue;
		}

		It->second.Release();
		SkeletalBindPoseLastUsedFrameMap.erase(Mesh);
		It = SkeletalBindPoseBufferMap.erase(It);
	}

	for (auto It = SkeletalBoneMatrixBufferMap.begin(); It != SkeletalBoneMatrixBufferMap.end();)
	{
		const uint32 ComponentUUID = It->first;
		auto LastUsedIt = SkeletalBoneMatrixLastUsedFrameMap.find(ComponentUUID);
		const bool bUsedThisFrame =
			LastUsedIt != SkeletalBoneMatrixLastUsedFrameMap.end() &&
			LastUsedIt->second == ResourceTrackingFrame;

		if (bUsedThisFrame)
		{
			++It;
			continue;
		}

		It->second.Release();
		SkeletalBoneMatrixCapacityMap.erase(ComponentUUID);
		SkeletalBoneMatrixLastUsedFrameMap.erase(ComponentUUID);
		It = SkeletalBoneMatrixBufferMap.erase(It);
	}

	bResourceTrackingFrameActive = false;
}

//	MeshBuffer는 VB, IB를 모두 포함하고 있습니다.
FMeshBuffer& FMeshBufferManager::GetMeshBuffer(EPrimitiveType InPrimitiveType)
{
	auto it = MeshBufferMap.find(InPrimitiveType);
	if (it != MeshBufferMap.end())
	{
		return it->second;
	}
	
	//	존재하지 않는 PrimitiveType이 요청된 경우, Billboard Quad를 기본 반환합니다.
	return MeshBufferMap.at(EPrimitiveType::EPT_Billboard);
}

FMeshBuffer* FMeshBufferManager::GetStaticMeshBuffer(const UStaticMesh* StaticMeshAsset, int32 LODLevel)
{
	if (!Device || !StaticMeshAsset || !StaticMeshAsset->HasValidMeshData())
    {
        return nullptr;
    }

    // 1. LOD 레벨 안전장치 (Crash 방지)
    // 요청한 LOD가 실제 가진 것보다 크면, 가장 최하위(마지막) LOD로 강제 조정합니다.
    int32 ValidLODCount = StaticMeshAsset->GetValidLODCount();
    if (LODLevel >= ValidLODCount)
    {
        LODLevel = ValidLODCount - 1;
    }
    if (LODLevel < 0) LODLevel = 0;

    auto& TargetMap = StaticMeshBufferMap[LODLevel];

    auto It = TargetMap.find(StaticMeshAsset);
    if (It != TargetMap.end())
    {
        return &It->second;
    }

    const FStaticMesh* LODData = StaticMeshAsset->GetMeshData(LODLevel);
    if (!LODData)
    {
        return nullptr;
    }

    const TArray<FNormalVertex>& Vertices = LODData->Vertices;
    const TArray<uint32>&        Indices  = LODData->Indices;

    if (Vertices.empty() || Indices.empty())
    {
        return nullptr;
    }

	FMeshBuffer& NewBuffer = TargetMap[StaticMeshAsset];
    NewBuffer.CreateImmutableVertices(Device, Vertices, Indices, EGpuResourceCategory::Mesh, "StaticMesh.LOD" + std::to_string(LODLevel));
    

    return &NewBuffer;
}

FMeshBuffer* FMeshBufferManager::GetProcMeshBuffer(uint32 ProcMeshCompUUID, const TArray<FNormalVertex>& Vertices, const TArray<uint32>& Indices)
{
    if (Vertices.empty() || Indices.empty())
    {
        return nullptr;
    }

    auto It = ProcMeshBufferMap.find(ProcMeshCompUUID);
    if (It != ProcMeshBufferMap.end())
    {
        FMeshBuffer& Existing = It->second;
        if (Existing.IsValid())
        {
            const uint32 ExistingVertexCount = Existing.GetVertexBuffer().GetVertexCount();
            const uint32 ExistingIndexCount = Existing.GetIndexBuffer().GetIndexCount();
			// Size 달라짐을 통해 유효 여부 확인
            if (ExistingVertexCount == static_cast<uint32>(Vertices.size()) && ExistingIndexCount == static_cast<uint32>(Indices.size()))
            {
                return &Existing;
            }
            // recreate if size differs
            Existing.Release();
        }
    }

    FMeshBuffer& NewBuffer = ProcMeshBufferMap[ProcMeshCompUUID];
    NewBuffer.CreateImmutableVertices(Device, Vertices, Indices, EGpuResourceCategory::Mesh, "ProceduralMesh");

    return &NewBuffer;
}

FMeshBuffer* FMeshBufferManager::GetSkeletalMeshBuffer(uint32 SkeletalMeshCompUUID, const USkeletalMesh* SkeletalMeshAsset, const TArray<FSkeletalMeshVertex>& Vertices, const TArray<uint32>& Indices, bool bNeedsUpload)
{
	if (!Device || !SkeletalMeshAsset || Vertices.empty() || Indices.empty())
	{
		return nullptr;
	}

	auto It = SkeletalMeshBufferMap.find(SkeletalMeshCompUUID);
	if (It != SkeletalMeshBufferMap.end())
	{
		FMeshBuffer& Existing = It->second;
		if (Existing.IsValid())
		{
			auto SourceIt = SkeletalMeshSourceMap.find(SkeletalMeshCompUUID);
			const bool bSameSourceMesh = SourceIt != SkeletalMeshSourceMap.end() && SourceIt->second == SkeletalMeshAsset;
			const uint32 ExistingVertexCount = Existing.GetVertexBuffer().GetVertexCount();
			const uint32 ExistingIndexCount = Existing.GetIndexBuffer().GetIndexCount();
			const bool bSameSize =
				ExistingVertexCount == static_cast<uint32>(Vertices.size()) &&
				ExistingIndexCount == static_cast<uint32>(Indices.size());

			// 개수만 확인하면 mesh 교체 시 vertex / index 개수가 우연히 같은 경우에 index buffer를 잘못 재사용하는 경우가 생길 수 있음~
			if (bSameSourceMesh && bSameSize)
			{
				if (bNeedsUpload)
				{
					SCOPE_STAT("CPU.Skinning.VertexUpload");
					UpdateSkeletalMeshBufferVertices(Device, Existing, Vertices);
				}

				return &Existing;
			}

			Existing.Release();
		}
	}

	FMeshBuffer& NewBuffer = SkeletalMeshBufferMap[SkeletalMeshCompUUID];
	NewBuffer.CreateDynamicVertices<FSkeletalMeshVertex>(Device, static_cast<uint32>(Vertices.size()), Indices, EGpuResourceCategory::Skeletal, "SkeletalMesh.Dynamic");
	{
		SCOPE_STAT("CPU.Skinning.VertexUpload");
		UpdateSkeletalMeshBufferVertices(Device, NewBuffer, Vertices);
	}
	SkeletalMeshSourceMap[SkeletalMeshCompUUID] = SkeletalMeshAsset;

	return NewBuffer.IsValid() ? &NewBuffer : nullptr;
}

FMeshBuffer* FMeshBufferManager::GetSkeletalBindPoseBuffer(const USkeletalMesh* SkeletalMeshAsset)
{
	if (!Device || !SkeletalMeshAsset || !SkeletalMeshAsset->HasValidMeshData())
	{
		return nullptr;
	}

	auto It = SkeletalBindPoseBufferMap.find(SkeletalMeshAsset);
	if (It != SkeletalBindPoseBufferMap.end() && It->second.IsValid())
	{
		if (bResourceTrackingFrameActive)
		{
			SkeletalBindPoseLastUsedFrameMap[SkeletalMeshAsset] = ResourceTrackingFrame;
		}
		return &It->second;
	}

	const TArray<FSkeletalMeshVertex>& Vertices = SkeletalMeshAsset->GetVertices();
	const TArray<uint32>& Indices = SkeletalMeshAsset->GetIndices();
	if (Vertices.empty() || Indices.empty())
	{
		return nullptr;
	}

	FMeshBuffer& NewBuffer = SkeletalBindPoseBufferMap[SkeletalMeshAsset];
	NewBuffer.CreateImmutableSourceVertices(Device, Vertices, Indices, EGpuResourceCategory::Skeletal, "SkeletalMesh.BindPose");
	if (NewBuffer.IsValid() && bResourceTrackingFrameActive)
	{
		SkeletalBindPoseLastUsedFrameMap[SkeletalMeshAsset] = ResourceTrackingFrame;
	}
	return NewBuffer.IsValid() ? &NewBuffer : nullptr;
}

FStructuredBuffer* FMeshBufferManager::GetSkeletalBoneMatrixBuffer(uint32 SkeletalMeshCompUUID, const TArray<FMatrix>& BoneMatrices, bool bNeedsUpload)
{
	if (!Device || BoneMatrices.empty())
	{
		return nullptr;
	}

	const uint32 BoneCount = static_cast<uint32>(BoneMatrices.size());
	auto CapacityIt = SkeletalBoneMatrixCapacityMap.find(SkeletalMeshCompUUID);
	auto BufferIt = SkeletalBoneMatrixBufferMap.find(SkeletalMeshCompUUID);
	const bool bHasValidBuffer =
		BufferIt != SkeletalBoneMatrixBufferMap.end() &&
		CapacityIt != SkeletalBoneMatrixCapacityMap.end() &&
		CapacityIt->second == BoneCount &&
		BufferIt->second.GetSRV() != nullptr;

	if (!bHasValidBuffer)
	{
		if (BufferIt != SkeletalBoneMatrixBufferMap.end())
		{
			BufferIt->second.Release();
		}

		FStructuredBuffer& NewBuffer = SkeletalBoneMatrixBufferMap[SkeletalMeshCompUUID];
		NewBuffer.Create(Device, sizeof(FMatrix), BoneCount, false, EGpuResourceCategory::Skeletal, "SkeletalMesh.BoneMatrices");
		SkeletalBoneMatrixCapacityMap[SkeletalMeshCompUUID] = BoneCount;
		BufferIt = SkeletalBoneMatrixBufferMap.find(SkeletalMeshCompUUID);
		bNeedsUpload = true;
	}

	FStructuredBuffer& Buffer = BufferIt->second;
	if (bResourceTrackingFrameActive)
	{
		SkeletalBoneMatrixLastUsedFrameMap[SkeletalMeshCompUUID] = ResourceTrackingFrame;
	}
	if (bNeedsUpload)
	{
		SCOPE_STAT("GPU.Skinning.BoneBufferUploadCPU");
		ID3D11DeviceContext* DeviceContext = nullptr;
		Device->GetImmediateContext(&DeviceContext);
		if (DeviceContext)
		{
			Buffer.Update(DeviceContext, BoneMatrices.data(), BoneCount);
			DeviceContext->Release();
		}
	}

	return Buffer.GetSRV() ? &Buffer : nullptr;
}

void FMeshBufferManager::AppendGpuMemoryStats(FGpuResourceMemoryStats& OutStats) const
{
	for (const auto& Pair : MeshBufferMap)
	{
		Pair.second.AppendGpuMemoryStats(OutStats, "PrimitiveMeshBuffer");
	}

	for (int32 LodIndex = 0; LodIndex < MAX_LOD; ++LodIndex)
	{
		for (const auto& Pair : StaticMeshBufferMap[LodIndex])
		{
			Pair.second.AppendGpuMemoryStats(OutStats, "StaticMeshBuffer");
		}
	}

	for (const auto& Pair : ProcMeshBufferMap)
	{
		Pair.second.AppendGpuMemoryStats(OutStats, "ProceduralMeshBuffer");
	}

	for (const auto& Pair : SkeletalMeshBufferMap)
	{
		Pair.second.AppendGpuMemoryStats(OutStats, "SkeletalDynamicMeshBuffer");
	}

	if (FSkinningRuntimeSettings::GetMode() == ESkinningMode::GPUVertexShader)
	{
		for (const auto& Pair : SkeletalBindPoseBufferMap)
		{
			Pair.second.AppendGpuMemoryStats(OutStats, "SkeletalBindPoseMeshBuffer");
		}

		for (const auto& Pair : SkeletalBoneMatrixBufferMap)
		{
			Pair.second.AppendGpuMemoryStats(OutStats, "SkeletalBoneMatrixBuffer");
		}
	}
}
