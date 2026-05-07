#include "BlurPass.h"

#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "ShadowPass.h"
#include "ShadowAtlasManager.h"

bool FBlurPass::Initialize()
{
    FShadowAtlasManager AtlasManager;
    SpotShadowResolution = AtlasManager.SpotAtlasResolution;
    DirectionalShadowResolution = AtlasManager.DirectionalAtlasResolution;
    PointShadowResolution = AtlasManager.PointAtlasResolution;

    return true;
}

bool FBlurPass::Release()
{
    ShadowVSMInputSRV.Reset();

    ShadowBlurTempTexture.Reset();
    ShadowBlurTempSRV.Reset();
    ShadowBlurTempUAV.Reset();

	ShadowBlurFinalTexture.Reset();
    ShadowBlurFinalSRV.Reset();
    ShadowBlurFinalUAV.Reset();

	DirectionalShadowBlurTempTexture.Reset();
    DirectionalShadowBlurTempSRV.Reset();
    DirectionalShadowBlurTempUAV.Reset();

    DirectionalShadowBlurFinalTexture.Reset();
    DirectionalShadowBlurFinalSRV.Reset();
    DirectionalShadowBlurFinalUAV.Reset();

    PointShadowBlurTempTexture.Reset();
    PointShadowBlurTempSRV.Reset();
    PointShadowBlurTempUAV.Reset();

    PointShadowBlurFinalTexture.Reset();
    PointShadowBlurFinalSRV.Reset();
    PointShadowBlurFinalUAV.Reset();

	ComputeShader.Reset();
    ConstantBuffer.Reset();

    return true;
}

bool FBlurPass::Begin(const FRenderPassContext* Context)
{
	 OutSRV = PrevPassSRV;
	 OutRTV = PrevPassRTV;

	 if (Context == nullptr)
	 {
		 return false;
	 }

     if (!EnsureComputeShader(Context->Device))
     {
         return false;
     }
     if (!EnsureConstantBuffer(Context->Device))
     {
         return false;
     }
     if (!EnsureSpotShadowBlurResources(Context->Device))
	 {
		 return false;
     }
     if (!EnsureDirectionalShadowBlurResources(Context->Device))
     {
         return false;
     }
     if (!EnsurePointShadowBlurResources(Context->Device))
     {
         return false;
     }

    return true;
}

bool FBlurPass::DrawCommand(const FRenderPassContext* Context)
{
    if (!ComputeShader || !ConstantBuffer)
        return false;

	if (Context->RenderTargets == nullptr)
        return true;

	// Spot
	ShadowVSMInputSRV = Context->RenderTargets->SpotShadowVSMSRV;
    DrawBlurCommand(Context, 0, 0, SpotShadowResolution, 
		ShadowBlurTempSRV.Get(), ShadowBlurTempUAV.Get(), 
		ShadowBlurFinalSRV.Get(), ShadowBlurFinalUAV.Get());
    Context->RenderTargets->SpotShadowVSMSRV = ShadowBlurFinalSRV.Get();

	// Directional
	ShadowVSMInputSRV = Context->RenderTargets->DirectionalShadowVSMSRV;
    DrawBlurCommand(Context, 0, 0, DirectionalShadowResolution,
                    DirectionalShadowBlurTempSRV.Get(), DirectionalShadowBlurTempUAV.Get(),
                    DirectionalShadowBlurFinalSRV.Get(), DirectionalShadowBlurFinalUAV.Get());
    Context->RenderTargets->DirectionalShadowVSMSRV = DirectionalShadowBlurFinalSRV.Get();

    // Point: face tile별로 분리 dispatch — face 경계 너머로 blur가 새지 않도록 함
    ShadowVSMInputSRV = Context->RenderTargets->PointShadowVSMSRV;
    {
        const auto& PointSlots = FShadowAtlasManager::GetActivePointSlots();
        const float AtlasSizeF = static_cast<float>(PointShadowResolution);
        for (const FPointAtlasSlotDesc& Slot : PointSlots)
        {
            for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
            {
                const FVector4& Rect = Slot.FaceAtlasRects[FaceIndex];
                const uint32 TileBaseX = static_cast<uint32>(Rect.X * AtlasSizeF + 0.5f);
                const uint32 TileBaseY = static_cast<uint32>(Rect.Y * AtlasSizeF + 0.5f);
                const uint32 TileSize = Slot.TileResolution;
                if (TileSize == 0)
                    continue;
                DrawBlurCommand(Context, TileBaseX, TileBaseY, TileSize,
                                PointShadowBlurTempSRV.Get(), PointShadowBlurTempUAV.Get(),
                                PointShadowBlurFinalSRV.Get(), PointShadowBlurFinalUAV.Get());
            }
        }
    }
    Context->RenderTargets->PointShadowVSMSRV = PointShadowBlurFinalSRV.Get();

    return true;
}

