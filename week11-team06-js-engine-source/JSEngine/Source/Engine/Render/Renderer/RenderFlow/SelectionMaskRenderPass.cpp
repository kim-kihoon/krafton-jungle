#include "SelectionMaskRenderPass.h"
#include "Core/ResourceManager.h"
#include "Core/Logging/Stats.h"
#include "Component/PrimitiveComponent.h"
#include "Render/Common/ShaderBindingSlots.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/ShaderPaths.h"
#include "Render/Resource/Texture.h"
#include "Render/Resource/VertexFactoryTypes.h"

static ID3D11ShaderResourceView* GetTextureSRVFromParam(const FMaterialParamValue& Param)
{
    if (Param.Type != EMaterialParamType::Texture || !std::holds_alternative<UTexture*>(Param.Value))
    {
        return nullptr;
    }

    UTexture* Texture = std::get<UTexture*>(Param.Value);
    return Texture ? Texture->GetSRV() : nullptr;
}

static ID3D11ShaderResourceView* GetDiffuseSRV(UMaterialInterface* Material)
{
    if (!Material || !Material->HasDiffuseMap())
    {
        return nullptr;
    }

    FMaterialParamValue Param;
    if (!Material->GetParam("DiffuseMap", Param))
    {
        return nullptr;
    }

    return GetTextureSRVFromParam(Param);
}

static uint32 GetSelectionMaskShaderKey(const FRenderCommand& Cmd)
{
    if (Cmd.VertexFactoryType == EVertexFactoryType::SkinCacheSkeletalMesh)
    {
        return 4;
    }

    if (Cmd.VertexFactoryType == EVertexFactoryType::SkeletalMesh)
    {
        return 3;
    }

    UPrimitiveComponent* Primitive = Cmd.SourcePrimitive;
    if (!Primitive)
    {
        return 0;
    }

    const EPrimitiveType PrimitiveType = Primitive->GetPrimitiveType();
    if (PrimitiveType == EPrimitiveType::EPT_StaticMesh)
    {
        return 1;
    }

    if (PrimitiveType == EPrimitiveType::EPT_Billboard || PrimitiveType == EPrimitiveType::EPT_SubUV)
    {
        return 2;
    }

    return 0;
}

static FShaderProgram* GetSelectionMaskProgram(const FRenderCommand& Cmd)
{
    const uint32 ShaderKey = GetSelectionMaskShaderKey(Cmd);

    const char* VSEntry = "VSPrimitive";
    const char* PSEntry = "PSPrimitive";
    const FVertexLayoutDesc* VertexLayout = &FVertexFactoryRegistry::Get(EVertexFactoryType::Primitive).SelectionLayout;
    if (ShaderKey == 1)
    {
        VSEntry = "VSStaticMesh";
        PSEntry = "PSTextured";
        VertexLayout = &FVertexFactoryRegistry::Get(EVertexFactoryType::StaticMesh).SelectionLayout;
    }
    else if (ShaderKey == 2)
    {
        VSEntry = "VSBillboard";
        PSEntry = "PSTextured";
        VertexLayout = &FVertexFactoryRegistry::Get(EVertexFactoryType::Billboard).SelectionLayout;
    }
    else if (ShaderKey == 3)
    {
        VSEntry = "VSSkeletalMesh";
        PSEntry = "PSTextured";
        VertexLayout = &FVertexFactoryRegistry::Get(EVertexFactoryType::SkeletalMesh).SelectionLayout;
    }
    else if (ShaderKey == 4)
    {
        VSEntry = "VSSkinCacheSkeletalMesh";
        PSEntry = "PSTextured";
        VertexLayout = &FVertexFactoryRegistry::Get(EVertexFactoryType::SkinCacheSkeletalMesh).SelectionLayout;
    }

    FShaderStageKey VSKey;
    VSKey.FilePath = FShaderPaths::EditorSelectionMask;
    VSKey.EntryPoint = VSEntry;
    VSKey.PermutationKey = ShaderKey;

    FShaderStageKey PSKey;
    PSKey.FilePath = FShaderPaths::EditorSelectionMask;
    PSKey.EntryPoint = PSEntry;
    PSKey.PermutationKey = ShaderKey;

    return FResourceManager::Get().GetOrCreateShaderProgram(
        VSKey,
        PSKey,
        nullptr,
        nullptr,
        VertexLayout);
}

