#include "EditorIconPass.h"
#include "RenderPassRegistry.h"

REGISTER_RENDER_PASS(FEditorIconPass)

FEditorIconPass::FEditorIconPass()
{
	PassType    = ERenderPass::EditorIcon;
	RenderState = { EDepthStencilState::DepthReadOnly, EBlendState::AlphaBlend,
	                ERasterizerState::SolidBackCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}