void FBlurPass::DrawBlurCommand(const FRenderPassContext* Context, 
    uint32 TileBaseX, uint32 TileBaseY, uint32 TileSize,
	ID3D11ShaderResourceView* ShadowBlurTempSRV, 
	ID3D11UnorderedAccessView* ShadowBlurTempUAV, 
	ID3D11ShaderResourceView* ShadowBlurFinalSRV, 
	ID3D11UnorderedAccessView* ShadowBlurFinalUAV)
{
    ID3D11DeviceContext* DC = Context->DeviceContext;

    const uint32 GroupX = (TileSize + 7) / 8;
    const uint32 GroupY = (TileSize + 7) / 8;

    ID3D11ShaderResourceView* NullSRV = nullptr;
    ID3D11UnorderedAccessView* NullUAV = nullptr;
    ID3D11Buffer* NullCB = nullptr;

    DC->CSSetShader(ComputeShader.Get(), nullptr, 0);

    // ----------------------------------------------------------------
    // Pass 1 : Horizontal Blur
    //   Input  : ShadowVSMInputSRV  (t14)
    //   Output : ShadowBlurTempUAV  (u0)
    // ----------------------------------------------------------------
    UpdateConstantBuffer(DC, 0, TileBaseX, TileBaseY, TileSize);

    ID3D11Buffer* CB = ConstantBuffer.Get();
    ID3D11ShaderResourceView* InSRV = ShadowVSMInputSRV.Get();
    ID3D11UnorderedAccessView* OutUAV = ShadowBlurTempUAV;

    DC->CSSetConstantBuffers(10, 1, &CB);
    DC->CSSetShaderResources(14, 1, &InSRV);
    DC->CSSetUnorderedAccessViews(0, 1, &OutUAV, nullptr);

    DC->Dispatch(GroupX, GroupY, 1);

    DC->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
    DC->CSSetShaderResources(14, 1, &NullSRV);

    // ----------------------------------------------------------------
    // Pass 2 : Vertical Blur
    //   Input  : ShadowBlurTempSRV   (t14)
    //   Output : ShadowBlurFinalUAV  (u0)
    // ----------------------------------------------------------------
    UpdateConstantBuffer(DC, 1, TileBaseX, TileBaseY, TileSize);

    ID3D11ShaderResourceView* TempSRV = ShadowBlurTempSRV;
    ID3D11UnorderedAccessView* FinalUAV = ShadowBlurFinalUAV;

    DC->CSSetConstantBuffers(10, 1, &CB);
    DC->CSSetShaderResources(14, 1, &TempSRV);
    DC->CSSetUnorderedAccessViews(0, 1, &FinalUAV, nullptr);

    DC->Dispatch(GroupX, GroupY, 1);

    // 언바인딩
    DC->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
    DC->CSSetShaderResources(14, 1, &NullSRV);
    DC->CSSetConstantBuffers(10, 1, &NullCB);
    DC->CSSetShader(nullptr, nullptr, 0);
}

bool FBlurPass::End(const FRenderPassContext* Context)
{
    return true;
}

