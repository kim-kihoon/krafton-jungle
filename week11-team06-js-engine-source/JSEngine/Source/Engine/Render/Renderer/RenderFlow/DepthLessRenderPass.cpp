#include "DepthLessRenderPass.h"
#include "Render/Common/ShaderBindingSlots.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/VertexFactoryTypes.h"
#include "Core/ResourceManager.h"

namespace
{
    FShaderProgram* GetDepthLessShaderProgram(const FRenderCommand& Cmd)
    {
        if (!Cmd.Material)
        {
            return nullptr;
        }

        const FVertexFactoryDesc& VertexFactoryDesc = FVertexFactoryRegistry::Get(Cmd.VertexFactoryType);

        FShaderStageKey VSKey;
        VSKey.FilePath = VertexFactoryDesc.VertexShaderPath;
        VSKey.EntryPoint = VertexFactoryDesc.BasePassVSEntry;
        VSKey.Target = "vs_5_0";

        FShaderStageKey PSKey;
        PSKey.FilePath = Cmd.Material->GetPixelShaderPath();
        PSKey.EntryPoint = Cmd.Material->GetPixelShaderEntryPoint();
        PSKey.Target = "ps_5_0";

        return FResourceManager::Get().GetOrCreateShaderProgram(
            VSKey,
            PSKey,
            nullptr,
            nullptr,
            &VertexFactoryDesc.VertexLayout);
    }
}

bool FDepthLessRenderPass::Initialize()
{
    return true;
}

bool FDepthLessRenderPass::Release()
{
    return true;
}

bool FDepthLessRenderPass::Begin(const FRenderPassContext* Context)
{

    ID3D11RenderTargetView* RTV = PrevPassRTV;
    ID3D11DepthStencilView* DSV = Context->RenderTargets->DepthStencilView;

    ID3D11ShaderResourceView* NullSRV[1] = { nullptr };
    Context->DeviceContext->PSSetShaderResources(ShaderBindingSlots::VSMShadowMapSRV, 1, NullSRV);

    Context->DeviceContext->OMSetRenderTargets(1, &RTV, DSV);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;
    return true;
}

bool FDepthLessRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::DepthLess);
    if (Commands.empty())
    {
        return true;
    }

    for (const FRenderCommand& Cmd : Commands)
    {
        Context->RenderResources->PerObjectConstantBuffer.Update(Context->DeviceContext, &Cmd.PerObjectConstants, sizeof(FPerObjectConstants));
        ID3D11Buffer* cb1 = Context->RenderResources->PerObjectConstantBuffer.GetBuffer();
        Context->DeviceContext->VSSetConstantBuffers(ShaderBindingSlots::PerObjectCB, 1, &cb1);
        Context->DeviceContext->PSSetConstantBuffers(ShaderBindingSlots::PerObjectCB, 1, &cb1);

        FMeshDrawBinding DrawBinding;
        if (!BuildMeshDrawBinding(Cmd, DrawBinding))
        {
            continue;
        }

        if (Cmd.Material != nullptr)
        {
            FShaderProgram* Program = GetDepthLessShaderProgram(Cmd);
            if (!Program)
            {
                continue;
            }

            Program->Bind(Context->DeviceContext);
            Cmd.Material->BindRenderStates(Context->DeviceContext);
            Cmd.Material->BindParameters(Context->DeviceContext, Program->PS);
            BindVertexFactoryResources(Context->DeviceContext, Cmd.VertexFactoryType, Cmd);
        }

        BindMeshDrawBinding(Context->DeviceContext, DrawBinding);

        if (DrawBinding.HasIndexBuffer())
        {
            Context->DeviceContext->DrawIndexed(DrawBinding.IndexCount, DrawBinding.IndexStart, 0);
        }
        else
        {
            Context->DeviceContext->Draw(DrawBinding.VertexCount, 0);
        }
    }

    return true;
}

bool FDepthLessRenderPass::End(const FRenderPassContext* Context)
{
    return true;
}
