#include "ShadowPass.h"

#include "Core/ResourceManager.h"
#include "Core/Logger.h"

namespace
{
	float GetShadowMomentClearValue(uint32 ShadowFilterType)
	{
		static constexpr uint32 ShadowFilterTypeESM = 2u;
		static constexpr float ShadowESMExponentClearValue = 2.3538527e17f; // exp(40)
		return ShadowFilterType == ShadowFilterTypeESM ? ShadowESMExponentClearValue : 1.0f;
	}

	// AtlasRect(0~1 정규화 UV)를 실제 D3D viewport 픽셀 좌표로 바꿉니다.
	D3D11_VIEWPORT MakeViewportFromAtlasRect(const FVector4& AtlasRect, float AtlasResolution)
	{
		D3D11_VIEWPORT Viewport = {};
		Viewport.TopLeftX = AtlasRect.X * AtlasResolution;
		Viewport.TopLeftY = AtlasRect.Y * AtlasResolution;
		Viewport.Width    = AtlasRect.Z * AtlasResolution;
		Viewport.Height   = AtlasRect.W * AtlasResolution;
		Viewport.MinDepth = 0.0f;
		Viewport.MaxDepth = 1.0f;
		return Viewport;
	}
}

bool FShadowPass::Initialize()
{
	return true;
}

bool FShadowPass::Release()
{
	DirectionalShaderBinding.reset();
	ShaderBinding.reset();
    PointShaderBinding.reset();
	ShadowBackCullRasterizerState.Reset();
	ShadowNoCullRasterizerState.Reset();
	ShadowAtlasManager.Release();

	return true;
}

bool FShadowPass::Begin(const FRenderPassContext* Context)
{
	OutSRV = PrevPassSRV;
	OutRTV = PrevPassRTV;

	if (Context == nullptr)
	{
		return false;
	}

    if (Context->RenderTargets != nullptr)
    {
        Context->RenderTargets->DirectionalShadowSRV = nullptr;
        Context->RenderTargets->SpotShadowSRV = nullptr;
        Context->RenderTargets->SpotShadowCount = 0;
        Context->RenderTargets->PointShadowSRV = nullptr;
        Context->RenderTargets->PointShadowCount = 0;
        Context->RenderTargets->PointShadowVSMSRV = nullptr;
    }

	if (!EnsureDirectionalShadowResources(Context->Device, MAX_CASCADE_COUNT))
	{
		return false;
	}

	if (!EnsureSpotShadowResources(Context->Device))
    {
        return false;
    }
    
    if (!EnsurePointShadowResources(Context->Device))
    {
        return false;
    }

	return true;
}

