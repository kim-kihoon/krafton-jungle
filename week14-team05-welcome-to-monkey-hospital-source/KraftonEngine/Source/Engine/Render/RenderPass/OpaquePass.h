#pragma once

#include "Render/RenderPass/RenderPassBase.h"

class FOpaquePass final : public FRenderPassBase
{
public:
	FOpaquePass();
	bool BeginPass(const FPassContext& Ctx) override;
};
