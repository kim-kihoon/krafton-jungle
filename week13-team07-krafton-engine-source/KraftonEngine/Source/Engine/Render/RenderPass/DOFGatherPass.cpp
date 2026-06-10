#include "DOFGatherPass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"
#include "Render/Command/DrawCommandList.h"

REGISTER_RENDER_PASS(FDOFGatherPass)

FDOFGatherPass::FDOFGatherPass()
{
	PassType = ERenderPass::DOFGather;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
		ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FDOFGatherPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	if (!Frame.bDepthOfFieldEnabled || !Frame.SceneColorCopyTexture || !Frame.SceneColorCopySRV || !Frame.ViewportRenderTexture || !Frame.DOFBlurRTV || !Frame.DOFCoCSRV)
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	FStateCache& Cache = Ctx.Cache;

	ID3D11ShaderResourceView* NullSRV = nullptr;
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);

	DC->OMSetRenderTargets(0, nullptr, nullptr);
	DC->CopyResource(Frame.SceneColorCopyTexture, Frame.ViewportRenderTexture);
	DC->OMSetRenderTargets(1, &Frame.DOFBlurRTV, nullptr);

	ID3D11ShaderResourceView* SceneColorSRV = Frame.SceneColorCopySRV;
	ID3D11ShaderResourceView* CocSRV = Frame.DOFCoCSRV;
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &SceneColorSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DOFCoC, 1, &CocSRV);

	Cache.bForceAll = true;
	return true;
}

void FDOFGatherPass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRVs[0]);
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::DOFCoC, 1, &NullSRVs[1]);
}
