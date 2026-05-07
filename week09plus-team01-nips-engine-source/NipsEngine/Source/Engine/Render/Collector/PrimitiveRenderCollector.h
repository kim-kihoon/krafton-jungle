#pragma once

#include "Render/Collector/RenderCollectionStats.h"
#include "Render/Scene/RenderBus.h"
#include "Spatial/WorldSpatialIndex.h"

enum class EWorldType : uint32;
class AActor;
class FLineBatcher;
class FMeshBufferManager;
class UPrimitiveComponent;
class FRenderCollector;

class FPrimitiveRenderCollector // RenderCollector와 has-a 구조
{
public:
	void Initialize(FMeshBufferManager* InMeshBufferManager) { MeshBufferManager = InMeshBufferManager; }
	void Release() { MeshBufferManager = nullptr; }

	void CollectFromActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType, FRenderCollectionStats& LastStats, FLineBatcher* LineBatcher);
	void CollectFromComponent(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType, FRenderCollectionStats& LastStats, FLineBatcher* LineBatcher);

private:
	FMeshBufferManager* MeshBufferManager = nullptr;
	FWorldSpatialIndex::FPrimitiveOBBQueryScratch OBBQueryScratch;
};
