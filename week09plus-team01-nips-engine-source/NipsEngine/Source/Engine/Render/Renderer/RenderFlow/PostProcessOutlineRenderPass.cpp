#include "PostProcessOutlineRenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/Shader.h"

namespace
{
	void BindOutlineMaskSRV(UMaterialInterface* Material, ID3D11Device* Device, ID3D11ShaderResourceView* MaskSRV)
	{
		if (!Material || !Device)
		{
			return;
		}

		if (UMaterial* BaseMaterial = Cast<UMaterial>(Material))
		{
			BaseMaterial->EnsureShaderBinding(Device);
			if (BaseMaterial->ShaderBinding)
			{
				BaseMaterial->ShaderBinding->SetSRV("SelectionMaskTexture", MaskSRV);
			}
			return;
		}

		UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Material);
		if (!MaterialInstance || !MaterialInstance->Parent || !MaterialInstance->Parent->Shader)
		{
			return;
		}

		if (!MaterialInstance->ShaderBinding || MaterialInstance->ShaderBinding->GetShader() != MaterialInstance->Parent->Shader)
		{
			MaterialInstance->ShaderBinding = MaterialInstance->Parent->Shader->CreateBindingInstance(Device);
		}

		if (MaterialInstance->ShaderBinding)
		{
			MaterialInstance->ShaderBinding->SetSRV("SelectionMaskTexture", MaskSRV);
		}
	}
}

bool FPostProcessOutlineRenderPass::Initialize()
{
    return true;
}

bool FPostProcessOutlineRenderPass::Release()
{
    FinalOverlayShaderBinding.reset();
    return true;
}

bool FPostProcessOutlineRenderPass::Begin(const FRenderPassContext* Context)
{
    ID3D11RenderTargetView* RTV = PrevPassRTV;
    Context->DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);

    Context->DeviceContext->IASetInputLayout(nullptr);
    Context->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    Context->DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;
    return true;
}

bool FPostProcessOutlineRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    /*  현재 파이프라인의 마지막 render pass. 먼저 selection / post - process outline을 그리고,
		그 다음 최종 fullscreen overlay 합성을 수행합니다. fade/letterbox 같은 카메라 overlay는
		최종 present 직전까지 렌더링된 모든 결과에 영향을 줘야 하므로 여기서 적용합니다. */

    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::PostProcessOutline);
    if (!Commands.empty())
    {
        for (const FRenderCommand& Cmd : Commands)
        {
            if (Cmd.Material != nullptr)
            {
                BindOutlineMaskSRV(Cmd.Material, Context->Device, Context->RenderTargets->SelectionMaskSRV);
                Cmd.Material->Bind(Context->DeviceContext, Context->RenderBus);
            }
            Context->DeviceContext->Draw(3, 0);
        }
    }

    if (Context == nullptr || Context->Device == nullptr || Context->DeviceContext == nullptr || Context->RenderBus == nullptr || Context->RenderTargets == nullptr)
    {
        return false;
    }

    if (PrevPassSRV == nullptr || Context->RenderTargets->SceneFXAARTV == nullptr || Context->RenderTargets->SceneFXAASRV == nullptr)
    {
        return true;
    }

    UShader* FinalOverlayShader = FResourceManager::Get().GetShader("Shaders/Multipass/FinalOverlayPass.hlsl");
    if (!FinalOverlayShader)
    {
        return false;
    }

    if (!FinalOverlayShaderBinding || FinalOverlayShaderBinding->GetShader() != FinalOverlayShader)
    {
        FinalOverlayShaderBinding = FinalOverlayShader->CreateBindingInstance(Context->Device);
    }

    if (!FinalOverlayShaderBinding)
    {
        return false;
    }

    ID3D11RenderTargetView* FinalRTV = Context->RenderTargets->SceneFXAARTV;
    Context->DeviceContext->OMSetRenderTargets(1, &FinalRTV, nullptr);

    const float Width = Context->RenderTargets->Width;
    const float Height = Context->RenderTargets->Height;
    const FCameraOverlaySettings& Overlay = Context->RenderBus->GetCameraOverlaySettings();

    FinalOverlayShaderBinding->ApplyFrameParameters(*Context->RenderBus);
    FinalOverlayShaderBinding->SetSRV("FinalSceneColor", PrevPassSRV);
    FinalOverlayShaderBinding->SetVector4("FadeColor", FVector4(Overlay.FadeColor));
    FinalOverlayShaderBinding->SetFloat("LetterBoxRatio", Overlay.LetterBoxRatio);
    FinalOverlayShaderBinding->SetVector2("InvResolution", FVector2((Width > 0.0f) ? (1.0f / Width) : 0.0f, (Height > 0.0f) ? (1.0f / Height) : 0.0f));
    FinalOverlayShaderBinding->SetAllSamplers(FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear));
    FinalOverlayShaderBinding->Bind(Context->DeviceContext);
    Context->DeviceContext->Draw(3, 0);

    OutSRV = Context->RenderTargets->SceneFXAASRV;
    OutRTV = Context->RenderTargets->SceneFXAARTV;

    return true;
}

bool FPostProcessOutlineRenderPass::End(const FRenderPassContext* Context)
{
    ID3D11ShaderResourceView* nullSRV = nullptr;
    Context->DeviceContext->PSSetShaderResources(0, 1, &nullSRV);
    Context->DeviceContext->PSSetShaderResources(7, 1, &nullSRV);

	ID3D11BlendState* OpaqueBlend = FResourceManager::Get().GetOrCreateBlendState(EBlendType::Opaque);
    float BlendFactor[4] = { 1.f, 1.f, 1.f, 1.f };
    Context->DeviceContext->OMSetBlendState(OpaqueBlend, BlendFactor, 0xFFFFFFFF);

    return true;
}
