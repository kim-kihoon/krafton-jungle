#include "DoFBackgroundBlurPass.h"
#include "RenderPassRegistry.h"

#include "Render/Command/DrawCommandList.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"

REGISTER_RENDER_PASS(FDoFBackgroundBlurPass)

FDoFBackgroundBlurPass::FDoFBackgroundBlurPass()
{
	PassType = ERenderPass::DoFBackgroundBlur;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
		ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FDoFBackgroundBlurPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	if (!Frame.RenderOptions.ShowFlags.bDoF || !Frame.DoFBackgroundRTV ||
		!Frame.SceneColorCopyTexture || !Frame.ViewportRenderTexture ||
		!Frame.SceneColorCopySRV || !Frame.CoCSRV || !Frame.DepthCopySRV)
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	DC->CopyResource(Frame.SceneColorCopyTexture, Frame.ViewportRenderTexture);
	DC->OMSetRenderTargets(1, &Frame.DoFBackgroundRTV, nullptr);

	ID3D11ShaderResourceView* DepthSRV = Frame.DepthCopySRV;
	ID3D11ShaderResourceView* SceneColorSRV = Frame.SceneColorCopySRV;
	ID3D11ShaderResourceView* CoCSRV = Frame.CoCSRV;
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &DepthSRV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &SceneColorSRV);
	DC->PSSetShaderResources(ESystemTexSlot::CoC, 1, &CoCSRV);

	Ctx.Cache.bForceAll = true;
	return true;
}

void FDoFBackgroundBlurPass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::CoC, 1, &NullSRV);
	DC->OMSetRenderTargets(1, &Ctx.Cache.RTV, Ctx.Cache.DSV);
	Ctx.Cache.bForceAll = true;
}
