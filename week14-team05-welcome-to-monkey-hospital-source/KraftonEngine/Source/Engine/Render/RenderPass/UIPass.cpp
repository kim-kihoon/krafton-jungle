#include "UIPass.h"

#include "RenderPassRegistry.h"
#include "Render/Types/FrameContext.h"
#include "Render/Types/ViewModeUtils.h"
#include "UI/PhotoOverlay.h"
#include "UI/UIManager.h"

REGISTER_RENDER_PASS(FUIPass)

FUIPass::FUIPass()
{
	PassType = ERenderPass::UI;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::AlphaBlend,
	                ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FUIPass::BeginPass(const FPassContext& Ctx)
{
	if (ViewModeUtils::IsPureDebugViewMode(Ctx.Frame.RenderOptions.ViewMode))
		return false;

	// 사진 캡처 프레임에서는 최종 화면 캡처에 RmlUi가 섞이지 않도록 UI 패스만 건너뛴다.
	if (FPhotoOverlay::ShouldSuppressViewportUIForCapture())
		return false;

	return Ctx.Frame.ViewportRTV &&
		(UUIManager::Get().HasViewportWidgets() || UUIManager::Get().HasRuntimeOverlays(Ctx.Frame));
}

void FUIPass::Execute(const FPassContext& Ctx)
{
	UUIManager::Get().Render(Ctx);
	UUIManager::Get().RenderRuntimeOverlays(Ctx);
}
