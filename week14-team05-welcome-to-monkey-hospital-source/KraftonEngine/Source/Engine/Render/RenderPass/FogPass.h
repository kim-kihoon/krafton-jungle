#pragma once

#include "Render/RenderPass/RenderPassBase.h"

// FFogPass — HeightFog 풀스크린 패스. AdditiveDecal 直後, Transparent 前에 실행.
//   불투명/하늘만 fog를 입히고, Transparent는 fog 위에 그려지므로 fog의
//   "depth 없는 픽셀 덮어쓰기"에 가려지지 않는다. (Transparent self-fog는 UberTransparent.)
// BeginPass 는 PostProcess 와 동일하게 불투명 깊이를 복사본으로 갱신 → 셰이더가 t16(SceneDepth)로 읽는다.
class FFogPass final : public FRenderPassBase
{
public:
	FFogPass();
	bool BeginPass(const FPassContext& Ctx) override;
};