void FBlurPass::UpdateConstantBuffer(ID3D11DeviceContext* DeviceContext, uint32 BlurDirection,
    uint32 TileBaseX, uint32 TileBaseY, uint32 TileSize)
{
    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(DeviceContext->Map(ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
        return;

    FShadowBlurConstants* CB = static_cast<FShadowBlurConstants*>(Mapped.pData);
    CB->BlurDirection = BlurDirection;
    CB->TileBaseX = TileBaseX;
    CB->TileBaseY = TileBaseY;
    CB->TileSize = TileSize;

    DeviceContext->Unmap(ConstantBuffer.Get(), 0);
}

bool FBlurPass::EnsureComputeShader(ID3D11Device* Device)
{
    if (ComputeShader)
    {
        return true;
    }

    TComPtr<ID3DBlob> CSBlob;
    TComPtr<ID3DBlob> ErrorBlob;
    const HRESULT CompileResult = D3DCompileFromFile(
        FPaths::ToWide("Shaders/Multipass/ShadowBlurCS.hlsl").c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "mainCS",
        "cs_5_0",
        0,
        0,
        CSBlob.GetAddressOf(),
        ErrorBlob.GetAddressOf());

    if (FAILED(CompileResult))
    {
        if (ErrorBlob)
        {
            UE_LOG("ShadowBlurPass CS Compile Error: %s", static_cast<const char*>(ErrorBlob->GetBufferPointer()));
        }
        else
        {
            UE_LOG("Failed to compile ShadowBlurPass.hlsl");
        }
        return false;
    }

    const HRESULT CreateResult = Device->CreateComputeShader(
		CSBlob->GetBufferPointer(), CSBlob->GetBufferSize(), nullptr, ComputeShader.GetAddressOf());

    if (FAILED(CreateResult))
    {
        UE_LOG("Failed to create ShadowBlurPass compute shader");
        return false;
    }

	return true;
}

bool FBlurPass::EnsureConstantBuffer(ID3D11Device* Device)
{
    if (ConstantBuffer)
    {
        return true;
    }

    D3D11_BUFFER_DESC CBDesc = {};
    CBDesc.ByteWidth = sizeof(FShadowBlurConstants);
    CBDesc.Usage = D3D11_USAGE_DYNAMIC;
    CBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    CBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    return SUCCEEDED(Device->CreateBuffer(&CBDesc, nullptr, ConstantBuffer.GetAddressOf()));
}

bool FBlurPass::EnsureSpotShadowBlurResources(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }

	if (ShadowBlurTempTexture && ShadowBlurTempSRV && ShadowBlurTempUAV)
    {
        return true;
    }

    if (ShadowBlurFinalTexture && ShadowBlurFinalSRV && ShadowBlurFinalUAV)
    {
        return true;
    }

	D3D11_TEXTURE2D_DESC TexDesc = {};
    TexDesc.Width = SpotShadowResolution;
    TexDesc.Height = SpotShadowResolution;
    TexDesc.MipLevels = 1;
    TexDesc.ArraySize = 1;
    TexDesc.Format = DXGI_FORMAT_R32G32_FLOAT; // R=depth, G=depth²
    TexDesc.SampleDesc.Count = 1;
    TexDesc.SampleDesc.Quality = 0;
    TexDesc.Usage = D3D11_USAGE_DEFAULT;
    TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    TexDesc.CPUAccessFlags = 0;
    TexDesc.MiscFlags = 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MostDetailedMip = 0;
    SRVDesc.Texture2D.MipLevels = 1;

	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    UAVDesc.Texture2D.MipSlice = 0;

	TComPtr<ID3D11Texture2D> NewBlurTempTexture;
    if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, NewBlurTempTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur texture array");
        return false;
    }

    TComPtr<ID3D11ShaderResourceView> NewBlurTempSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBlurTempTexture.Get(), &SRVDesc, NewBlurTempSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur shader resource view");
        return false;
    }

	TComPtr<ID3D11UnorderedAccessView> NewBlurTempUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewBlurTempTexture.Get(), &UAVDesc, NewBlurTempUAV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow unordered access view");
        return false;
    }

	ShadowBlurTempTexture = std::move(NewBlurTempTexture);
    ShadowBlurTempSRV = std::move(NewBlurTempSRV);
    ShadowBlurTempUAV = std::move(NewBlurTempUAV);

	TComPtr<ID3D11Texture2D> NewBlurFinalTexture;
    if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, NewBlurFinalTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur texture array");
        return false;
    }

    TComPtr<ID3D11ShaderResourceView> NewBlurFinalSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBlurFinalTexture.Get(), &SRVDesc, NewBlurFinalSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur shader resource view");
        return false;
    }

	TComPtr<ID3D11UnorderedAccessView> NewBlurFinalUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewBlurFinalTexture.Get(), &UAVDesc, NewBlurFinalUAV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow unordered access view");
        return false;
    }

	ShadowBlurFinalTexture = std::move(NewBlurFinalTexture);
    ShadowBlurFinalSRV = std::move(NewBlurFinalSRV);
    ShadowBlurFinalUAV = std::move(NewBlurFinalUAV);

	return true;
}

