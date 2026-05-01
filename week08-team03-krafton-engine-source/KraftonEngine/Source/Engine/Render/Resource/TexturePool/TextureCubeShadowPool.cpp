#include "TextureCubeShadowPool.h"
#include "Profiling/MemoryStats.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Render/Resource/ShaderManager.h"
#include <array>
namespace
{
	constexpr float ShadowDebugContrast = 75.0f;

	template<typename T>
	void SafeRelease(T*& Ptr)
	{
		if (Ptr)
		{
			Ptr->Release();
			Ptr = nullptr;
		}
	}

	uint32 MakeTierResolution(uint32 BaseResolution, uint32 TierIndex)
	{
		const uint32 SafeBase = BaseResolution > 0 ? BaseResolution : 1024;
		switch (TierIndex)
		{
		case 0: return SafeBase / 4 > 0 ? SafeBase / 4 : 1u;
		case 1: return SafeBase / 2 > 0 ? SafeBase / 2 : 1u;
		case 2: return SafeBase;
		default: return SafeBase * 2;
		}
	}

	struct FDebugShadowConstants
	{
		float SrcUVRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		uint32 SrcSlice = 0;
		uint32 bReversedZ = 1;
		float Contrast = ShadowDebugContrast;
		float Padding = 0.0f;
	};

	struct FDebugPassStateBackup
	{
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> RenderTargets = {};
		ID3D11DepthStencilView* DepthStencilView = nullptr;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> Viewports = {};
		UINT ViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		ID3D11VertexShader* VertexShader = nullptr;
		ID3D11PixelShader* PixelShader = nullptr;
		ID3D11InputLayout* InputLayout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11ShaderResourceView* PSSRV0 = nullptr;
		ID3D11SamplerState* PSSampler0 = nullptr;
		ID3D11Buffer* VSCB = nullptr;
		ID3D11Buffer* PSCB = nullptr;
		ID3D11RasterizerState* RasterizerState = nullptr;
		ID3D11BlendState* BlendState = nullptr;
		float BlendFactor[4] = {};
		UINT SampleMask = 0xffffffffu;
		ID3D11DepthStencilState* DepthStencilState = nullptr;
		UINT StencilRef = 0;

		void Capture(ID3D11DeviceContext* Context)
		{
			Context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, RenderTargets.data(), &DepthStencilView);
			Context->RSGetViewports(&ViewportCount, Viewports.data());
			Context->VSGetShader(&VertexShader, nullptr, nullptr);
			Context->PSGetShader(&PixelShader, nullptr, nullptr);
			Context->IAGetInputLayout(&InputLayout);
			Context->IAGetPrimitiveTopology(&PrimitiveTopology);
			Context->PSGetShaderResources(0, 1, &PSSRV0);
			Context->PSGetSamplers(0, 1, &PSSampler0);
			Context->VSGetConstantBuffers(ECBSlot::PerShader0, 1, &VSCB);
			Context->PSGetConstantBuffers(ECBSlot::PerShader0, 1, &PSCB);
			Context->RSGetState(&RasterizerState);
			Context->OMGetBlendState(&BlendState, BlendFactor, &SampleMask);
			Context->OMGetDepthStencilState(&DepthStencilState, &StencilRef);
		}

		void Restore(ID3D11DeviceContext* Context)
		{
			Context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, RenderTargets.data(), DepthStencilView);
			if (ViewportCount > 0)
			{
				Context->RSSetViewports(ViewportCount, Viewports.data());
			}
			Context->VSSetShader(VertexShader, nullptr, 0);
			Context->PSSetShader(PixelShader, nullptr, 0);
			Context->IASetInputLayout(InputLayout);
			Context->IASetPrimitiveTopology(PrimitiveTopology);
			Context->PSSetShaderResources(0, 1, &PSSRV0);
			Context->PSSetSamplers(0, 1, &PSSampler0);
			Context->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &VSCB);
			Context->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &PSCB);
			Context->RSSetState(RasterizerState);
			Context->OMSetBlendState(BlendState, BlendFactor, SampleMask);
			Context->OMSetDepthStencilState(DepthStencilState, StencilRef);

			for (ID3D11RenderTargetView*& RenderTarget : RenderTargets)
			{
				SafeRelease(RenderTarget);
			}
			SafeRelease(DepthStencilView);
			SafeRelease(VertexShader);
			SafeRelease(PixelShader);
			SafeRelease(InputLayout);
			SafeRelease(PSSRV0);
			SafeRelease(PSSampler0);
			SafeRelease(VSCB);
			SafeRelease(PSCB);
			SafeRelease(RasterizerState);
			SafeRelease(BlendState);
			SafeRelease(DepthStencilState);
		}
	};
}

