#include "OpaquePass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"
#include "Render/Command/DrawCommandList.h"

REGISTER_RENDER_PASS(FOpaquePass)

FOpaquePass::FOpaquePass()
{
	PassType    = ERenderPass::Opaque;
	RenderState = { EDepthStencilState::DepthGreaterEqual, EBlendState::Opaque,
	                ERasterizerState::SolidBackCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
}

bool FOpaquePass::BeginPass(const FPassContext& Ctx)
{
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	FStateCache& Cache = Ctx.Cache;

	DC->OMSetRenderTargets(1, &Cache.RTV, Cache.DSV);

	Cache.bForceAll = true;
	return true;
}
