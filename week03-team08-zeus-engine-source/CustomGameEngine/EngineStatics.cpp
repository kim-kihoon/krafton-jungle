#include "EngineStatics.h"

uint32 UEngineStatics::NextUUID = 0;

uint32 UEngineStatics::TotalAllocatedBytes;
uint32 UEngineStatics::TotalAllocationCount;
uint32 UEngineStatics::TotalDrawCalls;

void* operator new(size_t size)
{
	if (void* p = std::malloc(size))
	{
		UEngineStatics::TotalAllocatedBytes += static_cast<uint32>(size);
		UEngineStatics::TotalAllocationCount++;
		return p;
	}
	throw std::bad_alloc();
}

void* operator new[](size_t size)
{
	if (void* p = std::malloc(size))
	{
		UEngineStatics::TotalAllocatedBytes += static_cast<uint32>(size);
		UEngineStatics::TotalAllocationCount++;
		return p;
	}
	throw std::bad_alloc();
}

void operator delete(void* p, size_t size) noexcept
{
	if (p == nullptr) return;

	UEngineStatics::TotalAllocatedBytes -= static_cast<uint32>(size);
	UEngineStatics::TotalAllocationCount--;
	std::free(p);
}

void operator delete[](void* p, size_t size) noexcept
{
	if (p == nullptr) return;
	UEngineStatics::TotalAllocatedBytes -= static_cast<uint32>(size);
	UEngineStatics::TotalAllocationCount--;
	std::free(p);
}
