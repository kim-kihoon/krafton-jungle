#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FDebugViewModeResolvePass final : public FRenderPassBase
{
public:
	FDebugViewModeResolvePass();

	bool BeginPass(const FPassContext& Ctx) override;
};
