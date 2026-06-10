#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FDoFSetupPass final : public FRenderPassBase
{
public:
	FDoFSetupPass();

	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
