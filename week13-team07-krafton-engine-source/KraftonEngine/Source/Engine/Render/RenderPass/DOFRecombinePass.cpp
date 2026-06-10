#include "DOFRecombinePass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"
#include "Render/Command/DrawCommandList.h"

REGISTER_RENDER_PASS(FDOFRecombinePass)

FDOFRecombinePass::FDOFRecombinePass()
{
	PassType = ERenderPass::DOFRecombine;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
		ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FDOFRecombinePass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	if (!Frame.bDepthOfFieldEnabled || !Frame.SceneColorCopyTexture || !Frame.SceneColorCopySRV || !Frame.ViewportRenderTexture
		|| !Frame.DOFCoCSRV || !Frame.DOFBlurSRV)
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	FStateCache& Cache = Ctx.Cache;

	ID3D11ShaderResourceView* NullSRVs[3] = { nullptr, nullptr, nullptr };
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRVs[0]);
	DC->PSSetShaderResources(ESystemTexSlot::DOFCoC, 1, &NullSRVs[1]);
	DC->PSSetShaderResources(ESystemTexSlot::DOFBlur, 1, &NullSRVs[2]);

	DC->OMSetRenderTargets(0, nullptr, nullptr);
	DC->CopyResource(Frame.SceneColorCopyTexture, Frame.ViewportRenderTexture);
	DC->OMSetRenderTargets(1, &Frame.ViewportRTV, Frame.ViewportDSV);

	ID3D11ShaderResourceView* SceneColorSRV = Frame.SceneColorCopySRV;
	ID3D11ShaderResourceView* CocSRV = Frame.DOFCoCSRV;
	ID3D11ShaderResourceView* BlurSRV = Frame.DOFBlurSRV;
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &SceneColorSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DOFCoC, 1, &CocSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DOFBlur, 1, &BlurSRV);

	Cache.RTV = Frame.ViewportRTV;
	Cache.DSV = Frame.ViewportDSV;
	Cache.bForceAll = true;
	return true;
}

void FDOFRecombinePass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::DOFCoC, 1, &NullSRV);
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::DOFBlur, 1, &NullSRV);
}
