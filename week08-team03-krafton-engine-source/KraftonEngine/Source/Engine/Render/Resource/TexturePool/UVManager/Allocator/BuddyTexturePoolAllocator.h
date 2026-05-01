#pragma once

#include "Render/Resource/TexturePool/UVManager/Allocator/TexturePoolAllocatorBase.h"
#include <unordered_map>

class FBuddyTexturePoolAllocator : public FTexturePoolAllocatorBase
{
public:
	virtual void Initialize(uint32 InSize, uint32 InLayerCount, uint32 InMinBlockSize = 256) override;
	virtual bool AllocateHandle(float TextureSize, FTexturePoolHandle& OutHandle) override;
	virtual bool CanAllocateHandle(float TextureSize) const override;
	virtual bool CanAllocateHandleSet(const FTexturePoolHandleRequest& Request) const override;
	virtual float EstimateAllocationCost(const FTexturePoolHandleRequest& Request) const override;
	virtual FAtlasUV GetAtlasUV(const FTexturePoolHandle& InHandle) override;
	virtual void ReleaseHandle(const FTexturePoolHandle& InHandle) override;
	virtual void BroadcastEntries() override;
	virtual uint32 GetFreeRectCount() const override;
	virtual uint64 GetTotalFreeArea() const override;
	virtual uint64 GetLargestFreeRectArea() const override;
	virtual float GetFragmentationRatio() const override;
	virtual void GetFreeRects(TArray<FAtlasDebugRect>& OutRects) const override;
	virtual void GetAllocatedRects(TArray<FAtlasDebugRect>& OutRects) const override;
	virtual void SetSize(uint32 InNewTextureSize) override;
	virtual void SetLayerCount(uint32 InNewLayerCount) override;

private:
	struct FBuddyBlock
	{
		uint32 X = 0;
		uint32 Y = 0;
		uint32 Size = 0;
		uint32 ArrayIndex = 0;
	};

private:
	static bool IsPowerOfTwo(uint32 Value);
	static uint32 FloorPowerOfTwo(uint32 Value);
	static uint32 CeilPowerOfTwo(uint32 Value);

	void ResetAllocationState(uint32 InLayerCount);
	uint32 QuantizeBlockSize(uint32 RequestSize) const;
	bool TryAllocateBlock(uint32 RequestSize, TMap<uint32, TArray<FBuddyBlock>>& InOutFreeLists, FBuddyBlock& OutBlock) const;
	bool ContainsFreeBlock(const FBuddyBlock& Block) const;
	bool RemoveFreeBlock(const FBuddyBlock& Block);
	bool AreBuddiesFreeForMerge(const FBuddyBlock& Block) const;
	void RemoveBuddiesForMerge(const FBuddyBlock& Block);
	bool AreSameBlock(const FBuddyBlock& A, const FBuddyBlock& B) const;
	TArray<FBuddyBlock> MakeSiblingBlocks(const FBuddyBlock& Block) const;
	FAtlasDebugRect MakeDebugRect(const FBuddyBlock& Block, bool bAllocated, uint32 HandleIndex = static_cast<uint32>(-1)) const;

private:
	uint32 Size = 4096;
	uint32 NextHandle = 1;

	TMap<uint32, TArray<FBuddyBlock>> FreeLists;
	std::unordered_map<uint32, FBuddyBlock> AllocatedBlocks;
};
