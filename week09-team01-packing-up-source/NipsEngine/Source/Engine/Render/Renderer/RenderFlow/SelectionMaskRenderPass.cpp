#include "SelectionMaskRenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/Texture.h"

bool FSelectionMaskRenderPass::Initialize()
{
    return true;
}

bool FSelectionMaskRenderPass::Release()
{
    ShaderBinding.reset();
    BillboardShaderBinding.reset();
    return true;
}

bool FSelectionMaskRenderPass::Begin(const FRenderPassContext* Context)
{
    ID3D11RenderTargetView* RTV = Context->RenderTargets->SelectionMaskRTV;
    ID3D11DepthStencilView* DSV = Context->RenderTargets->DepthStencilView;
    Context->DeviceContext->OMSetRenderTargets(1, &RTV, DSV);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11DepthStencilState* DepthStencilState = FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::StencilWrite);
    ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(EBlendType::AlphaBlend);
    ID3D11RasterizerState* RasterizerState = FResourceManager::Get().GetOrCreateRasterizerState(ERasterizerType::SolidNoCull);
    Context->DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);
    Context->DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);
    Context->DeviceContext->RSSetState(RasterizerState);

    UShader* SelectionMaskShader = FResourceManager::Get().GetShader("Shaders/SelectionMask.hlsl");
    if (SelectionMaskShader)
    {
        if (!ShaderBinding || ShaderBinding->GetShader() != SelectionMaskShader)
        {
            ShaderBinding = SelectionMaskShader->CreateBindingInstance(Context->Device);
        }

        if (ShaderBinding && Context->RenderBus)
        {
            ShaderBinding->ApplyFrameParameters(*Context->RenderBus);
        }
    }

    UShader* BillboardMaskShader = FResourceManager::Get().GetShader("Shaders/BillboardSelectionMask.hlsl");
    if (BillboardMaskShader)
    {
        if (!BillboardShaderBinding || BillboardShaderBinding->GetShader() != BillboardMaskShader)
        {
            BillboardShaderBinding = BillboardMaskShader->CreateBindingInstance(Context->Device);
        }

        if (BillboardShaderBinding && Context->RenderBus)
        {
            BillboardShaderBinding->ApplyFrameParameters(*Context->RenderBus);
        }
    }

    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;
    return true;
}

bool FSelectionMaskRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::SelectionMask);
    if (Commands.empty())
    {
        return true;
    }

    for (const FRenderCommand& Cmd : Commands)
    {
        if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
        {
            continue;
        }

        uint32 offset = 0;
        ID3D11Buffer* vertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
        if (vertexBuffer == nullptr)
        {
            continue;
        }

        uint32 vertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
        uint32 stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
        if (vertexCount == 0 || stride == 0)
        {
            continue;
        }

        if (Cmd.Type == ERenderCommandType::SelectionMask)
        {
            if (FShaderBindingInstance* Binding = ShaderBinding.get())
            {
                Binding->ApplyPerObjectParameters(Cmd.PerObjectConstants);
                Binding->Bind(Context->DeviceContext);
            }
        }
        else if (Cmd.Type == ERenderCommandType::BillboardSelectionMask)
        {
            if (FShaderBindingInstance* Binding = BillboardShaderBinding.get())
            {
                Binding->ApplyPerObjectParameters(Cmd.PerObjectConstants);
                if (Cmd.Constants.Billboard.Texture)
                {
                    ID3D11SamplerState* Sampler = FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear);
                    Binding->SetSRV("DiffuseMap", Cmd.Constants.Billboard.Texture->GetSRV());
                    Binding->SetSampler("BillboardSampler", Sampler);
                }
                Binding->Bind(Context->DeviceContext);
            }
        }

        Context->DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        ID3D11Buffer* indexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
        if (indexBuffer != nullptr)
        {
            Context->DeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            Context->DeviceContext->DrawIndexed(Cmd.SectionIndexCount, Cmd.SectionIndexStart, 0);
        }
        else
        {
            Context->DeviceContext->Draw(vertexCount, 0);
        }
    }

    return true;
}

bool FSelectionMaskRenderPass::End(const FRenderPassContext* Context)
{
    return true;
}
