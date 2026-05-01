#include "Render/Resource/TexturePool/UVManager/Allocator/TextureHandleAllocatorReal.h"

bool FTextureHandleAllocatorReal::AllocateHandle(float TextureSize, FTexturePoolHandle& OutHandle)
{
	(void)TextureSize;
	OutHandle = {};
	return false;
}

bool FTextureHandleAllocatorReal::CanAllocateHandle(float TextureSize) const
{
	(void)TextureSize;
	return false;
}

bool FTextureHandleAllocatorReal::CanAllocateHandleSet(const FTexturePoolHandleRequest& Request) const
{
	(void)Request;
	return false;
}

FAtlasUV FTextureHandleAllocatorReal::GetAtlasUV(const FTexturePoolHandle& InHandle)
{
	(void)InHandle;
	return {};
}

void FTextureHandleAllocatorReal::ReleaseHandle(const FTexturePoolHandle& InHandle)
{
	(void)InHandle;
}

void FTextureHandleAllocatorReal::BroadcastEntries()
{
}
