#pragma once

#include "Render/RenderPass/RenderPassBase.h"

/*
	통합 Transparent 패스 — Font / SubUV / Billboard / Particle 등 반투명 지오메트리를 모두 흡수.
	기본 상태는 DepthReadOnly + AlphaBlend, 실제 Blend는 per-DrawCommand가 결정합니다
	(FDrawCommand::RenderState.Blend → DrawCommandList.cpp:173).
	- Transparent / Additive / Modulate 모두 이 단일 패스에서 처리.
	- 패스 분리 금지 — 깊이 교차가 어긋나면 시연 망함.
*/
class FTransparentPass final : public FRenderPassBase
{
public:
	FTransparentPass();
};

