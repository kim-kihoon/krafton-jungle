#include "SkinningStats.h"

TArray<FSkinMatrixBufferRecord> SkinningStats::SkinMatrixRecords;
TArray<FCPUSkinnedMeshVertexRecord> SkinningStats::CPUSkinnedMeshVertexRecords;

uint32 SkinningStats::SkinMatrixUsedCount = 0;
uint32 SkinningStats::SkinMatrixStrideBytes = 0;
uint32 SkinningStats::CPUSkinnedMeshVertexCount = 0;
uint64 SkinningStats::CPUSkinnedMeshVertexBytes = 0;

void SkinningStats::Reset()
{
	SkinMatrixRecords.clear();
	CPUSkinnedMeshVertexRecords.clear();

	SkinMatrixUsedCount = 0;
	SkinMatrixStrideBytes = 0;

	CPUSkinnedMeshVertexCount = 0;
	CPUSkinnedMeshVertexBytes = 0;
}

void SkinningStats::RecordSkinMatrixStructuredBuffer(const void* OwnerKey, uint32 UsedMatrixCount, uint32 MatrixStrideBytes)
{
	if (!OwnerKey || UsedMatrixCount == 0 || MatrixStrideBytes == 0)
	{
		return;
	}

	FSkinMatrixBufferRecord* FoundRecord = nullptr;
	for (FSkinMatrixBufferRecord& Record : SkinMatrixRecords)
	{
		if (Record.OwnerKey == OwnerKey)
		{
			FoundRecord = &Record;
			break;
		}
	}

	if (FoundRecord)
	{
		FoundRecord->UsedMatrixCount = UsedMatrixCount;
		FoundRecord->MatrixStrideBytes = MatrixStrideBytes;
	}
	else
	{
		SkinMatrixRecords.push_back({ OwnerKey, UsedMatrixCount, MatrixStrideBytes });
	}

	SkinMatrixUsedCount = 0;
	SkinMatrixStrideBytes = 0;
	for (const FSkinMatrixBufferRecord& Record : SkinMatrixRecords)
	{
		SkinMatrixUsedCount += Record.UsedMatrixCount;
		SkinMatrixStrideBytes = Record.MatrixStrideBytes;
	}
}

void SkinningStats::RecordCPUSkinnedMeshVertices(const void* MeshKey, uint32 VertexCount, uint32 VertexStrideBytes)
{
	if (!MeshKey || VertexCount == 0 || VertexStrideBytes == 0)
	{
		return;
	}

	for (FCPUSkinnedMeshVertexRecord& Record : CPUSkinnedMeshVertexRecords)
	{
		if (Record.MeshKey == MeshKey)
		{
			Record.VertexCount = VertexCount;
			Record.VertexStrideBytes = VertexStrideBytes;
			RebuildCPUSkinnedMeshVertexTotals();
			return;
		}
	}

	CPUSkinnedMeshVertexRecords.push_back({ MeshKey, VertexCount, VertexStrideBytes });
	RebuildCPUSkinnedMeshVertexTotals();
}

void SkinningStats::RebuildCPUSkinnedMeshVertexTotals()
{
	CPUSkinnedMeshVertexCount = 0;
	CPUSkinnedMeshVertexBytes = 0;

	for (const FCPUSkinnedMeshVertexRecord& Record : CPUSkinnedMeshVertexRecords)
	{
		CPUSkinnedMeshVertexCount += Record.VertexCount;
		CPUSkinnedMeshVertexBytes += static_cast<uint64>(Record.VertexCount) * Record.VertexStrideBytes;
	}
}
