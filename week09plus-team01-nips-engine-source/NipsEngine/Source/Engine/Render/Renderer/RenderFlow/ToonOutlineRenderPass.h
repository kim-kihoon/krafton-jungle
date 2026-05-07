#pragma once
#include "RenderPass.h"
#include "Render/Common/ComPtr.h"

class FToonOutlineRenderPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

protected:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

    bool EnsureConstantBuffer(ID3D11Device* Device);
    bool EnsureRenderStates(ID3D11Device* Device);

private:
    TComPtr<ID3D11RasterizerState> FrontCullRS;
    TComPtr<ID3D11Buffer> FrameConstantBuffer;
    TComPtr<ID3D11Buffer> ToonOutlineConstantBuffer;
};
