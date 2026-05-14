#pragma once

#include "Render/Scene/RenderBus.h"

struct FCullingStats
{
	int32 TotalVisiblePrimitiveCount{0};
	int32 BVHPassedPrimitiveCount{0};
	int32 FallbackPassedPrimitiveCount{0};
};

struct FDecalStats
{
	int32 TotalDecalCount = 0;
	int32 CollectTimeMS = 0;
};

struct FShadowStats
{
	uint32 DirectionalLightCount = 0;
	uint32 PointLightCount = 0;
	uint32 SpotLightCount = 0;
	uint32 AmbientLightCount = 0;

	uint32 DirectionalShadowCount = 0;
	uint32 PointShadowCount = 0;
	uint32 SpotShadowCount = 0;

	size_t DirectionalShadowMemoryBytes = 0;
	size_t PointShadowMemoryBytes = 0;
	size_t SpotShadowMemoryBytes = 0;

	FDirectionalShadowConstants DirectionalShadowConstants = {};

	size_t GetTotalShadowMemoryBytes() const
	{
		return DirectionalShadowMemoryBytes + PointShadowMemoryBytes + SpotShadowMemoryBytes;
	}
};

struct FRenderCollectionStats
{
	FCullingStats Culling = {};
	FDecalStats Decal = {};
	FShadowStats Shadow = {};
};
