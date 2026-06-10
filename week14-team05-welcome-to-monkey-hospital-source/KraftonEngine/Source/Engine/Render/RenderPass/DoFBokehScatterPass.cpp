#include "DoFBokehScatterPass.h"
#include "RenderPassRegistry.h"

#include "Render/Command/DrawCommandList.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"

REGISTER_RENDER_PASS(FDoFBokehScatterPass)

FDoFBokehScatterPass::FDoFBokehScatterPass()
{
	PassType = ERenderPass::DoFBokehScatter;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
		ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FDoFBokehScatterPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	if (!Frame.RenderOptions.ShowFlags.bDoF || !Frame.DoFBokehRTV ||
		Frame.DoFBokehWidth <= 0.0f || Frame.DoFBokehHeight <= 0.0f ||
		!Frame.SceneColorCopySRV || !Frame.CoCSRV)
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	DC->OMSetRenderTargets(1, &Frame.DoFBokehRTV, nullptr);

	D3D11_VIEWPORT BokehViewport = {};
	BokehViewport.TopLeftX = 0.0f;
	BokehViewport.TopLeftY = 0.0f;
	BokehViewport.Width = Frame.DoFBokehWidth;
	BokehViewport.Height = Frame.DoFBokehHeight;
	BokehViewport.MinDepth = 0.0f;
	BokehViewport.MaxDepth = 1.0f;
	DC->RSSetViewports(1, &BokehViewport);

	ID3D11ShaderResourceView* SceneColorSRV = Frame.SceneColorCopySRV;
	ID3D11ShaderResourceView* CoCSRV = Frame.CoCSRV;
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &SceneColorSRV);
	DC->PSSetShaderResources(ESystemTexSlot::CoC, 1, &CoCSRV);

	Ctx.Cache.bForceAll = true;
	return true;
}

void FDoFBokehScatterPass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::CoC, 1, &NullSRV);
	DC->OMSetRenderTargets(1, &Ctx.Cache.RTV, Ctx.Cache.DSV);

	D3D11_VIEWPORT FullViewport = {};
	FullViewport.TopLeftX = 0.0f;
	FullViewport.TopLeftY = 0.0f;
	FullViewport.Width = Ctx.Frame.ViewportWidth;
	FullViewport.Height = Ctx.Frame.ViewportHeight;
	FullViewport.MinDepth = 0.0f;
	FullViewport.MaxDepth = 1.0f;
	DC->RSSetViewports(1, &FullViewport);

	Ctx.Cache.bForceAll = true;
}
