#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FDOFSetupPass final : public FRenderPassBase
{
public:
	FDOFSetupPass();
	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
