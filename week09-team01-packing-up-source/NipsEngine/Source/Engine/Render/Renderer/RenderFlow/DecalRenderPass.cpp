#include "DecalRenderPass.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/Material.h"
#include "SceneLightBinding.h"

bool FDecalRenderPass::Initialize()
{
    return true;
}

bool FDecalRenderPass::Begin(const FRenderPassContext* Context)
{
    bSkipDecalDraw = false;

    const EViewMode ViewMode = Context->RenderBus ? Context->RenderBus->GetViewMode() : EViewMode::Lit;
    // view mode별 composite 우회 규칙은 공용 helper에서 관리한다.
    // 새 view mode가 decal을 건너뛰어야 하면 여기 if를 늘리지 말고 ShouldBypassSceneCompositePasses를 확장한다.
    if (ShouldBypassSceneCompositePasses(ViewMode))
    {
        OutSRV = PrevPassSRV ? PrevPassSRV : Context->RenderTargets->SceneColorSRV;
        OutRTV = PrevPassRTV ? PrevPassRTV : Context->RenderTargets->SceneColorRTV;
        bSkipDecalDraw = true;
        return true;
    }

    const FRenderTargetSet* RenderTargets = Context->RenderTargets;
    ID3D11RenderTargetView* RTVs[3] = {
        RenderTargets->SceneColorRTV,
        RenderTargets->SceneNormalRTV,
        RenderTargets->SceneWorldPosRTV
    };
    ID3D11DepthStencilView* DSV = RenderTargets->DepthStencilView;

    Context->DeviceContext->OMSetRenderTargets(ARRAYSIZE(RTVs), RTVs, DSV);
    OutSRV = RenderTargets->SceneColorSRV;
    OutRTV = RenderTargets->SceneColorRTV;

    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return true;
}

bool FDecalRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    if (bSkipDecalDraw)
    {
        return true;
    }

    const FRenderBus* RenderBus = Context->RenderBus;
    const TArray<FRenderCommand>& Commands = RenderBus->GetCommands(ERenderPass::Decal);

    if (Commands.empty())
    {
        return true;
    }
	
    SceneLightBinding::BindResources(
        Context,
        VisibleLightConstantBuffer,
		DirectionalShadowConstantBuffer,
        SpotShadowInfoConstantBuffer,
        SpotShadowConstantsBuffer,
        SpotShadowConstantsSRV,
        SpotShadowConstantsCapacity,
        PointShadowInfoConstantBuffer,
        PointShadowConstantsBuffer,
        PointShadowConstantsSRV,
        PointShadowConstantsCapacity);

    for (const FRenderCommand& Cmd : Commands)
    {
        if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
        {
            return false;
        }

        uint32 offset = 0;
        ID3D11Buffer* vertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
        if (vertexBuffer == nullptr)
        {
            return false;
        }

        uint32 vertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
        uint32 stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
        if (vertexCount == 0 || stride == 0)
        {
            return false;
        }

        if (Cmd.Material)
        {
            Cmd.Material->Bind(
                Context->DeviceContext,
                Context->RenderBus,
                &Cmd.PerObjectConstants,
                nullptr,
                Context,
                &Cmd.Constants.Decal);

			if (Cmd.MaskSRV)
			{
                Context->DeviceContext->PSSetShaderResources(19, 1, &Cmd.MaskSRV);
			}
        }

        SceneLightBinding::BindResources(
            Context,
            VisibleLightConstantBuffer,
			DirectionalShadowConstantBuffer,
            SpotShadowInfoConstantBuffer,
            SpotShadowConstantsBuffer,
            SpotShadowConstantsSRV,
            SpotShadowConstantsCapacity,
            PointShadowInfoConstantBuffer,
            PointShadowConstantsBuffer,
            PointShadowConstantsSRV,
            PointShadowConstantsCapacity);

        CheckOverrideViewMode(Context);

        Context->DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        ID3D11Buffer* indexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
        if (indexBuffer != nullptr)
        {
            uint32 indexStart = Cmd.SectionIndexStart;
            uint32 indexCount = Cmd.SectionIndexCount;
            Context->DeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            Context->DeviceContext->DrawIndexed(indexCount, indexStart, 0);
        }
        else
        {
            Context->DeviceContext->Draw(vertexCount, 0);
        }
    }

    return true;
}

bool FDecalRenderPass::End(const FRenderPassContext* Context)
{
    SceneLightBinding::UnbindResources(Context ? Context->DeviceContext : nullptr);
    return true;
}

bool FDecalRenderPass::Release()
{
    VisibleLightConstantBuffer.Reset();
    DirectionalShadowConstantBuffer.Reset();
    SpotShadowInfoConstantBuffer.Reset();
    SpotShadowConstantsBuffer.Reset();
    SpotShadowConstantsSRV.Reset();
    SpotShadowConstantsCapacity = 0;
    PointShadowInfoConstantBuffer.Reset();
    PointShadowConstantsBuffer.Reset();
    PointShadowConstantsSRV.Reset();
    PointShadowConstantsCapacity = 0;
    return true;
}
