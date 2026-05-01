#pragma once

#include "Render/Resource/TexturePool/UVManager/Allocator/TexturePoolAllocatorBase.h"

class FTextureHandleAllocatorReal : public FTexturePoolAllocatorBase
{
public:
	virtual bool AllocateHandle(float TextureSize, FTexturePoolHandle& OutHandle) override;
	virtual bool CanAllocateHandle(float TextureSize) const override;
	virtual bool CanAllocateHandleSet(const FTexturePoolHandleRequest& Request) const override;
	virtual FAtlasUV GetAtlasUV(const FTexturePoolHandle& InHandle) override;
	virtual void ReleaseHandle(const FTexturePoolHandle& InHandle) override;
	virtual void BroadcastEntries() override;
};