void FTextureCubeShadowPool::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InBaseResolution, uint32 InitialCubeCapacity)
{
	Release();

	Device = InDevice;
	DeviceContext = InDeviceContext;
	BaseResolution = InBaseResolution > 0 ? InBaseResolution : 1024;
	bVSMMode = false;

	for (uint32 TierIndex = 0; TierIndex < TierCount; ++TierIndex)
	{
		Tiers[TierIndex].Resolution = MakeTierResolution(BaseResolution, TierIndex);
		if (InitialCubeCapacity > 0)
		{
			RebuildResources(TierIndex, InitialCubeCapacity);
		}
	}
}

void FTextureCubeShadowPool::Release()
{
	if (TrackedShadowCubeMemory > 0)
	{
		MemoryStats::SubShadowCubeMemory(TrackedShadowCubeMemory);
		TrackedShadowCubeMemory = 0;
	}

	for (FTierPool& Tier : Tiers)
	{
		Tier.FaceDSVs.clear();
		Tier.TempFaceVSMRTVs.clear();
		Tier.FilteredFaceVSMRTVs.clear();
		Tier.TempMomentTexture.Reset();
		Tier.FilteredMomentTexture.Reset();
		Tier.SRV.Reset();
		Tier.DebugArraySRV.Reset();
		Tier.TempVSMArraySRV.Reset();
		Tier.Texture.Reset();
		Tier.AllocationFlags.clear();
		Tier.FreeCubeIndices.clear();
		Tier.Resolution = 0;
		Tier.CubeCapacity = 0;
		Tier.AllocatedCount = 0;
	}

	DebugResources.clear();
	DebugConstantBuffer.Release();
	DebugPointClampSampler.Reset();
	DebugRasterizerState.Reset();
	DebugDepthStencilState.Reset();
	Device = nullptr;
	DeviceContext = nullptr;
	BaseResolution = 1024;
	bVSMMode = false;
}

bool FTextureCubeShadowPool::EnsureVSMMode(bool bUseVSM)
{
	if (!Device || bVSMMode == bUseVSM)
	{
		return true;
	}

	bVSMMode = bUseVSM;
	bool bResult = true;
	for (uint32 TierIndex = 0; TierIndex < TierCount; ++TierIndex)
	{
		FTierPool* Tier = GetTier(TierIndex);
		if (!Tier || Tier->CubeCapacity == 0)
		{
			continue;
		}

		bResult = RebuildResources(TierIndex, Tier->CubeCapacity) && bResult;
	}

	return bResult;
}

FTextureCubeShadowPool::FCubeShadowHandle FTextureCubeShadowPool::Allocate(float ResolutionScale)
{
	const uint32 TierIndex = GetTierIndexForScale(ResolutionScale);
	FTierPool* Tier = GetTier(TierIndex);
	if (!Device || !Tier)
	{
		return {};
	}

	if (Tier->FreeCubeIndices.empty())
	{
		const uint32 NewCapacity = Tier->CubeCapacity > 0 ? Tier->CubeCapacity * 2 : 1;
		Resize(TierIndex, NewCapacity);
	}

	if (Tier->FreeCubeIndices.empty())
	{
		return {};
	}

	const uint32 CubeIndex = Tier->FreeCubeIndices.back();
	Tier->FreeCubeIndices.pop_back();

	if (CubeIndex >= Tier->AllocationFlags.size())
	{
		return {};
	}

	Tier->AllocationFlags[CubeIndex] = 1;
	++Tier->AllocatedCount;

	FCubeShadowHandle Handle;
	Handle.CubeIndex = CubeIndex;
	Handle.TierIndex = TierIndex;
	return Handle;
}

void FTextureCubeShadowPool::ReleaseHandle(FCubeShadowHandle Handle)
{
	FTierPool* Tier = GetTier(Handle.TierIndex);
	if (!Handle.IsValid() || !Tier || Handle.CubeIndex >= Tier->AllocationFlags.size())
	{
		return;
	}

	if (Tier->AllocationFlags[Handle.CubeIndex] == 0)
	{
		return;
	}

	Tier->AllocationFlags[Handle.CubeIndex] = 0;
	Tier->FreeCubeIndices.push_back(Handle.CubeIndex);

	if (Tier->AllocatedCount > 0)
	{
		--Tier->AllocatedCount;
	}
}

