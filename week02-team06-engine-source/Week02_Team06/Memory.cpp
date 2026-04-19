#include "pch.h"
#include "Memory.h"

uint64 FMemory::TotalAllocationBytes = 0;
uint64 FMemory::TotalAllocationCount = 0;


void* operator new(size_t size)
{
    FMemory::TotalAllocationBytes += size;
    FMemory::TotalAllocationCount++;
    return malloc(size);
}

void operator delete(void* ptr, size_t size)
{
    FMemory::TotalAllocationBytes -= size;
    FMemory::TotalAllocationCount--;
    free(ptr);
}