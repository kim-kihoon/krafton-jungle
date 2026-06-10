#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FDoFBackgroundBlurPass final : public FRenderPassBase
{
public:
	FDoFBackgroundBlurPass();

	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
