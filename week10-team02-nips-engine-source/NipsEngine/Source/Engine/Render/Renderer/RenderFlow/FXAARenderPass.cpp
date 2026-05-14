#include "FXAARenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"

bool FFXAARenderPass::Initialize()
{
    return true;
}

bool FFXAARenderPass::Release()
{
    ShaderBinding.reset();
    return true;
}

bool FFXAARenderPass::Begin(const FRenderPassContext* Context)
{
    bSkipFXAADraw = false;

    const EViewMode ViewMode = Context->RenderBus ? Context->RenderBus->GetViewMode() : EViewMode::Lit;
    // view mode별 composite 우회 규칙은 공용 helper에서 관리한다.
    // 새 view mode가 FXAA를 건너뛰어야 하면 여기 if를 늘리지 말고 ShouldBypassSceneCompositePasses를 확장한다.
    if (ShouldBypassSceneCompositePasses(ViewMode))
    {
        OutSRV = PrevPassSRV ? PrevPassSRV : Context->RenderTargets->SceneColorSRV;
        OutRTV = PrevPassRTV ? PrevPassRTV : Context->RenderTargets->SceneColorRTV;
        bSkipFXAADraw = true;
        return true;
    }

    const FRenderTargetSet* RenderTargets = Context->RenderTargets;
    ID3D11RenderTargetView* RTVs[1] = { RenderTargets->SceneFXAARTV };
    Context->DeviceContext->OMSetRenderTargets(ARRAYSIZE(RTVs), RTVs, nullptr);

    OutSRV = RenderTargets->SceneFXAASRV;
    OutRTV = RenderTargets->SceneFXAARTV;

    UShader* FXAAShader = FResourceManager::Get().GetShader("Shaders/Multipass/FXAAPass.hlsl");
    if (!FXAAShader)
    {
        return false;
    }

    if (!ShaderBinding || ShaderBinding->GetShader() != FXAAShader)
    {
        ShaderBinding = FXAAShader->CreateBindingInstance(Context->Device);
    }

    if (!ShaderBinding)
    {
        return false;
    }

    ShaderBinding->ApplyFrameParameters(*Context->RenderBus);
    ShaderBinding->SetSRV("FinalSceneColor", PrevPassSRV);
    ShaderBinding->SetVector2(
        "InvResolution",
        FVector2(
            (Context->RenderTargets->Width > 0.0f) ? (1.0f / Context->RenderTargets->Width) : 0.0f,
            (Context->RenderTargets->Height > 0.0f) ? (1.0f / Context->RenderTargets->Height) : 0.0f));
    ShaderBinding->SetUInt("Enabled", Context->RenderBus->GetFXAAEnabled() ? 1u : 0u);
    ShaderBinding->SetAllSamplers(FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear));

    Context->DeviceContext->IASetInputLayout(nullptr);
    Context->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    Context->DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    return true;
}

bool FFXAARenderPass::DrawCommand(const FRenderPassContext* Context)
{
    if (bSkipFXAADraw)
    {
        return true;
    }

    if (!ShaderBinding)
    {
        return false;
    }

    ShaderBinding->Bind(Context->DeviceContext);
    Context->DeviceContext->Draw(3, 0);
    return true;
}

bool FFXAARenderPass::End(const FRenderPassContext* Context)
{
    if (bSkipFXAADraw)
    {
        return true;
    }

    ID3D11ShaderResourceView* NullSRVs[] = { nullptr };
    Context->DeviceContext->PSSetShaderResources(0, 1, NullSRVs);
    return true;
}
