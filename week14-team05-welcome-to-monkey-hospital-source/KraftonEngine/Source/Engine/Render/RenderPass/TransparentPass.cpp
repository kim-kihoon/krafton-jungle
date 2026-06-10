#include "TransparentPass.h"
#include "RenderPassRegistry.h"

REGISTER_RENDER_PASS(FTransparentPass)

FTransparentPass::FTransparentPass()
{
	PassType    = ERenderPass::Transparent;
	// DepthReadOnly: 반투명은 깊이 테스트만 하고 깊이를 쓰지 않음 (정렬로 교차 처리).
	// Blend는 per-DrawCommand가 override — 여기 AlphaBlend는 fallback일 뿐.
	RenderState = { EDepthStencilState::DepthReadOnly, EBlendState::AlphaBlend,
	                ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
}

