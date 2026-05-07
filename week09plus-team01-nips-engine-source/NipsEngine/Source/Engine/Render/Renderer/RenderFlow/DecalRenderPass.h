#pragma once
#include "RenderPass.h"
#include "Render/Common/ComPtr.h"

class FDecalRenderPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

private:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    bool bSkipDecalDraw = false;
    TComPtr<ID3D11Buffer> VisibleLightConstantBuffer;
    TComPtr<ID3D11Buffer> DirectionalShadowConstantBuffer;
    TComPtr<ID3D11Buffer> SpotShadowInfoConstantBuffer;
    TComPtr<ID3D11Buffer> SpotShadowConstantsBuffer;
    TComPtr<ID3D11ShaderResourceView> SpotShadowConstantsSRV;
    uint32 SpotShadowConstantsCapacity = 0;
    TComPtr<ID3D11Buffer> PointShadowInfoConstantBuffer;
    TComPtr<ID3D11Buffer> PointShadowConstantsBuffer;
    TComPtr<ID3D11ShaderResourceView> PointShadowConstantsSRV;
    uint32 PointShadowConstantsCapacity = 0;
};