bool FShadowPass::DrawCommand(const FRenderPassContext* Context)
{
	if (Context == nullptr || Context->RenderBus == nullptr || Context->DeviceContext == nullptr)
	{
		return false;
	}

	const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::ShadowCasters);

	// ─────────────────── Directional Shadow ───────────────────
	const FDirectionalShadowConstants* DirShadow = Context->RenderBus->GetDirectionalShadow();
	if (DirShadow != nullptr && DirectionalShaderBinding)
	{
		// 이전 프레임에 shader stage에서 directional shadow array를 읽고 있었을 수 있으니
		// depth target으로 다시 쓰기 전에 SRV 바인딩을 끊어준다.
		ID3D11ShaderResourceView* NullDirectionalShadowSRV = nullptr;
		Context->DeviceContext->PSSetShaderResources(13, 1, &NullDirectionalShadowSRV);
		Context->DeviceContext->VSSetShaderResources(13, 1, &NullDirectionalShadowSRV);

		Context->DeviceContext->PSSetShaderResources(16, 1, &NullDirectionalShadowSRV);
		Context->DeviceContext->VSSetShaderResources(16, 1, &NullDirectionalShadowSRV);

		ID3D11DepthStencilView* AtlasDSV = ShadowAtlasManager.GetDirectionalAtlasDSV();
		ID3D11RenderTargetView* AtlasRTV = ShadowAtlasManager.GetDirectionalVSMAtlasRTV();

		if (AtlasDSV == nullptr)
		{
			return false;
		}
		Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D11DepthStencilState* DSState =
			FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::Default, Context->Device);
		Context->DeviceContext->OMSetDepthStencilState(DSState, 0);
		ID3D11RasterizerState* DirectionalRS = GetOrCreateShadowRasterizerState(
			Context->Device,
			DirShadow->ShadowMode == DirectionalShadowModeValue::PSM);
		if (DirectionalRS == nullptr)
		{
			return false;
		}
		Context->DeviceContext->RSSetState(DirectionalRS);
		Context->DeviceContext->OMSetRenderTargets(1, &AtlasRTV, AtlasDSV);
		Context->DeviceContext->ClearDepthStencilView(AtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

		const float ShadowMomentClearValue =
			GetShadowMomentClearValue(static_cast<uint32>(Context->RenderBus->GetShadowFilterType()));
		float ClearColor[4] = { ShadowMomentClearValue, ShadowMomentClearValue, 1.0f, 1.0f };
		Context->DeviceContext->ClearRenderTargetView(AtlasRTV, ClearColor);

		const TArray<FDirectionalAtlasSlotDesc>& CascadeSlots = FShadowAtlasManager::GetDirectionalCascadeSlots();
		const uint32 CascadeCount = static_cast<uint32>(CascadeSlots.size());
		const uint32 DirectionalPassCount =
			(DirShadow->ShadowMode == DirectionalShadowModeValue::PSM && CascadeCount > 0u) ? 1u : CascadeCount;
		
		for (uint32 CascadeIndex = 0; CascadeIndex < DirectionalPassCount; ++CascadeIndex)
		{
			const FDirectionalAtlasSlotDesc& Slot = CascadeSlots[CascadeIndex];
			const D3D11_VIEWPORT DirShadowViewport = 
				MakeViewportFromAtlasRect(Slot.AtlasRect, static_cast<float>(FShadowAtlasManager::DirectionalAtlasResolution ));
			Context->DeviceContext->RSSetViewports(1, &DirShadowViewport);

			DirectionalShaderBinding->SetMatrix4("LightViewProj", DirShadow->LightViewProj[CascadeIndex]);
            DirectionalShaderBinding->SetUInt("ShadowFilterType", static_cast<uint32>(Context->RenderBus->GetShadowFilterType()));

			for (const FRenderCommand& Cmd : Commands)
			{
				if (Cmd.Type == ERenderCommandType::PostProcessOutline)
				{
					continue;
				}

				if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
				{
					continue;
				}

				ID3D11Buffer* VertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
				if (VertexBuffer == nullptr)
				{
					continue;
				}

				const uint32 VertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
				const uint32 Stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
				if (VertexCount == 0 || Stride == 0)
				{
					continue;
				}
			
				uint32 Offset = 0;
				DirectionalShaderBinding->SetMatrix4("World", Cmd.PerObjectConstants.Model);
				DirectionalShaderBinding->Bind(Context->DeviceContext);

				Context->DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

				ID3D11Buffer* IndexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
				if (IndexBuffer != nullptr)
				{
					Context->DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
					Context->DeviceContext->DrawIndexed(Cmd.SectionIndexCount, Cmd.SectionIndexStart, 0);
				}
				else
				{
					Context->DeviceContext->Draw(VertexCount, 0);
				}
			}
		}

		if (Context->RenderTargets != nullptr)
		{
			Context->RenderTargets->DirectionalShadowSRV = ShadowAtlasManager.GetDirectionalAtlasSRV();

			Context->RenderTargets->DirectionalShadowVSMSRV = ShadowAtlasManager.GetDirectionalVSMAtlasSRV();
		}
	}

    // ─────────────────── Spot Shadow ───────────────────
    const TArray<FSpotShadowConstants>& SpotShadows = Context->RenderBus->GetCastShadowSpotLights();
    if (ShaderBinding && !SpotShadows.empty() && !Commands.empty())
    {

    // 이전 프레임에 픽셀 셰이더에서 이 shadow atlas를 읽고 있었을 수 있으니,
    // depth를 다시 쓰기 전에 SRV 바인딩 끊어주기
    ID3D11ShaderResourceView* NullShadowSRV = nullptr;
    Context->DeviceContext->PSSetShaderResources(12, 1, &NullShadowSRV);
    Context->DeviceContext->PSSetShaderResources(15, 1, &NullShadowSRV);

    // spot shadow atlas를 depth 타겟으로 바라보는 핸들
    ID3D11DepthStencilView* AtlasDSV = ShadowAtlasManager.GetSpotAtlasDSV();
    ID3D11RenderTargetView* AtlasRTV = ShadowAtlasManager.GetSpotVSMAtlasRTV();

    if (AtlasDSV == nullptr)
    {
        return false;
    }
    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    ID3D11DepthStencilState* DepthStencilState =
        FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::Default, Context->Device);
    Context->DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);
    ID3D11RasterizerState* ShadowRS = GetOrCreateShadowRasterizerState(Context->Device, false);
    if (ShadowRS == nullptr)
    {
        return false;
    }
    Context->DeviceContext->RSSetState(ShadowRS);
    Context->DeviceContext->OMSetRenderTargets(1, &AtlasRTV, AtlasDSV);
    
    // 매 프레임 atlas 전체를 초기화하고, 이번 프레임의 visible spot shadow들을 다시 채우기
    Context->DeviceContext->ClearDepthStencilView(AtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

    const float ShadowMomentClearValue =
        GetShadowMomentClearValue(static_cast<uint32>(Context->RenderBus->GetShadowFilterType()));
    float ClearColor[4] = { ShadowMomentClearValue, ShadowMomentClearValue, 1.0f, 1.0f };
    Context->DeviceContext->ClearRenderTargetView(AtlasRTV, ClearColor);

    // 실제로 atlas에 그린 spot shadow 개수를 기록
    uint32 RenderedSpotShadowCount = 0;

    for (const FSpotShadowConstants& SpotShadow : SpotShadows)
    {
        const D3D11_VIEWPORT ShadowViewport =
            MakeViewportFromAtlasRect(SpotShadow.AtlasRect, static_cast<float>(FShadowAtlasManager::SpotAtlasResolution));
        Context->DeviceContext->RSSetViewports(1, &ShadowViewport);

		ShaderBinding->SetMatrix4("LightViewProj", SpotShadow.LightViewProj);
		ShaderBinding->SetFloat("ShadowBias", SpotShadow.ShadowBias);
		ShaderBinding->SetFloat("ShadowFarPlane", SpotShadow.ShadowFarPlane);
        ShaderBinding->SetUInt("ShadowFilterType", static_cast<uint32>(Context->RenderBus->GetShadowFilterType()));
		
		for (const FRenderCommand& Cmd : Commands)
		{
			if (Cmd.Type == ERenderCommandType::PostProcessOutline)
			{
				continue;
			}

            if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
            {
                continue;
            }

            ID3D11Buffer* VertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
            if (VertexBuffer == nullptr)
            {
                continue;
            }

            const uint32 VertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
            const uint32 Stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
            if (VertexCount == 0 || Stride == 0)
            {
                continue;
            }

            uint32 Offset = 0;
            ShaderBinding->SetMatrix4("World", Cmd.PerObjectConstants.Model);
            ShaderBinding->Bind(Context->DeviceContext);

            Context->DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

            ID3D11Buffer* IndexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
            if (IndexBuffer != nullptr)
            {
                Context->DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
                Context->DeviceContext->DrawIndexed(Cmd.SectionIndexCount, Cmd.SectionIndexStart, 0);
            }
            else
            {
                Context->DeviceContext->Draw(VertexCount, 0);
            }
        }

        ++RenderedSpotShadowCount;
    }
    
    if (Context->RenderTargets != nullptr)
    {
        Context->RenderTargets->SpotShadowSRV = ShadowAtlasManager.GetSpotAtlasSRV();
        Context->RenderTargets->SpotShadowCount = RenderedSpotShadowCount;

		Context->RenderTargets->SpotShadowVSMSRV = ShadowAtlasManager.GetSpotVSMAtlasSRV();
    }

    }

    // ─────────────────── Point Shadow ───────────────────
    if (!PointShaderBinding)
    {
        return true;
    }
    
    const TArray<FPointShadowConstants>& PointShadows = Context->RenderBus->GetCastShadowPointLights();
    if (PointShadows.empty() || Commands.empty())
    {
        return true;
    }
    ID3D11ShaderResourceView* NullPointShadowSRV = nullptr;
    Context->DeviceContext->PSSetShaderResources(17, 1, &NullPointShadowSRV);
    Context->DeviceContext->PSSetShaderResources(18, 1, &NullPointShadowSRV);
    
    ID3D11DepthStencilView* PointAtlasDSV = ShadowAtlasManager.GetPointAtlasDSV();
    ID3D11RenderTargetView* PointAtlasRTV = ShadowAtlasManager.GetPointVSMAtlasRTV();
    if (PointAtlasDSV == nullptr || PointAtlasRTV == nullptr)
    {
        return false;
    }

    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    ID3D11DepthStencilState* PointDepthStencilState =
                FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::Default, Context->Device);
    Context->DeviceContext->OMSetDepthStencilState(PointDepthStencilState, 0);

    ID3D11RasterizerState* PointShadowRasterizerState =
        FResourceManager::Get().GetOrCreateRasterizerState(ERasterizerType::SolidNoCull, Context->Device);
    Context->DeviceContext->RSSetState(PointShadowRasterizerState);

    Context->DeviceContext->OMSetRenderTargets(1, &PointAtlasRTV, PointAtlasDSV);
    Context->DeviceContext->ClearDepthStencilView(PointAtlasDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

    const float PointShadowMomentClearValue =
        GetShadowMomentClearValue(static_cast<uint32>(Context->RenderBus->GetShadowFilterType()));
    float PointClearColor[4] = { PointShadowMomentClearValue, PointShadowMomentClearValue, 1.0f, 1.0f };
    Context->DeviceContext->ClearRenderTargetView(PointAtlasRTV, PointClearColor);
    
    uint32 RenderedPointShadowCount = 0;
    for (const FPointShadowConstants& PointShadow : PointShadows)
    {
        for (uint32 Face = 0; Face < 6; ++Face)
        {
            const D3D11_VIEWPORT FaceViewport = MakeViewportFromAtlasRect(PointShadow.FaceAtlasRects[Face], static_cast<float>(FShadowAtlasManager::PointAtlasResolution));    
            Context->DeviceContext->RSSetViewports(1, &FaceViewport);
            
            PointShaderBinding->SetMatrix4("LightViewProj", PointShadow.LightViewProj[Face]);
            PointShaderBinding->SetVector3("LightPosition", PointShadow.LightPosition);
            PointShaderBinding->SetFloat("FarPlane", PointShadow.FarPlane);
            PointShaderBinding->SetFloat("ShadowBias", PointShadow.ShadowBias);
            PointShaderBinding->SetFloat("ShadowResolution", PointShadow.ShadowResolution);
            PointShaderBinding->SetUInt("ShadowFilterType", static_cast<uint32>(Context->RenderBus->GetShadowFilterType()));
            
            for (const FRenderCommand& Cmd : Commands)
            {
                if (Cmd.Type == ERenderCommandType::PostProcessOutline)
                {
                    continue;
                }

                if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
                {
                    continue;
                }

                ID3D11Buffer* VertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
                if (VertexBuffer == nullptr)
                {
                    continue;
                }

                const uint32 VertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
                const uint32 Stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
                if (VertexCount == 0 || Stride == 0)
                {
                    continue;
                }

                uint32 Offset = 0;

                PointShaderBinding->SetMatrix4("World", Cmd.PerObjectConstants.Model);
                PointShaderBinding->Bind(Context->DeviceContext);

                Context->DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

                ID3D11Buffer* IndexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
                if (IndexBuffer != nullptr)
                {
                    Context->DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
                    Context->DeviceContext->DrawIndexed(Cmd.SectionIndexCount, Cmd.SectionIndexStart, 0);
                }
                else
                {
                    Context->DeviceContext->Draw(VertexCount, 0);
                }
            }
        }

        ++RenderedPointShadowCount;
    }

    if (Context->RenderTargets != nullptr)
    {
        Context->RenderTargets->PointShadowSRV = ShadowAtlasManager.GetPointAtlasSRV();
        Context->RenderTargets->PointShadowCount = RenderedPointShadowCount;
        Context->RenderTargets->PointShadowVSMSRV = ShadowAtlasManager.GetPointVSMAtlasSRV();
    }
    
    return true;
}

