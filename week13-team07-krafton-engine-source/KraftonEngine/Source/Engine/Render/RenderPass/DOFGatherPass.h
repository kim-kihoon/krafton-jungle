#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FDOFGatherPass final : public FRenderPassBase
{
public:
	FDOFGatherPass();
	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
