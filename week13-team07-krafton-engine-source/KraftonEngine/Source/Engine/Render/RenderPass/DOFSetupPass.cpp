#include "DOFSetupPass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/RenderConstants.h"
#include "Render/Command/DrawCommandList.h"

REGISTER_RENDER_PASS(FDOFSetupPass)

FDOFSetupPass::FDOFSetupPass()
{
	PassType = ERenderPass::DOFSetup;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
		ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FDOFSetupPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	if (!Frame.bDepthOfFieldEnabled || !Frame.DepthTexture || !Frame.DepthCopyTexture || !Frame.DepthCopySRV || !Frame.DOFCoCRTV)
	{
		return false;
	}

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	FStateCache& Cache = Ctx.Cache;

	ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, NullSRVs);

	DC->OMSetRenderTargets(0, nullptr, nullptr);
	DC->CopyResource(Frame.DepthCopyTexture, Frame.DepthTexture);
	DC->OMSetRenderTargets(1, &Frame.DOFCoCRTV, nullptr);

	ID3D11ShaderResourceView* DepthSRV = Frame.DepthCopySRV;
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &DepthSRV);

	Cache.bForceAll = true;
	return true;
}

void FDOFSetupPass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	Ctx.Device.GetDeviceContext()->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);
}