bool FShadowPass::End(const FRenderPassContext* Context)
{
	if (Context != nullptr && Context->DeviceContext != nullptr)
	{
		Context->DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

		if (Context->RenderTargets != nullptr)
		{
			D3D11_VIEWPORT Viewport = {};
			Viewport.TopLeftX = 0.0f;
			Viewport.TopLeftY = 0.0f;
			Viewport.Width = Context->RenderTargets->Width;
			Viewport.Height = Context->RenderTargets->Height;
			Viewport.MinDepth = 0.0f;
			Viewport.MaxDepth = 1.0f;
			Context->DeviceContext->RSSetViewports(1, &Viewport);
		}
	}

	return true;
}

bool FShadowPass::EnsureDirectionalShadowResources(ID3D11Device* Device, uint32 CascadeCount)
{
	if (Device == nullptr || CascadeCount == 0)
	{
		return false;
	}

	if (!ShadowAtlasManager.InitializeDirectionalAtlas(Device))
	{
		UE_LOG("Failed to initialize directional shadow atlas");
		return false;
	}

    if (!DirectionalShaderBinding)
    {
        UShader* DirectionalShadowShader = FResourceManager::Get().GetShader("Shaders/Multipass/DirectionalShadowDepth.hlsl");
        if (DirectionalShadowShader == nullptr)
        {
            bool bLoaded = FResourceManager::Get().LoadShader(
                "Shaders/Multipass/DirectionalShadowDepth.hlsl",
                "mainVS",
                "mainPS",
                nullptr);
            if (!bLoaded)
            {
                UE_LOG("Failed to load directional shadow depth shader");
                return false;
            }

            DirectionalShadowShader = FResourceManager::Get().GetShader("Shaders/Multipass/DirectionalShadowDepth.hlsl");
            if (DirectionalShadowShader == nullptr)
            {
                UE_LOG("Failed to find directional shadow depth shader");
                return false;
            }
        }

        DirectionalShaderBinding  = DirectionalShadowShader->CreateBindingInstance(Device);
        if (!DirectionalShaderBinding )
        {
            UE_LOG("Failed to create directional shadow shader binding");
            return false;
        }
    }
	
	return true;
}

