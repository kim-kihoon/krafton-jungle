#pragma once
#include "RenderPass.h"
#include <memory>

class FShaderBindingInstance;

class FPostProcessOutlineRenderPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

private:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    std::shared_ptr<FShaderBindingInstance> FinalOverlayShaderBinding;
};
