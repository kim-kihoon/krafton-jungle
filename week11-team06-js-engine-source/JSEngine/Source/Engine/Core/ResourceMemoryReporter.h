#pragma once

#include <cstddef>

#include "Core/CoreMinimal.h"

class FMaterialResourceCache;

enum class EGpuResourceCategory : uint8
{
	Mesh,
	Skeletal,
	SkinCache,
	Particles,  // 추후 구현을 위해 예약됨
	Shadow,
	Lighting,
	Editor,
	Other
};

struct FGpuResourceMemoryRecord
{
	EGpuResourceCategory Category = EGpuResourceCategory::Other;
	FString Name;
	size_t Bytes = 0;
	uint32 Count = 0;
	uint32 Stride = 0;
	FString Owner;
};

struct FGpuResourceMemoryStats
{
	TArray<FGpuResourceMemoryRecord> Records;
	TMap<EGpuResourceCategory, size_t> CategoryBytes;
	size_t TotalBytes = 0;

	void Reset();
	void AddRecord(EGpuResourceCategory Category, const FString& Name, size_t Bytes, uint32 Count, uint32 Stride, const FString& Owner);
	size_t GetCategoryBytes(EGpuResourceCategory Category) const;
	uint32 GetCategoryRecordCount(EGpuResourceCategory Category) const;
};

class FResourceMemoryReporter
{
public:
	static size_t GetMaterialMemorySize(const FMaterialResourceCache& MaterialCache);
	static const char* GetGpuResourceCategoryName(EGpuResourceCategory Category);
	static void AppendKnownGlobalGpuMemoryStats(FGpuResourceMemoryStats& OutStats);
};
