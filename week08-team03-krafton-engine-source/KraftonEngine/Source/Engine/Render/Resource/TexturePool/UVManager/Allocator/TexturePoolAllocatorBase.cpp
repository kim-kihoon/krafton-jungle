#include "Render/Resource/TexturePool/UVManager/Allocator/TexturePoolAllocatorBase.h"

void FTexturePoolAllocatorBase::Initialize(uint32 InSize, uint32 InLayerCount, uint32 InMinBlockSize)
{
	AtlasSize = InSize;
	LayerCount = InLayerCount;
	MinBlockSize = InMinBlockSize;
}

void FTexturePoolAllocatorBase::SetSize(uint32 InNewTextureSize)
{
	AtlasSize = InNewTextureSize;
	BroadcastEntries();
}

void FTexturePoolAllocatorBase::SetLayerCount(uint32 InNewLayerCount)
{
	LayerCount = InNewLayerCount;
	BroadcastEntries();
}

float FTexturePoolAllocatorBase::EstimateAllocationCost(const FTexturePoolHandleRequest& Request) const
{
	if (MinBlockSize == 0)
	{
		return 0.0f;
	}

	float TotalCost = 0.0f;
	for (uint32 Size : Request.Sizes)
	{
		const uint32 BlockCount = (Size + MinBlockSize - 1) / MinBlockSize;
		TotalCost += static_cast<float>(BlockCount * BlockCount);
	}
	return TotalCost;
}

uint32 FTexturePoolAllocatorBase::ReserveHandleSetId()
{
	return NextHandleSetId++;
}

FTexturePoolHandleSet* FTexturePoolAllocatorBase::RegisterHandleSet(std::unique_ptr<FTexturePoolHandleSet> InHandleSet)
{
	if (!InHandleSet)
	{
		return nullptr;
	}

	FTexturePoolHandleSet* HandleSet = InHandleSet.get();
	RegisteredHandleSets[HandleSet->InternalIndex] = std::move(InHandleSet);
	return HandleSet;
}

void FTexturePoolAllocatorBase::UnregisterHandleSet(uint32 InHandleSetId)
{
	RegisteredHandleSets.erase(InHandleSetId);
}

void FTexturePoolAllocatorBase::InvalidateAllHandleSets()
{
	for (auto& Pair : RegisteredHandleSets)
	{
		if (FTexturePoolHandleSet* HandleSet = Pair.second.get())
		{
			HandleSet->bIsValid = false;
			++HandleSet->DebugVersion;
		}
	}
}
