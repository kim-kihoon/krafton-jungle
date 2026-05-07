#pragma once
#include "RenderPass.h"
#include <memory>

class FShaderBindingInstance;

// Scene Depth / World Normal 같은 버퍼 시각화를 fullscreen pass로 출력하는 렌더 패스다.
// 새 buffer visualization view mode를 추가할 때 이 pass와 대응 HLSL을 함께 확장한다.
class FBufferVisualizationRenderPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

private:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    // 현재 뷰모드에서 시각화 draw를 생략해야 하면 true로 유지한다.
    bool bSkipVisualizationDraw = false;
    // fullscreen 시각화 셰이더 바인딩을 재사용한다.
    std::shared_ptr<FShaderBindingInstance> ShaderBinding;
};
