#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FDOFRecombinePass final : public FRenderPassBase
{
public:
	FDOFRecombinePass();
	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
