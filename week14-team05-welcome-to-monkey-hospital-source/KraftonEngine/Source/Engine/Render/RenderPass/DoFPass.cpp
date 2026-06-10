#include "DoFPass.h"
#include "RenderPassRegistry.h"

#include "Render/Command/DrawCommandList.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"

REGISTER_RENDER_PASS(FDoFPass)

FDoFPass::FDoFPass()
{
	PassType = ERenderPass::DoF;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
		ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FDoFPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	const bool bDebugCoC = Frame.RenderOptions.ViewMode == EViewMode::DoFCoC;
	if ((!Frame.RenderOptions.ShowFlags.bDoF && !bDebugCoC) || !Frame.SceneColorCopyTexture ||
		!Frame.ViewportRenderTexture || !Frame.SceneColorCopySRV || !Frame.CoCSRV)
	{
		return false;
	}

	if (!bDebugCoC && (!Frame.DoFBackgroundSRV || !Frame.DoFForegroundSRV || !Frame.DoFBokehSRV))
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();

	DC->OMSetRenderTargets(1, &Ctx.Cache.RTV, Ctx.Cache.DSV);

	ID3D11ShaderResourceView* SceneColorSRV = Frame.SceneColorCopySRV;
	ID3D11ShaderResourceView* CoCSRV = Frame.CoCSRV;
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &SceneColorSRV);
	DC->PSSetShaderResources(ESystemTexSlot::CoC, 1, &CoCSRV);

	if (!bDebugCoC)
	{
		ID3D11ShaderResourceView* LayerSRVs[3] = {
			Frame.DoFBackgroundSRV,
			Frame.DoFForegroundSRV,
			Frame.DoFBokehSRV
		};
		DC->PSSetShaderResources(ESystemTexSlot::DoFBackground, 3, LayerSRVs);
	}

	Ctx.Cache.bForceAll = true;
	return true;
}

void FDoFPass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11ShaderResourceView* NullLayerSRVs[3] = {};
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::CoC, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DoFBackground, 3, NullLayerSRVs);
}
