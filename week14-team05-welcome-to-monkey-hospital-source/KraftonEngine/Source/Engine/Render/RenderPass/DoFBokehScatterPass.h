#pragma once

#include "RenderPassBase.h"

class FDoFBokehScatterPass final : public FRenderPassBase
{
public:
	FDoFBokehScatterPass();

	bool BeginPass(const FPassContext& Ctx) override;
	void EndPass(const FPassContext& Ctx) override;
};
