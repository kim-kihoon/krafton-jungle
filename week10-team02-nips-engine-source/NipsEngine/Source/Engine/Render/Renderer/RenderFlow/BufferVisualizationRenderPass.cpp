#include "BufferVisualizationRenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"

namespace
{
    constexpr uint32 BufferVisualizationNone = 0u;
    constexpr uint32 BufferVisualizationSceneDepth = 1u;
    constexpr uint32 BufferVisualizationWorldNormal = 2u;

    // 에디터 뷰모드를 셰이더에서 사용할 정수 모드 값으로 변환한다.
    // 새 buffer visualization view mode를 추가할 때 가장 먼저 확장해야 하는 매핑 지점이다.
    uint32 ResolveVisualizationMode(EViewMode ViewMode)
    {
        switch (ViewMode)
        {
        case EViewMode::SceneDepth:
            return BufferVisualizationSceneDepth;
        case EViewMode::WorldNormal:
            return BufferVisualizationWorldNormal;
        default:
            return BufferVisualizationNone;
        }
    }
}

bool FBufferVisualizationRenderPass::Initialize()
{
    return true;
}

bool FBufferVisualizationRenderPass::Release()
{
    ShaderBinding.reset();
    return true;
}

bool FBufferVisualizationRenderPass::Begin(const FRenderPassContext* Context)
{
    bSkipVisualizationDraw = false;

    // 시각화가 비활성일 때는 이전 패스 결과를 그대로 다음 단계로 넘긴다.
    ID3D11ShaderResourceView* FallbackSRV = PrevPassSRV ? PrevPassSRV : Context->RenderTargets->SceneColorSRV;
    ID3D11RenderTargetView* FallbackRTV = PrevPassRTV ? PrevPassRTV : Context->RenderTargets->SceneColorRTV;

    const EViewMode ViewMode = Context->RenderBus ? Context->RenderBus->GetViewMode() : EViewMode::Lit;
    const uint32 VisualizationMode = ResolveVisualizationMode(ViewMode);
    // 일반 렌더 모드에서는 draw 없이 pass-through 한다.
    // 즉 이 pass는 항상 동작하는 후처리가 아니라, view mode 확장을 위한 선택적 덮어쓰기 지점이다.
    if (VisualizationMode == BufferVisualizationNone)
    {
        OutSRV = FallbackSRV;
        OutRTV = FallbackRTV;
        bSkipVisualizationDraw = true;
        return true;
    }

    const bool bHasTargets =
        Context->RenderTargets->SceneFXAARTV &&
        Context->RenderTargets->SceneFXAASRV;
    const bool bHasInputs =
        Context->RenderTargets->SceneDepthSRV &&
        Context->RenderTargets->SceneNormalSRV;

    UShader* VisualizationShader = FResourceManager::Get().GetShader("Shaders/Multipass/BufferVisualizationPass.hlsl");
    // 필요한 버퍼나 셰이더가 없으면 안전하게 원본 출력으로 되돌린다.
    // 새 view mode를 추가할 때는 이 입력 조건도 함께 맞춰줘야 정상 출력된다.
    if (!bHasTargets || !bHasInputs || !VisualizationShader)
    {
        OutSRV = FallbackSRV;
        OutRTV = FallbackRTV;
        bSkipVisualizationDraw = true;
        return true;
    }

    // 셰이더가 바뀌었거나 아직 바인딩이 없으면 인스턴스를 다시 만든다.
    if (!ShaderBinding || ShaderBinding->GetShader() != VisualizationShader)
    {
        ShaderBinding = VisualizationShader->CreateBindingInstance(Context->Device);
    }

    if (!ShaderBinding)
    {
        OutSRV = FallbackSRV;
        OutRTV = FallbackRTV;
        bSkipVisualizationDraw = true;
        return true;
    }

    // fullscreen 시각화 셰이더에 현재 모드와 입력 버퍼를 넘긴다.
    // 새 buffer visualization view mode는 필요한 입력 리소스를 여기서 추가 바인딩하는 방식으로 확장한다.
	if (Context->RenderBus)
		ShaderBinding->ApplyFrameParameters(*Context->RenderBus);

    ShaderBinding->SetUInt("VisualizationMode", VisualizationMode);
    ShaderBinding->SetSRV("SceneDepth", Context->RenderTargets->SceneDepthSRV);
    ShaderBinding->SetSRV("SceneNormal", Context->RenderTargets->SceneNormalSRV);

    ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(EBlendType::Opaque);
    Context->DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);

    ID3D11RenderTargetView* RTV = Context->RenderTargets->SceneFXAARTV;
    Context->DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);

    // 버텍스 버퍼 없이 fullscreen triangle 하나로 화면 전체를 덮는다.
    Context->DeviceContext->IASetInputLayout(nullptr);
    Context->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    Context->DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    OutSRV = Context->RenderTargets->SceneFXAASRV;
    OutRTV = Context->RenderTargets->SceneFXAARTV;
    return true;
}

bool FBufferVisualizationRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    // pass-through 상태가 아니면 여기서 실제 시각화 draw를 수행한다.
    if (bSkipVisualizationDraw || !ShaderBinding)
    {
        return true;
    }

    ShaderBinding->Bind(Context->DeviceContext);
    Context->DeviceContext->Draw(3, 0);
    return true;
}

bool FBufferVisualizationRenderPass::End(const FRenderPassContext* Context)
{
    if (bSkipVisualizationDraw)
    {
        return true;
    }

    // 다음 패스에서 SRV 충돌이 나지 않도록 PS 슬롯을 정리한다.
    ID3D11ShaderResourceView* NullSRVs[] = { nullptr, nullptr };
    Context->DeviceContext->PSSetShaderResources(0, 2, NullSRVs);
    return true;
}
