#pragma once

#include "Render/Scene/RenderBus.h"
#include <unordered_set>

class AActor;
class FLineBatcher;
class FMeshBufferManager;
class UGizmoComponent;
class UPrimitiveComponent;
class UWorld;
class FRenderCollector;

class FOverlayRenderCollector // RenderCollector와 has-a 구조
{
public:
	void Initialize(FMeshBufferManager* InMeshBufferManager) { MeshBufferManager = InMeshBufferManager; }
	void Release() { MeshBufferManager = nullptr; }

	void CollectSelection(const TArray<AActor*>& SelectedActors, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, FLineBatcher* LineBatcher);
	void CollectOutline(const TArray<AActor*>& Actors, const FVector4& OutlineColor, float OutlineThicknessPixels, FRenderBus& RenderBus);
	void CollectDebugBounds(UWorld* World, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, FLineBatcher* LineBatcher);
	void CollectGizmo(UGizmoComponent* Gizmo, const FShowFlags& ShowFlags, FRenderBus& RenderBus, bool bIsActiveOperation);
	void CollectGrid(float GridSpacing, int32 GridHalfLineCount, FRenderBus& RenderBus, bool bOrthographic = false);
	void CollectBVHInternalNodeAABBs(UPrimitiveComponent* PrimitiveComponent, const FShowFlags& ShowFlags, FRenderBus& RenderBus, FLineBatcher* LineBatcher, std::unordered_set<int32>& SeenNodeIndices);

private:
	bool CollectFromSelectedActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, FLineBatcher* LineBatcher);
	void CollectDebugBoundsFromActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, FLineBatcher* LineBatcher, std::unordered_set<int32>& SeenBVHNodeIndices);

	FMeshBufferManager* MeshBufferManager = nullptr;
};
