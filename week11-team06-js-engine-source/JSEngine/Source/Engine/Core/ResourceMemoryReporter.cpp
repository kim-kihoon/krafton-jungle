#include "Core/ResourceMemoryReporter.h"

#include "Core/MaterialResourceCache.h"
#include "Render/Resource/ShadowAtlasManager.h"

#include <utility>

void FGpuResourceMemoryStats::Reset()
{
	Records.clear();
	CategoryBytes.clear();
	TotalBytes = 0;
}

void FGpuResourceMemoryStats::AddRecord(EGpuResourceCategory Category, const FString& Name, size_t Bytes, uint32 Count, uint32 Stride, const FString& Owner)
{
	if (Bytes == 0)
	{
		return;
	}

	FGpuResourceMemoryRecord Record;
	Record.Category = Category;
	Record.Name = Name;
	Record.Bytes = Bytes;
	Record.Count = Count;
	Record.Stride = Stride;
	Record.Owner = Owner;
	Records.push_back(std::move(Record));

	CategoryBytes[Category] += Bytes;
	TotalBytes += Bytes;
}

size_t FGpuResourceMemoryStats::GetCategoryBytes(EGpuResourceCategory Category) const
{
	auto It = CategoryBytes.find(Category);
	return It != CategoryBytes.end() ? It->second : 0;
}

uint32 FGpuResourceMemoryStats::GetCategoryRecordCount(EGpuResourceCategory Category) const
{
	uint32 Count = 0;
	for (const FGpuResourceMemoryRecord& Record : Records)
	{
		if (Record.Category == Category)
		{
			++Count;
		}
	}
	return Count;
}

size_t FResourceMemoryReporter::GetMaterialMemorySize(const FMaterialResourceCache& MaterialCache)
{
	return MaterialCache.GetMaterialMemorySize();
}

const char* FResourceMemoryReporter::GetGpuResourceCategoryName(EGpuResourceCategory Category)
{
	switch (Category)
	{
	case EGpuResourceCategory::Mesh: return "GPU.Mesh";
	case EGpuResourceCategory::Skeletal: return "GPU.Skeletal";
	case EGpuResourceCategory::SkinCache: return "GPU.SkinCache";
	case EGpuResourceCategory::Particles: return "GPU.Particles";
	case EGpuResourceCategory::Shadow: return "GPU.Shadow";
	case EGpuResourceCategory::Lighting: return "GPU.Lighting";
	case EGpuResourceCategory::Editor: return "GPU.Editor";
	case EGpuResourceCategory::Other:
	default:
		return "GPU.Other";
	}
}

void FResourceMemoryReporter::AppendKnownGlobalGpuMemoryStats(FGpuResourceMemoryStats& OutStats)
{
	FShadowAtlasManager& ShadowAtlasManager = FShadowAtlasManager::Get();

	if (ShadowAtlasManager.GetSRV() != nullptr)
	{
		OutStats.AddRecord(
			EGpuResourceCategory::Shadow,
			"Shadow.Atlas.Depth",
			static_cast<size_t>(ShadowAtlasResolution2D) * ShadowAtlasResolution2D * sizeof(float),
			ShadowAtlasResolution2D * ShadowAtlasResolution2D,
			sizeof(float),
			"FShadowAtlasManager");
	}

	if (ShadowAtlasManager.GetVarianceSRV() != nullptr)
	{
		OutStats.AddRecord(
			EGpuResourceCategory::Shadow,
			"Shadow.Atlas.Variance",
			static_cast<size_t>(ShadowAtlasResolution2D) * ShadowAtlasResolution2D * sizeof(float) * 2,
			ShadowAtlasResolution2D * ShadowAtlasResolution2D,
			sizeof(float) * 2,
			"FShadowAtlasManager");
	}

	if (ShadowAtlasManager.GetBlurSRV() != nullptr)
	{
		OutStats.AddRecord(
			EGpuResourceCategory::Shadow,
			"Shadow.Atlas.BlurIntermediate",
			static_cast<size_t>(ShadowAtlasResolution2D) * ShadowAtlasResolution2D * sizeof(float) * 2,
			ShadowAtlasResolution2D * ShadowAtlasResolution2D,
			sizeof(float) * 2,
			"FShadowAtlasManager");
	}

	if (ShadowAtlasManager.GetCubeSRV() != nullptr)
	{
		constexpr uint32 CubeFaceCount = MAX_SHADOW_CUBES * CUBE_FACE_COUNT;
		constexpr uint32 CubeFacePixels = SHADOW_CUBE_SIZE * SHADOW_CUBE_SIZE;
		OutStats.AddRecord(
			EGpuResourceCategory::Shadow,
			"Shadow.CubeArray.Depth",
			static_cast<size_t>(CubeFaceCount) * CubeFacePixels * sizeof(float),
			CubeFaceCount * CubeFacePixels,
			sizeof(float),
			"FShadowAtlasManager");
		OutStats.AddRecord(
			EGpuResourceCategory::Shadow,
			"Shadow.CubeDebugFaces",
			static_cast<size_t>(CubeFaceCount) * CubeFacePixels * sizeof(float),
			CubeFaceCount * CubeFacePixels,
			sizeof(float),
			"FShadowAtlasManager");
	}
}