static void BuildSelectionMaskConstants(
    const FRenderCommand& Cmd,
    FSelectionMaskConstants& OutConstants,
    ID3D11ShaderResourceView*& OutTextureSRV)
{
    OutConstants = {};
    OutConstants.AlphaCutoff = 0.01f;
    OutConstants.UVScale = FVector2(1.0f, 1.0f);
    OutTextureSRV = FResourceManager::Get().GetDefaultWhiteSRV();

    UPrimitiveComponent* Primitive = Cmd.SourcePrimitive;
    if (!Primitive)
    {
        return;
    }

    const EPrimitiveType PrimitiveType = Primitive->GetPrimitiveType();
    if (PrimitiveType == EPrimitiveType::EPT_StaticMesh)
    {
        ID3D11ShaderResourceView* DiffuseSRV = GetDiffuseSRV(Cmd.Material);
        if (DiffuseSRV)
        {
            OutTextureSRV = DiffuseSRV;
            OutConstants.bUseAlphaTest = 1u;
        }
    }
    else if (PrimitiveType == EPrimitiveType::EPT_Billboard)
    {
        UTexture* Texture = Cmd.Constants.Billboard.Texture;
        if (Texture && Texture->GetSRV())
        {
            OutTextureSRV = Texture->GetSRV();
            OutConstants.bUseAlphaTest = 1u;
        }
    }
    else if (PrimitiveType == EPrimitiveType::EPT_SubUV)
    {
        const FParticleResource* Particle = Cmd.Constants.SubUV.Particle;
        if (Particle && Particle->Texture && Particle->Texture->GetSRV())
        {
            OutTextureSRV = Particle->Texture->GetSRV();
            OutConstants.bUseAlphaTest = 1u;
        }

        if (Particle && Particle->Columns > 0 && Particle->Rows > 0)
        {
            const uint32 Columns = Particle->Columns;
            const uint32 Rows = Particle->Rows;
            const uint32 FrameIndex = Cmd.Constants.SubUV.FrameIndex;
            const uint32 Col = FrameIndex % Columns;
            const uint32 Row = FrameIndex / Columns;
            OutConstants.UVOffset = FVector2(
                static_cast<float>(Col) / static_cast<float>(Columns),
                static_cast<float>(Row) / static_cast<float>(Rows));
            OutConstants.UVScale = FVector2(
                1.0f / static_cast<float>(Columns),
                1.0f / static_cast<float>(Rows));
        }
    }
}

bool FSelectionMaskRenderPass::Initialize()
{
    return true;
}

bool FSelectionMaskRenderPass::Release()
{
    return true;
}

bool FSelectionMaskRenderPass::Begin(const FRenderPassContext* Context)
{
    ID3D11RenderTargetView* RTV = Context->RenderTargets->SelectionMaskRTV;
    ID3D11DepthStencilView* DSV = Context->RenderTargets->DepthStencilView;
    Context->DeviceContext->OMSetRenderTargets(1, &RTV, DSV);
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto DepthStencilState = FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::StencilWrite);
    auto BlendState = FResourceManager::Get().GetOrCreateBlendState(EBlendType::AlphaBlend);
    auto RasterizerState = FResourceManager::Get().GetOrCreateRasterizerState(ERasterizerType::SolidNoCull);
    Context->DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);
    Context->DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);
    Context->DeviceContext->RSSetState(RasterizerState);

    ID3D11SamplerState* Sampler = FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear);
    Context->DeviceContext->PSSetSamplers(0, 1, &Sampler);

    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;
    return true;
}

bool FSelectionMaskRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    SCOPE_STAT("GPU.SkeletalDraw.SelectionMask");
    const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::SelectionMask);
    if (Commands.empty())
    {
        return true;
    }

    for (const FRenderCommand& Cmd : Commands)
    {
        FShaderProgram* Program = GetSelectionMaskProgram(Cmd);
        if (!Program)
        {
            continue;
        }

        Program->Bind(Context->DeviceContext);
        BindVertexFactoryResources(Context->DeviceContext, Cmd.VertexFactoryType, Cmd);

        Context->RenderResources->PerObjectConstantBuffer.Update(Context->DeviceContext, &Cmd.PerObjectConstants, sizeof(FPerObjectConstants));
        ID3D11Buffer* cb1 = Context->RenderResources->PerObjectConstantBuffer.GetBuffer();
        Context->DeviceContext->VSSetConstantBuffers(ShaderBindingSlots::PerObjectCB, 1, &cb1);
        Context->DeviceContext->PSSetConstantBuffers(ShaderBindingSlots::PerObjectCB, 1, &cb1);

        FSelectionMaskConstants MaskConstants;
        ID3D11ShaderResourceView* TextureSRV = nullptr;
        BuildSelectionMaskConstants(Cmd, MaskConstants, TextureSRV);
        Context->RenderResources->SelectionMaskConstantBuffer.Update(
            Context->DeviceContext,
            &MaskConstants,
            sizeof(FSelectionMaskConstants));
        ID3D11Buffer* cb12 = Context->RenderResources->SelectionMaskConstantBuffer.GetBuffer();
        Context->DeviceContext->VSSetConstantBuffers(12, 1, &cb12);
        Context->DeviceContext->PSSetConstantBuffers(12, 1, &cb12);
        Context->DeviceContext->PSSetShaderResources(0, 1, &TextureSRV);

        FMeshDrawBinding DrawBinding;
        if (!BuildMeshDrawBinding(Cmd, DrawBinding))
        {
            continue;
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

bool FSelectionMaskRenderPass::End(const FRenderPassContext* Context)
{
    ID3D11ShaderResourceView* NullSRV = nullptr;
    Context->DeviceContext->PSSetShaderResources(0, 1, &NullSRV);
    return true;
}
