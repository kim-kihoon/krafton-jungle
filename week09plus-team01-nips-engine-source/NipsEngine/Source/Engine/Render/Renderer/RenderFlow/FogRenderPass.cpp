#include "FogRenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Scene/RenderCommand.h"
#include <algorithm>

bool FFogRenderPass::Initialize()
{
    return true;
}

bool FFogRenderPass::Release()
{
    ShaderBinding.reset();
    return true;
}

bool FFogRenderPass::Begin(const FRenderPassContext* Context)
{
    bSkipFogDraw = false;

    const EViewMode ViewMode = Context->RenderBus ? Context->RenderBus->GetViewMode() : EViewMode::Lit;
    // view mode별 composite 우회 규칙은 공용 helper에서 관리한다.
    // 새 view mode가 fog를 건너뛰어야 하면 여기 if를 늘리지 말고 ShouldBypassSceneCompositePasses를 확장한다.
    if (ShouldBypassSceneCompositePasses(ViewMode))
    {
        OutSRV = PrevPassSRV ? PrevPassSRV : Context->RenderTargets->SceneColorSRV;
        OutRTV = PrevPassRTV ? PrevPassRTV : Context->RenderTargets->SceneColorRTV;
        bSkipFogDraw = true;
        return true;
    }

    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::Fog);
    if (Commands.empty())
    {
        OutSRV = PrevPassSRV;
        OutRTV = PrevPassRTV;
        bSkipFogDraw = true;
        return true;
    }

    ID3D11ShaderResourceView* FallbackSRV = PrevPassSRV ? PrevPassSRV : Context->RenderTargets->SceneColorSRV;
    ID3D11RenderTargetView* FallbackRTV = PrevPassRTV ? PrevPassRTV : Context->RenderTargets->SceneColorRTV;

    const bool bHasFogTargets =
        Context->RenderTargets->SceneFogRTV &&
        Context->RenderTargets->SceneFogSRV;
    const bool bHasFogInputs =
        Context->RenderTargets->SceneColorSRV &&
        Context->RenderTargets->SceneNormalSRV &&
        Context->RenderTargets->SceneDepthSRV &&
        Context->RenderTargets->SceneWorldPosSRV &&
        FallbackSRV &&
        FallbackRTV;

    UShader* FogPassShader = FResourceManager::Get().GetShader("Shaders/Multipass/FogPass.hlsl");
    if (!bHasFogTargets || !bHasFogInputs || !FogPassShader)
    {
        OutSRV = FallbackSRV;
        OutRTV = FallbackRTV;
        bSkipFogDraw = true;
        return true;
    }

    if (!ShaderBinding || ShaderBinding->GetShader() != FogPassShader)
    {
        ShaderBinding = FogPassShader->CreateBindingInstance(Context->Device);
    }

    if (!ShaderBinding)
    {
        OutSRV = FallbackSRV;
        OutRTV = FallbackRTV;
        bSkipFogDraw = true;
        return true;
    }

    ShaderBinding->ApplyFrameParameters(*Context->RenderBus);
    ShaderBinding->SetSRV("SceneColor", Context->RenderTargets->SceneColorSRV);
    ShaderBinding->SetSRV("SceneNormal", Context->RenderTargets->SceneNormalSRV);
    ShaderBinding->SetSRV("SceneDepth", Context->RenderTargets->SceneDepthSRV);
    ShaderBinding->SetSRV("ScenePrevPassColor", FallbackSRV);
    ShaderBinding->SetSRV("SceneWorldPos", Context->RenderTargets->SceneWorldPosSRV);
    ShaderBinding->SetAllSamplers(FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear));

    ID3D11DepthStencilState* DSState = FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::StencilWrite);
    Context->DeviceContext->OMSetDepthStencilState(DSState, 1);

    ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(EBlendType::Opaque);
    Context->DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);

    ID3D11RenderTargetView* RTVs[3] = { Context->RenderTargets->SceneFogRTV, nullptr, nullptr };
    Context->DeviceContext->OMSetRenderTargets(3, RTVs, nullptr);

    Context->DeviceContext->IASetInputLayout(nullptr);
    Context->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    Context->DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    OutSRV = Context->RenderTargets->SceneFogSRV;
    OutRTV = Context->RenderTargets->SceneFogRTV;
    return true;
}

bool FFogRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    if (bSkipFogDraw || !ShaderBinding)
    {
        return true;
    }

    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::Fog);
    if (Commands.empty())
    {
        return true;
    }

    FFogPassConstants FogPassConstants = {};
    FogPassConstants.FogCount = std::min<uint32>(static_cast<uint32>(Commands.size()), MaxFogLayerCount);
    for (uint32 FogIndex = 0; FogIndex < FogPassConstants.FogCount; ++FogIndex)
    {
        FogPassConstants.Layers[FogIndex] = Commands[FogIndex].Constants.Fog;
    }

    ShaderBinding->SetUInt("FogLayerCount", FogPassConstants.FogCount);
    ShaderBinding->SetBytes("FogLayers", FogPassConstants.Layers, sizeof(FogPassConstants.Layers));
    ShaderBinding->Bind(Context->DeviceContext);

    Context->DeviceContext->Draw(3, 0);
    return true;
}

bool FFogRenderPass::End(const FRenderPassContext* Context)
{
    if (bSkipFogDraw)
    {
        return true;
    }

    ID3D11ShaderResourceView* NullSRVs[] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    Context->DeviceContext->PSSetShaderResources(0, 5, NullSRVs);
    return true;
}
