#pragma once

#include "Asset/SkeletalMesh.h"
#include "Core/CoreMinimal.h"
#include "Core/Logging/Log.h"
#include "Render/Resource/Buffer.h"
#include "Render/Resource/VertexTypes.h"
#include "Render/Scene/RenderCommand.h"

#include <algorithm>

struct FSkinCacheEntry
{
	uint32 ComponentUUID = 0;
	const USkeletalMesh* SkeletalMesh = nullptr;
	uint32 VertexCount = 0;
	uint32 OutputStride = sizeof(FDeformedVertex);
	uint64 CachedRevision = 0;
	uint64 RequestedRevision = 0;
	ID3D11ShaderResourceView* SourceVertexSRV = nullptr;
	// Future morph inputs. Null means the current skinning-only path.
	ID3D11ShaderResourceView* MorphDeltaSRV = nullptr;
	ID3D11ShaderResourceView* MorphWeightSRV = nullptr;
	uint32 MorphTargetCount = 0;
	FRWVertexBuffer OutputBuffer;
	FString FailureReason;

	bool IsCompatible(const USkeletalMesh* InSkeletalMesh, uint32 InVertexCount, uint32 InOutputStride) const
	{
		return SkeletalMesh == InSkeletalMesh &&
			VertexCount == InVertexCount &&
			OutputStride == InOutputStride &&
			OutputBuffer.IsValid();
	}

	FVertexStreamOverride MakeVertexStreamOverride() const
	{
		FVertexStreamOverride Override;
		if (OutputBuffer.IsValid())
		{
			Override.VertexBuffer = OutputBuffer.GetBuffer();
			Override.Stride = OutputBuffer.GetStride();
			Override.VertexCount = OutputBuffer.GetCount();
			Override.Offset = 0;
		}
		return Override;
	}
};

class FSkinCacheManager
{
public:
	FSkinCacheEntry* FindEntry(uint32 ComponentUUID)
	{
		auto It = Entries.find(ComponentUUID);
		return It != Entries.end() ? &It->second : nullptr;
	}

	const FSkinCacheEntry* FindEntry(uint32 ComponentUUID) const
	{
		auto It = Entries.find(ComponentUUID);
		return It != Entries.end() ? &It->second : nullptr;
	}

	FSkinCacheEntry* GetOrCreateEntry(
		ID3D11Device* Device,
		uint32 ComponentUUID,
		const USkeletalMesh* SkeletalMesh,
		const FMeshBuffer* SourceMeshBuffer,
		uint64 DeformationRevision)
	{
		if (!Device || ComponentUUID == 0 || !SkeletalMesh || !SourceMeshBuffer || !SourceMeshBuffer->HasSourceVertexBuffer())
		{
			++MissingInputCount;
			return nullptr;
		}

		const uint32 VertexCount = SourceMeshBuffer->GetDrawVertexCount();
		const uint32 OutputStride = sizeof(FDeformedVertex);
		if (VertexCount == 0)
		{
			++MissingInputCount;
			return nullptr;
		}

		FSkinCacheEntry& Entry = Entries[ComponentUUID];
		if (!Entry.IsCompatible(SkeletalMesh, VertexCount, OutputStride))
		{
			Entry.OutputBuffer.Release();
			Entry = {};
			Entry.ComponentUUID = ComponentUUID;
			Entry.SkeletalMesh = SkeletalMesh;
			Entry.VertexCount = VertexCount;
			Entry.OutputStride = OutputStride;
			Entry.SourceVertexSRV = SourceMeshBuffer->GetSourceVertexSRV();
			Entry.CachedRevision = 0;

			const bool bCreated = Entry.OutputBuffer.Create(
				Device,
				OutputStride,
				VertexCount,
				EGpuResourceCategory::SkinCache,
				"SkinCache.Output");
			if (!bCreated)
			{
				Entry.FailureReason = "Output allocation failed";
				++FailedAllocationCount;
				UE_LOG_WARNING(
					"[SkinCache] Output allocation failed. Component=%u Vertices=%u Stride=%u",
					ComponentUUID,
					VertexCount,
					OutputStride);
				return nullptr;
			}
		}

		Entry.SourceVertexSRV = SourceMeshBuffer->GetSourceVertexSRV();
		Entry.RequestedRevision = DeformationRevision;
		return &Entry;
	}

	void MarkEntryDispatched(uint32 ComponentUUID, uint64 DeformationRevision)
	{
		if (FSkinCacheEntry* Entry = FindEntry(ComponentUUID))
		{
			Entry->CachedRevision = DeformationRevision;
		}
	}

	void RemoveEntry(uint32 ComponentUUID)
	{
		auto It = Entries.find(ComponentUUID);
		if (It == Entries.end())
		{
			return;
		}

		It->second.OutputBuffer.Release();
		Entries.erase(It);
	}

	void RemoveEntriesNotIn(const TArray<uint32>& ActiveComponentUUIDs)
	{
		for (auto It = Entries.begin(); It != Entries.end();)
		{
			const bool bActive = std::find(
				ActiveComponentUUIDs.begin(),
				ActiveComponentUUIDs.end(),
				It->first) != ActiveComponentUUIDs.end();
			if (bActive)
			{
				++It;
				continue;
			}

			It->second.OutputBuffer.Release();
			It = Entries.erase(It);
		}
	}

	void Release()
	{
		for (auto& Pair : Entries)
		{
			Pair.second.OutputBuffer.Release();
		}
		Entries.clear();
		FailedAllocationCount = 0;
		MissingInputCount = 0;
	}

	void AppendGpuMemoryStats(FGpuResourceMemoryStats& OutStats) const
	{
		for (const auto& Pair : Entries)
		{
			Pair.second.OutputBuffer.AppendGpuMemoryStats(OutStats, "SkinCache.Output");
		}

		OutStats.AddRecord(
			EGpuResourceCategory::SkinCache,
			"SkinCache.EntryMetadata",
			Entries.size() * sizeof(FSkinCacheEntry),
			static_cast<uint32>(Entries.size()),
			sizeof(FSkinCacheEntry),
			"FSkinCacheManager");
	}

	uint32 GetEntryCount() const { return static_cast<uint32>(Entries.size()); }
	uint32 GetFailedAllocationCount() const { return FailedAllocationCount; }
	uint32 GetMissingInputCount() const { return MissingInputCount; }
	uint32 GetValidOutputCount() const
	{
		uint32 Count = 0;
		for (const auto& Pair : Entries)
		{
			if (Pair.second.OutputBuffer.IsValid())
			{
				++Count;
			}
		}
		return Count;
	}
	uint32 GetPendingDispatchCount() const
	{
		uint32 Count = 0;
		for (const auto& Pair : Entries)
		{
			if (Pair.second.RequestedRevision != Pair.second.CachedRevision)
			{
				++Count;
			}
		}
		return Count;
	}
	size_t GetOutputBytes() const
	{
		size_t Bytes = 0;
		for (const auto& Pair : Entries)
		{
			Bytes += Pair.second.OutputBuffer.GetByteSize();
		}
		return Bytes;
	}
	size_t GetMetadataBytes() const { return Entries.size() * sizeof(FSkinCacheEntry); }
	const TMap<uint32, FSkinCacheEntry>& GetEntries() const { return Entries; }

private:
	TMap<uint32, FSkinCacheEntry> Entries;
	uint32 FailedAllocationCount = 0;
	uint32 MissingInputCount = 0;
};
