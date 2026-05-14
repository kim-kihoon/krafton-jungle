#pragma once
#include "IRenderPipeline.h"
#include "Render/Collector/RenderCollector.h"
#include "Render/Scene/RenderBus.h"

class UEngine;

class FDefaultRenderPipeline : public IRenderPipeline
{
public:
	FDefaultRenderPipeline(UEngine* InEngine, FRenderer& InRenderer);
	~FDefaultRenderPipeline() override;

	virtual void Execute(float DeltaTime, FRenderer& Renderer) {}

private:
	UEngine* Engine = nullptr;
	FRenderCollector Collector;
	FRenderBus Bus;
};
