#pragma once

#include "Render/RenderPass/RenderPassBase.h"

// 에디터 아이콘(라이트/포그/데칼 등 빌보드) 전용 오버레이 패스.
// 포스트프로세스/FXAA 이후 실행 + NoDepth(항상 위) + AlphaBlend.
// → 깊이 쓰기/포그 합성과 분리되어, 뒤 배경이 비어도 사라지지 않고 소프트 엣지가 유지된다.
class FEditorIconPass final : public FRenderPassBase
{
public:
	FEditorIconPass();
};
