#pragma once
#include "Render/Renderer/IRenderPipeline.h"
#include "Render/Collector/RenderCollector.h"
#include "Render/Scene/RenderBus.h"

class UEditorEngine;
class FEditorViewportClient;
struct FSceneView;

class FEditorRenderPipeline : public IRenderPipeline
{
public:
	FEditorRenderPipeline(UEditorEngine* InEditor, FRenderer& InRenderer);
	~FEditorRenderPipeline() override;

	void Execute(float DeltaTime, FRenderer& Renderer) override;
	const FRenderCollector::FCullingStats& GetViewportCullingStats(int32 ViewportIndex) const;
	const FRenderCollector::FDecalStats& GetViewportDecalStats(int32 ViewportIndex) const;
	const FRenderCollector::FShadowStats& GetViewportShadowStats(int32 ViewportIndex) const;

private:
	/*
	 * 단일 뷰포트 렌더 헬퍼.
	 * SetSubViewport → 씬 수집 → PrepareBatchers → Render 순으로 실행합니다.
	 * Execute 루프에서 4번 호출됩니다.
	 */
	void RenderViewport(FRenderer& Renderer, int32 ViewportIndex);
	bool PrepareViewport(FRenderer& Renderer, int32 ViewportIndex, FSceneView& OutSceneView, FEditorViewportClient*& OutViewportClient);

	UEditorEngine* Editor = nullptr;
	FRenderCollector Collector;
	FRenderBus Bus;
	TArray<FRenderCollector::FCullingStats> ViewportCullingStats;
	TArray<FRenderCollector::FDecalStats> ViewportDecalStats;
	TArray<FRenderCollector::FShadowStats> ViewportShadowStats;
};
