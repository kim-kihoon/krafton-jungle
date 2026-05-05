#include "PostProcessOutlineRenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/Material.h"

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
    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::PostProcessOutline);
    if (Commands.empty())
    {
        return true;
    }

    for (const FRenderCommand& Cmd : Commands)
    {
        if (Cmd.Material != nullptr)
        {
            BindOutlineMaskSRV(Cmd.Material, Context->Device, Context->RenderTargets->SelectionMaskSRV);
            Cmd.Material->Bind(Context->DeviceContext, Context->RenderBus);
        }
        Context->DeviceContext->Draw(3, 0);
    }

    return true;
}

bool FPostProcessOutlineRenderPass::End(const FRenderPassContext* Context)
{
    ID3D11ShaderResourceView* nullSRV = nullptr;
    Context->DeviceContext->PSSetShaderResources(7, 1, &nullSRV);

	ID3D11BlendState* OpaqueBlend = FResourceManager::Get().GetOrCreateBlendState(EBlendType::Opaque);
    float BlendFactor[4] = { 1.f, 1.f, 1.f, 1.f };
    Context->DeviceContext->OMSetBlendState(OpaqueBlend, BlendFactor, 0xFFFFFFFF);

    return true;
}
