#include "ViewModeMeshPass.h"
#include "RenderPassRegistry.h"

REGISTER_RENDER_PASS(FViewModeMeshPass)

FViewModeMeshPass::FViewModeMeshPass()
{
	PassType    = ERenderPass::ViewModeMesh;
	RenderState = { EDepthStencilState::DepthGreaterEqual, EBlendState::Opaque,
	                ERasterizerState::SolidBackCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
}