bool FBlurPass::EnsureDirectionalShadowBlurResources(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }

    if (DirectionalShadowBlurTempTexture && DirectionalShadowBlurTempSRV && DirectionalShadowBlurTempUAV)
    {
        return true;
    }

    if (DirectionalShadowBlurFinalTexture && DirectionalShadowBlurFinalSRV && DirectionalShadowBlurFinalUAV)
    {
        return true;
    }

    D3D11_TEXTURE2D_DESC TexDesc = {};
    TexDesc.Width = DirectionalShadowResolution;
    TexDesc.Height = DirectionalShadowResolution;
    TexDesc.MipLevels = 1;
    TexDesc.ArraySize = 1;
    TexDesc.Format = DXGI_FORMAT_R32G32_FLOAT; // R=depth, G=depth²
    TexDesc.SampleDesc.Count = 1;
    TexDesc.SampleDesc.Quality = 0;
    TexDesc.Usage = D3D11_USAGE_DEFAULT;
    TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    TexDesc.CPUAccessFlags = 0;
    TexDesc.MiscFlags = 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MostDetailedMip = 0;
    SRVDesc.Texture2D.MipLevels = 1;

    D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    UAVDesc.Texture2D.MipSlice = 0;

    TComPtr<ID3D11Texture2D> NewBlurTempTexture;
    if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, NewBlurTempTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur texture array");
        return false;
    }

    TComPtr<ID3D11ShaderResourceView> NewBlurTempSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBlurTempTexture.Get(), &SRVDesc, NewBlurTempSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur shader resource view");
        return false;
    }

    TComPtr<ID3D11UnorderedAccessView> NewBlurTempUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewBlurTempTexture.Get(), &UAVDesc, NewBlurTempUAV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow unordered access view");
        return false;
    }

    DirectionalShadowBlurTempTexture = std::move(NewBlurTempTexture);
    DirectionalShadowBlurTempSRV = std::move(NewBlurTempSRV);
    DirectionalShadowBlurTempUAV = std::move(NewBlurTempUAV);

    TComPtr<ID3D11Texture2D> NewBlurFinalTexture;
    if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, NewBlurFinalTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur texture array");
        return false;
    }

    TComPtr<ID3D11ShaderResourceView> NewBlurFinalSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBlurFinalTexture.Get(), &SRVDesc, NewBlurFinalSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow blur shader resource view");
        return false;
    }

    TComPtr<ID3D11UnorderedAccessView> NewBlurFinalUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewBlurFinalTexture.Get(), &UAVDesc, NewBlurFinalUAV.GetAddressOf())))
    {
        UE_LOG("Failed to create spot shadow unordered access view");
        return false;
    }

    DirectionalShadowBlurFinalTexture = std::move(NewBlurFinalTexture);
    DirectionalShadowBlurFinalSRV = std::move(NewBlurFinalSRV);
    DirectionalShadowBlurFinalUAV = std::move(NewBlurFinalUAV);

    return true;
}

bool FBlurPass::EnsurePointShadowBlurResources(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }

    if (PointShadowBlurTempTexture && PointShadowBlurTempSRV && PointShadowBlurTempUAV)
    {
        return true;
    }

    if (PointShadowBlurFinalTexture && PointShadowBlurFinalSRV && PointShadowBlurFinalUAV)
    {
        return true;
    }

    D3D11_TEXTURE2D_DESC TexDesc = {};
    TexDesc.Width = PointShadowResolution;
    TexDesc.Height = PointShadowResolution;
    TexDesc.MipLevels = 1;
    TexDesc.ArraySize = 1;
    TexDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    TexDesc.SampleDesc.Count = 1;
    TexDesc.SampleDesc.Quality = 0;
    TexDesc.Usage = D3D11_USAGE_DEFAULT;
    TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MostDetailedMip = 0;
    SRVDesc.Texture2D.MipLevels = 1;

    D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    UAVDesc.Texture2D.MipSlice = 0;

    TComPtr<ID3D11Texture2D> NewBlurTempTexture;
    if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, NewBlurTempTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create point shadow blur texture array");
        return false;
    }

    TComPtr<ID3D11ShaderResourceView> NewBlurTempSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBlurTempTexture.Get(), &SRVDesc, NewBlurTempSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create point shadow blur shader resource view");
        return false;
    }

    TComPtr<ID3D11UnorderedAccessView> NewBlurTempUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewBlurTempTexture.Get(), &UAVDesc, NewBlurTempUAV.GetAddressOf())))
    {
        UE_LOG("Failed to create point shadow unordered access view");
        return false;
    }

    PointShadowBlurTempTexture = std::move(NewBlurTempTexture);
    PointShadowBlurTempSRV = std::move(NewBlurTempSRV);
    PointShadowBlurTempUAV = std::move(NewBlurTempUAV);

    TComPtr<ID3D11Texture2D> NewBlurFinalTexture;
    if (FAILED(Device->CreateTexture2D(&TexDesc, nullptr, NewBlurFinalTexture.GetAddressOf())))
    {
        UE_LOG("Failed to create point shadow blur texture array");
        return false;
    }

    TComPtr<ID3D11ShaderResourceView> NewBlurFinalSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBlurFinalTexture.Get(), &SRVDesc, NewBlurFinalSRV.GetAddressOf())))
    {
        UE_LOG("Failed to create point shadow blur shader resource view");
        return false;
    }

    TComPtr<ID3D11UnorderedAccessView> NewBlurFinalUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewBlurFinalTexture.Get(), &UAVDesc, NewBlurFinalUAV.GetAddressOf())))
    {
        UE_LOG("Failed to create point shadow unordered access view");
        return false;
    }

    PointShadowBlurFinalTexture = std::move(NewBlurFinalTexture);
    PointShadowBlurFinalSRV = std::move(NewBlurFinalSRV);
    PointShadowBlurFinalUAV = std::move(NewBlurFinalUAV);

    return true;
}
