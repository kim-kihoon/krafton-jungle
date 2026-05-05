#pragma once

#include "Render/Collector/RenderCollectionStats.h"
#include "Render/Scene/RenderBus.h"
#include "Spatial/WorldSpatialIndex.h"

class FMeshBufferManager;
class ULightComponent;
class UPrimitiveComponent;
class USpotLightComponent;
class UWorld;
struct FFrustum;
class FRenderCollector;

class FLightRenderCollector // RenderCollector와 has-a 구조
{
public:
	void Initialize(FMeshBufferManager* InMeshBufferManager) { MeshBufferManager = InMeshBufferManager; }
	void Release() { MeshBufferManager = nullptr; }

	void CollectLight(UWorld* World, FRenderBus& RenderBus, FRenderCollectionStats& LastStats, const FFrustum* ViewFrustum = nullptr);
	void CollectShadowCasters(UWorld* World, FRenderBus& RenderBus);

private:
	struct FSpotShadowCandidate
	{
		FRenderLight RenderLight = {};
		const ULightComponent* LightComponent = nullptr;
		const USpotLightComponent* SpotLight = nullptr;

		FVector LightDirection = FVector::ZeroVector;

		float RequestedResolution = 0.0f;
		uint32 RequestedTileSize = 0;
		float PriorityScore = 0.0f;
	};

	FRenderCollectionStats* CurrentStats = nullptr;
	FMeshBufferManager* MeshBufferManager = nullptr;
	FWorldSpatialIndex::FPrimitiveFrustumQueryScratch ShadowFrustumQueryScratch;
	FWorldSpatialIndex::FPrimitiveSphereQueryScratch ShadowSphereQueryScratch;
	TArray<UPrimitiveComponent*> ShadowVisiblePrimitiveScratch;

	FRenderCollectionStats& GetStats() { return *CurrentStats; }

	void CollectAmbientLight(FRenderLight& RenderLight, FRenderBus& RenderBus);
	void CollectDirectionalLight(const ULightComponent* LightComponent, FRenderLight& RenderLight, FRenderBus& RenderBus);
	void CollectPointLight(const ULightComponent* LightComponent, FRenderLight& RenderLight, FRenderBus& RenderBus, const FFrustum* ViewFrustum, int32& NextPointShadowIndex);
	void CollectSpotLight(const ULightComponent* LightComponent, FRenderLight& RenderLight, FRenderBus& RenderBus, const FFrustum* ViewFrustum, TArray<FSpotShadowCandidate>& SpotShadowCandidates);
	void AllocateSpotShadowCandidates(TArray<FSpotShadowCandidate>& SpotShadowCandidates, FRenderBus& RenderBus, int32& NextSpotShadowIndex);
};
