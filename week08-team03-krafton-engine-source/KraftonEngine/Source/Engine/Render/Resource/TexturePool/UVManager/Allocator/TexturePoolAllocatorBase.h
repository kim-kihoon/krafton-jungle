#pragma once

#include "Core/CoreTypes.h"
#include "Render/Resource/TexturePool/TexturePoolTypes.h"
#include <memory>

struct FAtlasUV
{
	uint32 ArrayIndex = 0;

	float u1 = 0.0f;
	float v1 = 0.0f;
	float u2 = 0.0f;
	float v2 = 0.0f;
};

struct FAtlasDebugRect
{
	uint32 X = 0;
	uint32 Y = 0;
	uint32 W = 0;
	uint32 H = 0;
	uint32 ArrayIndex = 0;
	uint32 HandleIndex = static_cast<uint32>(-1);
	bool bAllocated = false;
};

class FTexturePoolAllocatorBase
{
public:
	virtual ~FTexturePoolAllocatorBase() = default;

	virtual void Initialize(uint32 InSize, uint32 InLayerCount, uint32 InMinBlockSize = 32);

	virtual bool AllocateHandle(float TextureSize, FTexturePoolHandle& OutHandle) = 0;
	virtual bool CanAllocateHandle(float TextureSize) const = 0;
	virtual bool CanAllocateHandleSet(const FTexturePoolHandleRequest& Request) const = 0;
	virtual float EstimateAllocationCost(const FTexturePoolHandleRequest& Request) const;
	virtual FAtlasUV GetAtlasUV(const FTexturePoolHandle& InHandle) = 0;
	virtual void ReleaseHandle(const FTexturePoolHandle& InHandle) = 0;
	virtual void BroadcastEntries() = 0;
	virtual uint32 GetFreeRectCount() const { return 0; }
	virtual uint64 GetTotalFreeArea() const { return 0; }
	virtual uint64 GetLargestFreeRectArea() const { return 0; }
	virtual float GetFragmentationRatio() const { return 1.0f; }
	virtual void GetFreeRects(TArray<FAtlasDebugRect>& OutRects) const {}
	virtual void GetAllocatedRects(TArray<FAtlasDebugRect>& OutRects) const {}

	virtual void SetSize(uint32 InNewTextureSize);
	virtual void SetLayerCount(uint32 InNewLayerCount);

	uint32 ReserveHandleSetId();
	FTexturePoolHandleSet* RegisterHandleSet(std::unique_ptr<FTexturePoolHandleSet> InHandleSet);
	void UnregisterHandleSet(uint32 InHandleSetId);
	void InvalidateAllHandleSets();
	uint32 GetMinBlockSize() const { return MinBlockSize; }

protected:
	uint32 GetLayerCount() const { return LayerCount; }

protected:
	uint32 AtlasSize = 4096;
	uint32 MinBlockSize = 1024;

private:
	uint32 LayerCount = 0;
	uint32 NextHandleSetId = 0;
	TMap<uint32, std::unique_ptr<FTexturePoolHandleSet>> RegisteredHandleSets;
};
