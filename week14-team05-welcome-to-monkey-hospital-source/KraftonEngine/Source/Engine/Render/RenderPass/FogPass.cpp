#include "FogPass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"

REGISTER_RENDER_PASS(FFogPass)
#include "Render/Types/FrameContext.h"
#include "Render/Command/DrawCommandList.h"

FFogPass::FFogPass()
{
	PassType    = ERenderPass::Fog;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::AlphaBlend,
	                ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FFogPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	if (!Frame.DepthTexture || !Frame.DepthCopyTexture)
		return false;

	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	FStateCache& Cache = Ctx.Cache;

	// 불투명 깊이를 복사본으로 갱신 → fog 셰이더가 t16(SceneDepth)로 읽는다.
	// (t16 SRV 바인딩 자체는 PreDepthPass 에서 이뤄지며 프레임 내 유지됨 — PostProcess fog 와 동일 전제.)
	// Transparent 前이라 복사본은 불투명+데칼까지의 깊이를 담는다.
	DC->OMSetRenderTargets(0, nullptr, nullptr);
	DC->CopyResource(Frame.DepthCopyTexture, Frame.DepthTexture);
	DC->OMSetRenderTargets(1, &Cache.RTV, Cache.DSV);

	Cache.bForceAll = true;
	return true;
}