bool FShadowPass::EnsureSpotShadowResources(ID3D11Device* Device)
{
	if (Device == nullptr)
	{
		return false;
	}

	if (!ShadowAtlasManager.Initialize(Device))
	{
		UE_LOG("Failed to initialize spot shadow atlas manager");
		return false;
	}

    if (!ShaderBinding)
    {
        UShader* SoptShadowShader = FResourceManager::Get().GetShader("Shaders/Multipass/SpotShadowDepth.hlsl");
        if (SoptShadowShader == nullptr)
        {
            bool bLoaded = FResourceManager::Get().LoadShader(
                "Shaders/Multipass/SpotShadowDepth.hlsl",
                "mainVS",
                "mainPS",
                nullptr);
            if (!bLoaded)
            {
                UE_LOG("Failed to load spot shadow depth shader");
                return false;
            }

            SoptShadowShader = FResourceManager::Get().GetShader("Shaders/Multipass/SpotShadowDepth.hlsl");
            if (SoptShadowShader == nullptr)
            {
                UE_LOG("Failed to find spot shadow depth shader");
                return false;
            }
        }

		ShaderBinding = SoptShadowShader->CreateBindingInstance(Device);
		if (!ShaderBinding)
		{
			UE_LOG("Failed to create directional shadow shader binding");
			return false;
		}
	}

	return true;
}

