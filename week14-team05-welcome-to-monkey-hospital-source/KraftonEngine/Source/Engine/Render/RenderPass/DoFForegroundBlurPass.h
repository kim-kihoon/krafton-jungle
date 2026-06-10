#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FDoFForegroundBlurPass final : public FRenderPassBase
{
public:
	FDoFForegroundBlurPass();

	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