ID3D11ShaderResourceView* FTextureCubeShadowPool::GetSRV(uint32 TierIndex) const
{
	const FTierPool* Tier = GetTier(TierIndex);
	return Tier ? Tier->SRV.Get() : nullptr;
}

ID3D11ShaderResourceView* FTextureCubeShadowPool::GetFilteredVSMArraySRV(uint32 TierIndex) const
{
	const FTierPool* Tier = GetTier(TierIndex);
	return Tier ? Tier->DebugArraySRV.Get() : nullptr;
}

ID3D11ShaderResourceView* FTextureCubeShadowPool::GetTempVSMArraySRV(uint32 TierIndex) const
{
	const FTierPool* Tier = GetTier(TierIndex);
	return Tier ? Tier->TempVSMArraySRV.Get() : nullptr;
}

ID3D11DepthStencilView* FTextureCubeShadowPool::GetFaceDSV(FCubeShadowHandle Handle, uint32 FaceIndex) const
{
	const FTierPool* Tier = GetTier(Handle.TierIndex);
	if (!Handle.IsValid() || !Tier || FaceIndex >= CubeFaceCount)
	{
		return nullptr;
	}

	if (bVSMMode)
	{
		return !Tier->FaceDSVs.empty() ? Tier->FaceDSVs[0].Get() : nullptr;
	}

	const uint32 SliceIndex = GetSliceIndex(Handle, FaceIndex);
	if (SliceIndex >= Tier->FaceDSVs.size())
	{
		return nullptr;
	}

	return Tier->FaceDSVs[SliceIndex].Get();
}

ID3D11RenderTargetView* FTextureCubeShadowPool::GetFaceVSMRTV(FCubeShadowHandle Handle, uint32 FaceIndex) const
{
	return GetFilteredFaceVSMRTV(Handle, FaceIndex);
}

ID3D11RenderTargetView* FTextureCubeShadowPool::GetTempFaceVSMRTV(FCubeShadowHandle Handle, uint32 FaceIndex) const
{
	const FTierPool* Tier = GetTier(Handle.TierIndex);
	if (!Handle.IsValid() || !Tier || FaceIndex >= CubeFaceCount)
	{
		return nullptr;
	}

	const uint32 SliceIndex = GetSliceIndex(Handle, FaceIndex);
	if (SliceIndex >= Tier->TempFaceVSMRTVs.size())
	{
		return nullptr;
	}

	return Tier->TempFaceVSMRTVs[SliceIndex].Get();
}

ID3D11RenderTargetView* FTextureCubeShadowPool::GetFilteredFaceVSMRTV(FCubeShadowHandle Handle, uint32 FaceIndex) const
{
	const FTierPool* Tier = GetTier(Handle.TierIndex);
	if (!Handle.IsValid() || !Tier || FaceIndex >= CubeFaceCount)
	{
		return nullptr;
	}

	const uint32 SliceIndex = GetSliceIndex(Handle, FaceIndex);
	if (SliceIndex >= Tier->FilteredFaceVSMRTVs.size())
	{
		return nullptr;
	}

	return Tier->FilteredFaceVSMRTVs[SliceIndex].Get();
}

