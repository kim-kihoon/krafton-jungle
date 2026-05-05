#pragma once
#include "Render/Collector/LightRenderCollector.h"
#include "Render/Collector/OverlayRenderCollector.h"
#include "Render/Collector/PrimitiveRenderCollector.h"
#include "Render/Collector/RenderCollectionStats.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Spatial/WorldSpatialIndex.h"

#include <unordered_set>

enum class EWorldType : uint32;

class UWorld;
class AActor;
class UPrimitiveComponent;
class UGizmoComponent;
class FLineBatcher;
struct FFrustum;

class FRenderCollector {
private:
	FMeshBufferManager MeshBufferManager;
	FLightRenderCollector LightRenderCollector;
	FOverlayRenderCollector OverlayRenderCollector;
	FPrimitiveRenderCollector PrimitiveRenderCollector;
	FLineBatcher* LineBatcher = nullptr;

	// ────── Initialize & Release ─────────────────────────────────────────────
public:
	void Initialize(ID3D11Device* InDevice);
	void Release();
	void SetLineBatcher(FLineBatcher* InLineBatcher) { LineBatcher = InLineBatcher; }
	void ClearLineBatcher() { LineBatcher = nullptr; }

	// ────── Main Collects ────────────────────────────────────────────────────
public:
	void CollectWorld(UWorld* World, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, const FFrustum* ViewFrustum = nullptr);

	// ────── Stats ────────────────────────────────────────────────────────────
private:
	FRenderCollectionStats LastStats;
	void ResetCullingStats();
	void ResetDecalStats();
	void ResetShadowStats();

public:
	const FRenderCollectionStats& GetLastStats() const { return LastStats; }
	const FCullingStats& GetLastCullingStats() const { return LastStats.Culling; }
	const FDecalStats& GetLastDecalStats() const { return LastStats.Decal; }
	const FShadowStats& GetLastShadowStats() const { return LastStats.Shadow; }

	using FCullingStats = ::FCullingStats;
	using FDecalStats = ::FDecalStats;
	using FShadowStats = ::FShadowStats;
	using FRenderCollectionStats = ::FRenderCollectionStats;

	// ────── Query Buffer ─────────────────────────────────────────────────────
private:
	FWorldSpatialIndex::FPrimitiveFrustumQueryScratch FrustumQueryScratch;
	TArray<UPrimitiveComponent*> VisiblePrimitiveScratch;	
	
	// ────── Sub Collects ─────────────────────────────────────────────────────
public: 
	void CollectSelection(const TArray<AActor*>& SelectedActors, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus);
	void CollectOutline(const TArray<AActor*>& Actors, const FVector4& OutlineColor, float OutlineThicknessPixels, FRenderBus& RenderBus);
	void CollectDebugBounds(UWorld* World, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus);
	void CollectGizmo(UGizmoComponent* Gizmo, const FShowFlags& ShowFlags, FRenderBus& RenderBus, bool bIsActiveOperation);
	void CollectGrid(float GridSpacing, int32 GridHalfLineCount, FRenderBus& RenderBus, bool bOrthographic = false);

private:
	void CollectLight(UWorld* World, FRenderBus& RenderBus, const FFrustum* ViewFrustum = nullptr);
	void CollectShadowCasters(UWorld* World, FRenderBus& RenderBus);
	void CollectFromActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType);
	void CollectFromComponent(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType);
	void CollectBVHInternalNodeAABBs(UPrimitiveComponent* PrimitiveComponent, const FShowFlags& ShowFlags, FRenderBus& RenderBus, std::unordered_set<int32>& SeenNodeIndices);
};
