#include "BillboardRenderPass.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/Material.h"

bool FBillboardRenderPass::Initialize()
{
    return true;
}

bool FBillboardRenderPass::Release()
{
    return true;
}

bool FBillboardRenderPass::Begin(const FRenderPassContext* Context)
{
    ID3D11RenderTargetView* RTV = PrevPassRTV;
    ID3D11DepthStencilView* DSV = Context->RenderTargets->DepthStencilView;
    Context->DeviceContext->OMSetRenderTargets(1, &RTV, DSV);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;
    return true;
}

bool FBillboardRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::Billboard);
    if (Commands.empty())
    {
        return true;
    }

    for (const FRenderCommand& Cmd : Commands)
    {
        if (Cmd.Material == nullptr || Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
        {
            continue;
        }

        ID3D11Buffer* VertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
        if (VertexBuffer == nullptr)
        {
            continue;
        }

        uint32 VertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
        uint32 Stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
        if (VertexCount == 0 || Stride == 0)
        {
            continue;
        }

        UMaterial* Mat = static_cast<UMaterial*>(Cmd.Material);
        Mat->SetTexture("DiffuseMap", Cmd.Constants.Billboard.Texture);
        Mat->Bind(Context->DeviceContext, Context->RenderBus, &Cmd.PerObjectConstants);

        uint32 Offset = 0;
        Context->DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

        ID3D11Buffer* IndexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
        if (IndexBuffer != nullptr)
        {
            Context->DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
            Context->DeviceContext->DrawIndexed(6, 0, 0);
        }
    }

    return true;
}

bool FBillboardRenderPass::End(const FRenderPassContext* Context)
{
    return true;
}