ID3D11ShaderResourceView* FTextureCubeShadowPool::GetDebugSRV(FCubeShadowHandle Handle)
{
	const FTierPool* Tier = GetTier(Handle.TierIndex);
	if (!Handle.IsValid() || !Tier || !Tier->DebugArraySRV || Tier->Resolution == 0 || !CreateDebugPassResources())
	{
		return nullptr;
	}

	const uint32 Width = Tier->Resolution * 3;
	const uint32 Height = Tier->Resolution * 2;
	const uint32 CacheKey = (Handle.TierIndex << 16) | Handle.CubeIndex;
	FDebugPreviewResource& DebugResource = DebugResources[CacheKey];
	if (!CreateDebugResource(DebugResource, Width, Height))
	{
		return nullptr;
	}

	FShader* DebugShader = FShaderManager::Get().GetOrCreate(EShaderPath::ShadowDepthDebug);
	if (!DebugShader || !DebugShader->IsValid())
	{
		return nullptr;
	}

	FDebugPassStateBackup SavedState;
	SavedState.Capture(DeviceContext);

	ID3D11ShaderResourceView* NullSRV = nullptr;
	DeviceContext->PSSetShaderResources(0, 1, &NullSRV);

	const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	ID3D11RenderTargetView* DebugRTV = DebugResource.RTV.Get();
	ID3D11ShaderResourceView* SourceSRV = Tier->DebugArraySRV.Get();
	ID3D11SamplerState* Sampler = DebugPointClampSampler.Get();
	ID3D11Buffer* ConstantBufferHandle = DebugConstantBuffer.GetBuffer();

	DeviceContext->OMSetRenderTargets(1, &DebugRTV, nullptr);
	DeviceContext->ClearRenderTargetView(DebugRTV, ClearColor);
	DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
	DeviceContext->OMSetDepthStencilState(DebugDepthStencilState.Get(), 0);
	DeviceContext->RSSetState(DebugRasterizerState.Get());
	DebugShader->Bind(DeviceContext);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DeviceContext->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &ConstantBufferHandle);
	DeviceContext->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &ConstantBufferHandle);
	DeviceContext->PSSetSamplers(0, 1, &Sampler);
	DeviceContext->PSSetShaderResources(0, 1, &SourceSRV);

	for (uint32 FaceIndex = 0; FaceIndex < CubeFaceCount; ++FaceIndex)
	{
		FDebugShadowConstants Constants = {};
		Constants.SrcSlice = GetSliceIndex(Handle, FaceIndex);
		DebugConstantBuffer.Update(DeviceContext, &Constants, sizeof(Constants));

		D3D11_VIEWPORT Viewport = {};
		Viewport.TopLeftX = static_cast<float>((FaceIndex % 3) * Tier->Resolution);
		Viewport.TopLeftY = static_cast<float>((FaceIndex / 3) * Tier->Resolution);
		Viewport.Width = static_cast<float>(Tier->Resolution);
		Viewport.Height = static_cast<float>(Tier->Resolution);
		Viewport.MinDepth = 0.0f;
		Viewport.MaxDepth = 1.0f;
		DeviceContext->RSSetViewports(1, &Viewport);
		DeviceContext->Draw(3, 0);
	}

	DeviceContext->PSSetShaderResources(0, 1, &NullSRV);
	SavedState.Restore(DeviceContext);
	return DebugResource.SRV.Get();
}

FPointShadowFaceBasis FTextureCubeShadowPool::GetFaceBasis(uint32 FaceIndex)
{
	static const FVector FaceForwards[CubeFaceCount] =
	{
		FVector(1.0f, 0.0f, 0.0f),
		FVector(-1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, -1.0f, 0.0f),
		FVector(0.0f, 0.0f, 1.0f),
		FVector(0.0f, 0.0f, -1.0f)
	};

	static const FVector FaceUps[CubeFaceCount] =
	{
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, 0.0f, -1.0f),
		FVector(0.0f, 0.0f, 1.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f)
	};

	const uint32 SafeFaceIndex = FaceIndex < CubeFaceCount ? FaceIndex : 0;
	const FVector Forward = FaceForwards[SafeFaceIndex];
	const FVector Up = FaceUps[SafeFaceIndex];

	FPointShadowFaceBasis Basis;
	Basis.Forward = Forward;
	Basis.Up = Up;
	Basis.Right = Up.Cross(Forward);
	return Basis;
}

uint32 FTextureCubeShadowPool::GetResolution(FCubeShadowHandle Handle) const
{
	return GetResolutionForTier(Handle.TierIndex);
}

uint32 FTextureCubeShadowPool::GetResolutionForTier(uint32 TierIndex) const
{
	const FTierPool* Tier = GetTier(TierIndex);
	return Tier ? Tier->Resolution : 0;
}

uint32 FTextureCubeShadowPool::GetTierIndexForScale(float ResolutionScale) const
{
	const float SafeScale = ResolutionScale > 0.0f ? ResolutionScale : 0.0f;
	if (SafeScale <= 0.25f)
	{
		return 0;
	}
	if (SafeScale <= 0.5f)
	{
		return 1;
	}
	if (SafeScale <= 1.0f)
	{
		return 2;
	}
	return 3;
}