ID3D11RasterizerState* FShadowPass::GetOrCreateShadowRasterizerState(ID3D11Device* Device, bool bNoCull)
{
	if (Device == nullptr)
	{
		return nullptr;
	}

	TComPtr<ID3D11RasterizerState>* State =
		bNoCull ? &ShadowNoCullRasterizerState : &ShadowBackCullRasterizerState;
	if (State->Get() != nullptr)
	{
		return State->Get();
	}

	D3D11_RASTERIZER_DESC Desc = {};
	Desc.FillMode = D3D11_FILL_SOLID;
	Desc.CullMode = bNoCull ? D3D11_CULL_NONE : D3D11_CULL_BACK;
	Desc.DepthClipEnable = TRUE;

	if (FAILED(Device->CreateRasterizerState(&Desc, State->GetAddressOf())))
	{
		UE_LOG("Failed to create shadow rasterizer state");
		return nullptr;
	}

	return State->Get();
}

bool FShadowPass::EnsurePointShadowResources(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }

    if (!ShadowAtlasManager.InitializePointAtlas(Device))
    {
        UE_LOG("Failed to initialize point shadow atlas");
        return false;
    }

    if (!PointShaderBinding)
    {
        UShader* PointShadowShader = FResourceManager::Get().GetShader("Shaders/Multipass/PointShadowDepth.hlsl");
        if (PointShadowShader == nullptr)
        {
            bool bLoaded = FResourceManager::Get().LoadShader(
                "Shaders/Multipass/PointShadowDepth.hlsl",
                "mainVS",
                "mainPS",
                nullptr);
            if (!bLoaded)
            {
                UE_LOG("Failed to load point shadow depth shader");
                return false;
            }

            PointShadowShader = FResourceManager::Get().GetShader("Shaders/Multipass/PointShadowDepth.hlsl");
            if (PointShadowShader == nullptr)
            {
                UE_LOG("Failed to find point shadow depth shader");
                return false;
            }
        }

        PointShaderBinding  = PointShadowShader->CreateBindingInstance(Device);
        if (!PointShaderBinding )
        {
            UE_LOG("Failed to create point shadow shader binding");
            return false;
        }
    }
    
    return true;
}
