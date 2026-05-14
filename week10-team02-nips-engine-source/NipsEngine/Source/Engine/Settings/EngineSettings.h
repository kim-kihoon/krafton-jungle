#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Singleton.h"
#include "Spatial/WorldSpatialIndex.h"

// 에디터와 게임이 함께 사용하는 런타임 엔진 설정을 보관합니다. 
class FEngineSettings : public TSingleton<FEngineSettings>
{
	friend class TSingleton<FEngineSettings>;

public:
	bool bEnableStaticMeshLOD = true;

	int32 SpatialBatchRefitMinDirtyCount = 8;
	int32 SpatialBatchRefitDirtyPercentThreshold = 15;
	int32 SpatialRotationStructuralChangeThreshold = 8;
	int32 SpatialRotationDirtyCountThreshold = 24;
	int32 SpatialRotationDirtyPercentThreshold = 30;

	void ApplyToSpatialPolicy(FWorldSpatialIndex::FMaintenancePolicy& Policy) const
	{
		Policy.BatchRefitMinDirtyCount = std::max<int32>(1, SpatialBatchRefitMinDirtyCount);
		Policy.BatchRefitDirtyPercentThreshold = std::clamp<int32>(SpatialBatchRefitDirtyPercentThreshold, 1, 100);
		Policy.RotationStructuralChangeThreshold = std::max<int32>(1, SpatialRotationStructuralChangeThreshold);
		Policy.RotationDirtyCountThreshold = std::max<int32>(1, SpatialRotationDirtyCountThreshold);
		Policy.RotationDirtyPercentThreshold = std::clamp<int32>(SpatialRotationDirtyPercentThreshold, 1, 100);
	}

private:
	FEngineSettings() = default;
};
