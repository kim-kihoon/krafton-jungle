#pragma once

#include "Core/CoreTypes.h"

struct FSkinMatrixBufferRecord
{
	const void* OwnerKey = nullptr;
	uint32 UsedMatrixCount = 0;
	uint32 MatrixStrideBytes = 0;
};

struct FCPUSkinnedMeshVertexRecord
{
	const void* MeshKey = nullptr;
	uint32 VertexCount = 0;
	uint32 VertexStrideBytes = 0;
};

class SkinningStats
{
public:
	static void Reset();
	static void RecordSkinMatrixStructuredBuffer(const void* OwnerKey, uint32 UsedMatrixCount, uint32 MatrixStrideBytes);
	static void RecordCPUSkinnedMeshVertices(const void* MeshKey, uint32 VertexCount, uint32 VertexStrideBytes);
	static void RebuildCPUSkinnedMeshVertexTotals();

	static uint32 GetSkinMatrixUsedCount() { return SkinMatrixUsedCount; }
	static uint64 GetSkinMatrixUsedBytes()
	{
		return static_cast<uint64>(SkinMatrixUsedCount) * SkinMatrixStrideBytes;
	}

	static uint32 GetCPUSkinnedMeshVertexCount() { return CPUSkinnedMeshVertexCount; }

	static uint64 GetCPUSkinnedMeshVertexBytes()
	{
		return CPUSkinnedMeshVertexBytes;
	}

private:
	static TArray<FSkinMatrixBufferRecord> SkinMatrixRecords;
	static TArray<FCPUSkinnedMeshVertexRecord> CPUSkinnedMeshVertexRecords;

	static uint32 SkinMatrixUsedCount;
	static uint32 SkinMatrixStrideBytes;

	static uint32 CPUSkinnedMeshVertexCount;
	static uint64 CPUSkinnedMeshVertexBytes;
};
