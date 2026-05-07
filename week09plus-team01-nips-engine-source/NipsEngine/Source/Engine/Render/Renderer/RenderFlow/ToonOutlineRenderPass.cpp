#include "ToonOutlineRenderPass.h"
#include "Core/ResourceManager.h"
#include <cstring>

namespace
{
struct FFrameConstantBuffer
{
    FMatrix View;
    FMatrix Projection;
};

// ★ 추가
struct FToonOutlineConstantBuffer
{
    float OutlineThickness;
    float Pad[3];
};
} // namespace

bool FToonOutlineRenderPass::Initialize()
{
    return true;
}

bool FToonOutlineRenderPass::Begin(const FRenderPassContext* Context)
{
    if (!EnsureConstantBuffer(Context->Device))
        return false;
    if (!EnsureRenderStates(Context->Device))
        return false;

    const FRenderTargetSet* RenderTargets = Context->RenderTargets;
    ID3D11RenderTargetView* RTVs[1] = { RenderTargets->SceneColorRTV };
    ID3D11DepthStencilView* DSV = RenderTargets->DepthStencilView;
    Context->DeviceContext->OMSetRenderTargets(ARRAYSIZE(RTVs), RTVs, DSV);
    OutSRV = RenderTargets->SceneColorSRV;
    OutRTV = RenderTargets->SceneColorRTV;
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    FFrameConstantBuffer Constants = {};
    Constants.View = Context->RenderBus->GetView();
    Constants.Projection = Context->RenderBus->GetProj();
    D3D11_MAPPED_SUBRESOURCE MappedCB = {};
    if (FAILED(Context->DeviceContext->Map(FrameConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedCB)))
        return false;
    std::memcpy(MappedCB.pData, &Constants, sizeof(Constants));
    Context->DeviceContext->Unmap(FrameConstantBuffer.Get(), 0);
    ID3D11Buffer* CBuffers[] = { FrameConstantBuffer.Get() };
    Context->DeviceContext->VSSetConstantBuffers(0, 1, CBuffers);

    return true;
}

bool FToonOutlineRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    const FRenderBus* RenderBus = Context->RenderBus;
    const TArray<FRenderCommand>& Commands = RenderBus->GetCommands(ERenderPass::ToonOutline);
    if (Commands.empty())
        return true;

    UShader* Shader = FResourceManager::Get().GetShader("Shaders/Multipass/ToonOutlinePass.hlsl");

    for (const FRenderCommand& Cmd : Commands)
    {
        if (Cmd.Type == ERenderCommandType::PostProcessOutline)
            continue;
        if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
            return false;

        uint32 offset = 0;
        ID3D11Buffer* vertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
        if (vertexBuffer == nullptr)
            return false;

        uint32 vertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
        uint32 stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
        if (vertexCount == 0 || stride == 0)
            return false;

        if (Cmd.Material)
        {
            Cmd.Material->Bind(Context->DeviceContext, Context->RenderBus,
                               &Cmd.PerObjectConstants, Shader, Context);
        }

		/**
		 * TODO: Material 내에서 B2 Slot 를 이미 쓰고 있는 것 같은데 이런 충돌을 밑 방식 이외의 방식으로 해결할 수 있는지 찾아볼 필요가 있음 
		 */
        {
            FToonOutlineConstantBuffer OutlineCB = {};
            OutlineCB.OutlineThickness = 0.003f; // 원하는 두께

            D3D11_MAPPED_SUBRESOURCE Mapped = {};
            if (SUCCEEDED(Context->DeviceContext->Map(
                    ToonOutlineConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
            {
                std::memcpy(Mapped.pData, &OutlineCB, sizeof(OutlineCB));
                Context->DeviceContext->Unmap(ToonOutlineConstantBuffer.Get(), 0);
            }
            ID3D11Buffer* OutlineCBs[] = { ToonOutlineConstantBuffer.Get() };
            Context->DeviceContext->VSSetConstantBuffers(2, 1, OutlineCBs); // b2 슬롯
        }

        CheckOverrideViewMode(Context);
        Context->DeviceContext->RSSetState(FrontCullRS.Get());
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

bool FToonOutlineRenderPass::End(const FRenderPassContext* Context)
{
    Context->DeviceContext->RSSetState(nullptr);

    // ★ b2 슬롯 해제
    ID3D11Buffer* NullCB = nullptr;
    Context->DeviceContext->VSSetConstantBuffers(2, 1, &NullCB);
    return true;
}

bool FToonOutlineRenderPass::Release()
{
    FrontCullRS.Reset();
    FrameConstantBuffer.Reset();
    ToonOutlineConstantBuffer.Reset(); // ★
    return true;
}

bool FToonOutlineRenderPass::EnsureConstantBuffer(ID3D11Device* Device)
{
    if (FrameConstantBuffer)
        return true;

    D3D11_BUFFER_DESC CBDesc = {};
    CBDesc.ByteWidth = sizeof(FFrameConstantBuffer);
    CBDesc.Usage = D3D11_USAGE_DYNAMIC;
    CBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    CBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(Device->CreateBuffer(&CBDesc, nullptr, FrameConstantBuffer.GetAddressOf())))
        return false;

    // ★ ToonOutlineConstantBuffer 생성
    CBDesc.ByteWidth = sizeof(FToonOutlineConstantBuffer);
    return SUCCEEDED(Device->CreateBuffer(&CBDesc, nullptr, ToonOutlineConstantBuffer.GetAddressOf()));
}

bool FToonOutlineRenderPass::EnsureRenderStates(ID3D11Device* Device)
{
    if (FrontCullRS)
        return true;

    D3D11_RASTERIZER_DESC RSDesc = {};
    RSDesc.FillMode = D3D11_FILL_SOLID;
    RSDesc.CullMode = D3D11_CULL_FRONT;
    RSDesc.FrontCounterClockwise = false;
    RSDesc.DepthClipEnable = true;
    return SUCCEEDED(Device->CreateRasterizerState(&RSDesc, FrontCullRS.GetAddressOf()));
}