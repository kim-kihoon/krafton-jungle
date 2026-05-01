#pragma once

#include "Core/CoreTypes.h"

class FTexturePoolBase;

struct FTexturePoolHandle
{
	FTexturePoolHandle() = default;
	FTexturePoolHandle(uint32 InInternalIndex, uint32 InArrayIndex)
		: InternalIndex(InInternalIndex), ArrayIndex(InArrayIndex)
	{
	}

	uint32 InternalIndex = static_cast<uint32>(-1);
	uint32 ArrayIndex = static_cast<uint32>(-1);
};

struct FTexturePoolHandleSet
{
	FTexturePoolHandleSet(FTexturePoolBase* InPool, uint32 InInternalIndex)
		: Pool(InPool)
		, InternalIndex(InInternalIndex)
	{
	}

	void Release();
	FTexturePoolBase* GetPool() const { return Pool; }

	uint32 InternalIndex = static_cast<uint32>(-1);
	bool bIsValid = false;
	uint64 DebugVersion = 1;
	TArray<FTexturePoolHandle> Handles;
	TArray<uint32> AllocatedSizes;

private:
	FTexturePoolBase* Pool = nullptr;
};

struct FTexturePoolHandleRequest
{
	FTexturePoolHandleRequest() = default;

	FTexturePoolHandleRequest(std::initializer_list<uint32> InSizes)
		: Sizes(InSizes)
	{
	}

	template<typename... Args>
	FTexturePoolHandleRequest(Args... args)
	{
		(Sizes.push_back(static_cast<uint32>(args)), ...);
	}

	TArray<uint32> Sizes;
};