uint32 FTextureCubeShadowPool::GetCapacity(uint32 TierIndex) const
{
	const FTierPool* Tier = GetTier(TierIndex);
	return Tier ? Tier->CubeCapacity : 0;
}

uint32 FTextureCubeShadowPool::GetAllocatedCount(uint32 TierIndex) const
{
	const FTierPool* Tier = GetTier(TierIndex);
	return Tier ? Tier->AllocatedCount : 0;
}

void FTextureCubeShadowPool::Resize(uint32 TierIndex, uint32 NewCubeCapacity)
{
	FTierPool* Tier = GetTier(TierIndex);
	if (!Tier || NewCubeCapacity <= Tier->CubeCapacity)
	{
		return;
	}

	RebuildResources(TierIndex, NewCubeCapacity);
}

bool FTextureCubeShadowPool::RebuildResources(uint32 TierIndex, uint32 NewCubeCapacity)
{
	FTierPool* Tier = GetTier(TierIndex);
	if (!Device || !Tier || Tier->Resolution == 0 || NewCubeCapacity == 0)
	{
		return false;
	}

	TComPtr<ID3D11Texture2D> NewTexture;
	TComPtr<ID3D11ShaderResourceView> NewSRV;
	TComPtr<ID3D11ShaderResourceView> NewDebugArraySRV;
	TComPtr<ID3D11Texture2D> NewTempMomentTexture;
	TComPtr<ID3D11Texture2D> NewFilteredMomentTexture;
	TComPtr<ID3D11ShaderResourceView> NewTempVSMArraySRV;
	TArray<TComPtr<ID3D11DepthStencilView>> NewFaceDSVs;
	TArray<TComPtr<ID3D11RenderTargetView>> NewTempFaceVSMRTVs;
	TArray<TComPtr<ID3D11RenderTargetView>> NewFilteredFaceVSMRTVs;

	const uint32 TotalSlices = NewCubeCapacity * CubeFaceCount;

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = Tier->Resolution;
	TextureDesc.Height = Tier->Resolution;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = bVSMMode ? 1 : TotalSlices;
	TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = bVSMMode ? D3D11_BIND_DEPTH_STENCIL : (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL);
	TextureDesc.CPUAccessFlags = 0;
	TextureDesc.MiscFlags = bVSMMode ? 0 : D3D11_RESOURCE_MISC_TEXTURECUBE;

	HRESULT hr = Device->CreateTexture2D(&TextureDesc, nullptr, NewTexture.GetAddressOf());
	if (FAILED(hr))
	{
		assert(false);
		return false;
	}

	if (bVSMMode)
	{
		D3D11_TEXTURE2D_DESC MomentTextureDesc = {};
		MomentTextureDesc.Width = Tier->Resolution;
		MomentTextureDesc.Height = Tier->Resolution;
		MomentTextureDesc.MipLevels = 1;
		MomentTextureDesc.ArraySize = TotalSlices;
		MomentTextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		MomentTextureDesc.SampleDesc.Count = 1;
		MomentTextureDesc.Usage = D3D11_USAGE_DEFAULT;
		MomentTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		MomentTextureDesc.CPUAccessFlags = 0;
		MomentTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

		hr = Device->CreateTexture2D(&MomentTextureDesc, nullptr, NewTempMomentTexture.GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}
		hr = Device->CreateTexture2D(&MomentTextureDesc, nullptr, NewFilteredMomentTexture.GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		SRVDesc.TextureCubeArray.MostDetailedMip = 0;
		SRVDesc.TextureCubeArray.MipLevels = 1;
		SRVDesc.TextureCubeArray.First2DArrayFace = 0;
		SRVDesc.TextureCubeArray.NumCubes = NewCubeCapacity;

		hr = Device->CreateShaderResourceView(NewFilteredMomentTexture.Get(), &SRVDesc, NewSRV.GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC DebugSRVDesc = {};
		DebugSRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		DebugSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		DebugSRVDesc.Texture2DArray.MostDetailedMip = 0;
		DebugSRVDesc.Texture2DArray.MipLevels = 1;
		DebugSRVDesc.Texture2DArray.FirstArraySlice = 0;
		DebugSRVDesc.Texture2DArray.ArraySize = TotalSlices;

		hr = Device->CreateShaderResourceView(NewTempMomentTexture.Get(), &DebugSRVDesc, NewTempVSMArraySRV.GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}
		hr = Device->CreateShaderResourceView(NewFilteredMomentTexture.Get(), &DebugSRVDesc, NewDebugArraySRV.GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}
	}
	else
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		SRVDesc.TextureCubeArray.MostDetailedMip = 0;
		SRVDesc.TextureCubeArray.MipLevels = 1;
		SRVDesc.TextureCubeArray.First2DArrayFace = 0;
		SRVDesc.TextureCubeArray.NumCubes = NewCubeCapacity;

		hr = Device->CreateShaderResourceView(NewTexture.Get(), &SRVDesc, NewSRV.GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC DebugSRVDesc = {};
		DebugSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
		DebugSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		DebugSRVDesc.Texture2DArray.MostDetailedMip = 0;
		DebugSRVDesc.Texture2DArray.MipLevels = 1;
		DebugSRVDesc.Texture2DArray.FirstArraySlice = 0;
		DebugSRVDesc.Texture2DArray.ArraySize = TotalSlices;

		hr = Device->CreateShaderResourceView(NewTexture.Get(), &DebugSRVDesc, NewDebugArraySRV.GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}
	}

	const uint32 DSVCount = bVSMMode ? 1 : TotalSlices;
	NewFaceDSVs.resize(DSVCount);
	if (bVSMMode)
	{
		NewTempFaceVSMRTVs.resize(TotalSlices);
		NewFilteredFaceVSMRTVs.resize(TotalSlices);
	}
	for (uint32 SliceIndex = 0; SliceIndex < DSVCount; ++SliceIndex)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
		DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
		if (bVSMMode)
		{
			DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			DSVDesc.Texture2D.MipSlice = 0;
		}
		else
		{
			DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			DSVDesc.Texture2DArray.MipSlice = 0;
			DSVDesc.Texture2DArray.FirstArraySlice = SliceIndex;
			DSVDesc.Texture2DArray.ArraySize = 1;
		}

		hr = Device->CreateDepthStencilView(NewTexture.Get(), &DSVDesc, NewFaceDSVs[SliceIndex].GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}

	}

	if (bVSMMode)
	{
		for (uint32 SliceIndex = 0; SliceIndex < TotalSlices; ++SliceIndex)
		{
			D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
			RTVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
			RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			RTVDesc.Texture2DArray.MipSlice = 0;
			RTVDesc.Texture2DArray.FirstArraySlice = SliceIndex;
			RTVDesc.Texture2DArray.ArraySize = 1;

			hr = Device->CreateRenderTargetView(NewTempMomentTexture.Get(), &RTVDesc, NewTempFaceVSMRTVs[SliceIndex].GetAddressOf());
			if (FAILED(hr))
			{
				assert(false);
				return false;
			}

			hr = Device->CreateRenderTargetView(NewFilteredMomentTexture.Get(), &RTVDesc, NewFilteredFaceVSMRTVs[SliceIndex].GetAddressOf());
			if (FAILED(hr))
			{
				assert(false);
				return false;
			}
		}
	}

	const uint32 OldCapacity = Tier->CubeCapacity;

	Tier->Texture = std::move(NewTexture);
	Tier->SRV = std::move(NewSRV);
	Tier->DebugArraySRV = std::move(NewDebugArraySRV);
	Tier->TempMomentTexture = std::move(NewTempMomentTexture);
	Tier->FilteredMomentTexture = std::move(NewFilteredMomentTexture);
	Tier->TempVSMArraySRV = std::move(NewTempVSMArraySRV);
	Tier->FaceDSVs = std::move(NewFaceDSVs);
	Tier->TempFaceVSMRTVs = std::move(NewTempFaceVSMRTVs);
	Tier->FilteredFaceVSMRTVs = std::move(NewFilteredFaceVSMRTVs);
	Tier->CubeCapacity = NewCubeCapacity;
	DebugResources.clear();

	Tier->AllocationFlags.resize(Tier->CubeCapacity, 0);
	for (uint32 CubeIndex = Tier->CubeCapacity; CubeIndex > OldCapacity; --CubeIndex)
	{
		Tier->FreeCubeIndices.push_back(CubeIndex - 1);
	}

	UpdateMemoryStats();
	return true;
}

bool FTextureCubeShadowPool::CreateDebugResource(FDebugPreviewResource& OutResource, uint32 Width, uint32 Height)
{
	if (OutResource.Texture && OutResource.RTV && OutResource.SRV && OutResource.Width == Width && OutResource.Height == Height)
	{
		return true;
	}

	if (!Device || Width == 0 || Height == 0)
	{
		return false;
	}

	OutResource.Texture.Reset();
	OutResource.RTV.Reset();
	OutResource.SRV.Reset();

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = Width;
	TextureDesc.Height = Height;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = Device->CreateTexture2D(&TextureDesc, nullptr, OutResource.Texture.GetAddressOf());
	if (FAILED(hr))
	{
		return false;
	}

	D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
	RTVDesc.Format = TextureDesc.Format;
	RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	RTVDesc.Texture2D.MipSlice = 0;

	hr = Device->CreateRenderTargetView(OutResource.Texture.Get(), &RTVDesc, OutResource.RTV.GetAddressOf());
	if (FAILED(hr))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = TextureDesc.Format;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	hr = Device->CreateShaderResourceView(OutResource.Texture.Get(), &SRVDesc, OutResource.SRV.GetAddressOf());
	if (FAILED(hr))
	{
		return false;
	}

	OutResource.Width = Width;
	OutResource.Height = Height;
	return true;
}

bool FTextureCubeShadowPool::CreateDebugPassResources()
{
	if (!Device || !DeviceContext)
	{
		return false;
	}

	if (!DebugConstantBuffer.GetBuffer())
	{
		DebugConstantBuffer.Create(Device, sizeof(FDebugShadowConstants));
	}

	if (!DebugPointClampSampler)
	{
		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		SamplerDesc.MinLOD = 0.0f;
		SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		HRESULT hr = Device->CreateSamplerState(&SamplerDesc, DebugPointClampSampler.GetAddressOf());
		if (FAILED(hr))
		{
			return false;
		}
	}

	if (!DebugRasterizerState)
	{
		D3D11_RASTERIZER_DESC RasterizerDesc = {};
		RasterizerDesc.FillMode = D3D11_FILL_SOLID;
		RasterizerDesc.CullMode = D3D11_CULL_NONE;
		RasterizerDesc.DepthClipEnable = TRUE;

		HRESULT hr = Device->CreateRasterizerState(&RasterizerDesc, DebugRasterizerState.GetAddressOf());
		if (FAILED(hr))
		{
			return false;
		}
	}

	if (!DebugDepthStencilState)
	{
		D3D11_DEPTH_STENCIL_DESC DepthStencilDesc = {};
		DepthStencilDesc.DepthEnable = FALSE;
		DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		DepthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		DepthStencilDesc.StencilEnable = FALSE;

		HRESULT hr = Device->CreateDepthStencilState(&DepthStencilDesc, DebugDepthStencilState.GetAddressOf());
		if (FAILED(hr))
		{
			return false;
		}
	}

	return DebugConstantBuffer.GetBuffer() && DebugPointClampSampler && DebugRasterizerState && DebugDepthStencilState;
}

void FTextureCubeShadowPool::UpdateMemoryStats()
{
	uint64 NewMemory = 0;
	for (const FTierPool& Tier : Tiers)
	{
		NewMemory += MemoryStats::CalculateTextureMemory(Tier.Texture.Get());
		NewMemory += MemoryStats::CalculateTextureMemory(Tier.TempMomentTexture.Get());
		NewMemory += MemoryStats::CalculateTextureMemory(Tier.FilteredMomentTexture.Get());
	}

	if (NewMemory > TrackedShadowCubeMemory)
	{
		MemoryStats::AddShadowCubeMemory(NewMemory - TrackedShadowCubeMemory);
	}
	else if (TrackedShadowCubeMemory > NewMemory)
	{
		MemoryStats::SubShadowCubeMemory(TrackedShadowCubeMemory - NewMemory);
	}

	TrackedShadowCubeMemory = NewMemory;
}

uint32 FTextureCubeShadowPool::GetSliceIndex(FCubeShadowHandle Handle, uint32 FaceIndex) const
{
	return Handle.CubeIndex * CubeFaceCount + FaceIndex;
}

FTextureCubeShadowPool::FTierPool* FTextureCubeShadowPool::GetTier(uint32 TierIndex)
{
	return TierIndex < TierCount ? &Tiers[TierIndex] : nullptr;
}

const FTextureCubeShadowPool::FTierPool* FTextureCubeShadowPool::GetTier(uint32 TierIndex) const
{
	return TierIndex < TierCount ? &Tiers[TierIndex] : nullptr;
}
