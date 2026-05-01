#include "TextureAtlasPool.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Render/Resource/ShaderManager.h"
#include "Profiling/MemoryStats.h"
#include <algorithm>
#include <array>
#include <cmath>

template<typename T>
using TComPtr = Microsoft::WRL::ComPtr<T>;
using TexturePoolHandle = FTexturePoolBase::TexturePoolHandle;
using TexturePoolHandleSet = FTexturePoolBase::TexturePoolHandleSet;

namespace
{
	constexpr uint32 HandleDebugKeyPrefix = 0x40000000u;
	constexpr uint32 LayerDebugKeyPrefix = 0x80000000u;
	constexpr uint32 ShadowAtlasLayerCount = 2;
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

	uint32 GetPreviewExtent(float UVMin, float UVMax, uint32 TextureSize)
	{
		const float Extent = std::abs(UVMax - UVMin) * static_cast<float>(TextureSize);
		const uint32 PixelExtent = static_cast<uint32>(std::lround(Extent));
		return PixelExtent > 0 ? PixelExtent : 1u;
	}

	struct FDebugShadowConstants
	{
		float SrcUVRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		uint32 SrcSlice = 0;
		uint32 bReversedZ = 1;
		float Contrast = ShadowDebugContrast;
		float Padding = 0.0f;
	};

	struct FDebugRegionDraw
	{
		FAtlasUV UV = {};
		uint32 SliceIndex = 0;
		D3D11_VIEWPORT Viewport = {};
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
			Context->OMGetRenderTargets(
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
				RenderTargets.data(),
				&DepthStencilView);
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
			Context->OMSetRenderTargets(
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
				RenderTargets.data(),
				DepthStencilView);
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

	bool RenderShadowDebugPreview(
		ID3D11DeviceContext* DeviceContext,
		FConstantBuffer& DebugConstantBuffer,
		ID3D11ShaderResourceView* AtlasSRV,
		ID3D11RenderTargetView* DebugRTV,
		ID3D11SamplerState* PointClampSampler,
		ID3D11RasterizerState* RasterizerState,
		ID3D11DepthStencilState* DepthStencilState,
		const TArray<FDebugRegionDraw>& Draws)
	{
		if (!DeviceContext || !AtlasSRV || !DebugRTV || !PointClampSampler || !RasterizerState || !DepthStencilState)
		{
			return false;
		}

		FShader* DebugShader = FShaderManager::Get().GetOrCreate(EShaderPath::ShadowDepthDebug);
		if (!DebugShader || !DebugShader->IsValid())
		{
			return false;
		}

		FDebugPassStateBackup SavedState;
		SavedState.Capture(DeviceContext);

		ID3D11ShaderResourceView* NullSRV = nullptr;
		DeviceContext->PSSetShaderResources(0, 1, &NullSRV);

		const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		DeviceContext->OMSetRenderTargets(1, &DebugRTV, nullptr);
		DeviceContext->ClearRenderTargetView(DebugRTV, ClearColor);
		DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
		DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);
		DeviceContext->RSSetState(RasterizerState);
		DebugShader->Bind(DeviceContext);
		DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		ID3D11Buffer* ConstantBufferHandle = DebugConstantBuffer.GetBuffer();
		DeviceContext->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &ConstantBufferHandle);
		DeviceContext->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &ConstantBufferHandle);
		DeviceContext->PSSetSamplers(0, 1, &PointClampSampler);
		DeviceContext->PSSetShaderResources(0, 1, &AtlasSRV);

		for (const FDebugRegionDraw& Draw : Draws)
		{
			FDebugShadowConstants Constants = {};
			Constants.SrcUVRect[0] = Draw.UV.u1;
			Constants.SrcUVRect[1] = Draw.UV.v1;
			Constants.SrcUVRect[2] = Draw.UV.u2;
			Constants.SrcUVRect[3] = Draw.UV.v2;
			Constants.SrcSlice = Draw.SliceIndex;

			DebugConstantBuffer.Update(DeviceContext, &Constants, sizeof(Constants));
			DeviceContext->RSSetViewports(1, &Draw.Viewport);
			DeviceContext->Draw(3, 0);
		}

		DeviceContext->PSSetShaderResources(0, 1, &NullSRV);
		SavedState.Restore(DeviceContext);
		return true;
	}
}

FTextureAtlasPool::~FTextureAtlasPool()
{
	Release();
}

void FTextureAtlasPool::Initialize(
	ID3D11Device* InDevice,
	ID3D11DeviceContext* InDeviceContext,
	uint32 InTextureSize,
	uint32 InAllocatorMinBlockSize)
{
	CurrentFilterMode = EShadowFilterMode::PCF;
	FTexturePoolBase::Initialize(InDevice, InDeviceContext, InTextureSize, InAllocatorMinBlockSize);
	if (GetAllocatedLayerCount() < ShadowAtlasLayerCount)
	{
		ResizeLayer(ShadowAtlasLayerCount);
	}
	CreateDebugPassResources();
}

void FTextureAtlasPool::Release()
{
	if (TrackedShadowAtlasMemory > 0)
	{
		MemoryStats::SubShadowAtlasMemory(TrackedShadowAtlasMemory);
		TrackedShadowAtlasMemory = 0;
	}

	DebugResource.clear();
	SliceDebugVersions.clear();

	DSVs.clear();
	SliceSRVs.clear();
	RTVs.clear();
	VSMFilteredRTVs.clear();
	VSMTempRTVs.clear();

	VSMFilteredSRV.Reset();
	VSMTempSRV.Reset();
	SRV.Reset();

	VSMDepthTexture.Reset();
	VSMFilteredTexture.Reset();
	VSMTempTexture.Reset();
	Texture.Reset();

	DebugConstantBuffer.Release();
	DebugPointClampSampler.Reset();
	DebugRasterizerState.Reset();
	DebugDepthStencilState.Reset();

	CurrentFilterMode = EShadowFilterMode::PCF;
}

std::unique_ptr<FTexturePoolAllocatorBase> FTextureAtlasPool::CreateAllocator()
{
	return std::make_unique<FBuddyTexturePoolAllocator>();
}

void FTextureAtlasPool::EnsureAtlasMode(EShadowFilterMode InFilterMode)
{
	if (CurrentFilterMode == InFilterMode)
	{
		return;
	}

	CurrentFilterMode = InFilterMode;
	RecreateAtlasResources();
}

void FTextureAtlasPool::RecreateAtlasResources()
{
	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return;
	}

	DebugResource.clear();
	Texture = CreateTexture(Device);
	RebuildSRV(Device, Texture.Get());
	RebuildDSV(Device, Texture.Get());
	UpdateMemoryStats();
	BroadCastHandlesUnvalid();
}

TArray<FAtlasUV> FTextureAtlasPool::GetAtlasUVArray(const TexturePoolHandleSet* InHandleSet)
{
	TArray<FAtlasUV> Result;
	if (!InHandleSet)
	{
		return Result;
	}

	Result.reserve(InHandleSet->Handles.size());
	for (const TexturePoolHandle& Handle : InHandleSet->Handles)
	{
		Result.push_back(GetAtlasUV(Handle));
	}

	return Result;
}

TArray<ID3D11ShaderResourceView*> FTextureAtlasPool::GetSliceSRVs(TexturePoolHandleSet* HandleSet)
{
	TArray<ID3D11ShaderResourceView*> Result;
	if (!HandleSet)
	{
		return Result;
	}

	TArray<TexturePoolHandle> Handles = HandleSet->Handles;
	Result.reserve(Handles.size());
	for (const TexturePoolHandle& Handle : Handles)
	{
		if (Handle.ArrayIndex < static_cast<uint32>(SliceSRVs.size()))
		{
			Result.push_back(SliceSRVs[Handle.ArrayIndex].Get());
		}
		else
		{
			Result.push_back(nullptr);
		}
	}

	return Result;
}

TArray<ID3D11DepthStencilView*> FTextureAtlasPool::GetDSVs(TexturePoolHandleSet* HandleSet)
{
	TArray<ID3D11DepthStencilView*> Result;
	if (!HandleSet)
	{
		return Result;
	}

	MarkDebugDirty(HandleSet);
	MarkSliceDebugDirty(HandleSet);

	TArray<TexturePoolHandle> Handles = HandleSet->Handles;
	Result.reserve(Handles.size());
	for (const TexturePoolHandle& Handle : Handles)
	{
		if (Handle.ArrayIndex < static_cast<uint32>(DSVs.size()))
		{
			Result.push_back(DSVs[Handle.ArrayIndex].Get());
		}
		else
		{
			Result.push_back(nullptr);
		}
	}

	return Result;
}

TArray<ID3D11RenderTargetView*> FTextureAtlasPool::GetRTVs(TexturePoolHandleSet* HandleSet)
{
	TArray<ID3D11RenderTargetView*> Result;
	if (!IsVSMMode() || !HandleSet)
	{
		return Result;
	}

	MarkDebugDirty(HandleSet);
	MarkSliceDebugDirty(HandleSet);

	TArray<TexturePoolHandle> Handles = HandleSet->Handles;
	Result.reserve(Handles.size());
	for (const TexturePoolHandle& Handle : Handles)
	{
		if (Handle.ArrayIndex < static_cast<uint32>(RTVs.size()))
		{
			Result.push_back(RTVs[Handle.ArrayIndex].Get());
		}
		else
		{
			Result.push_back(nullptr);
		}
	}

	return Result;
}

TArray<ID3D11RenderTargetView*> FTextureAtlasPool::GetFilteredRTVs(TexturePoolHandleSet* HandleSet)
{
	TArray<ID3D11RenderTargetView*> Result;
	if (!IsVSMMode() || !HandleSet)
	{
		return Result;
	}

	TArray<TexturePoolHandle> Handles = HandleSet->Handles;
	Result.reserve(Handles.size());
	for (const TexturePoolHandle& Handle : Handles)
	{
		if (Handle.ArrayIndex < static_cast<uint32>(VSMFilteredRTVs.size()))
		{
			Result.push_back(VSMFilteredRTVs[Handle.ArrayIndex].Get());
		}
		else
		{
			Result.push_back(nullptr);
		}
	}

	return Result;
}

TArray<ID3D11RenderTargetView*> FTextureAtlasPool::GetTempRTVs(TexturePoolHandleSet* HandleSet)
{
	TArray<ID3D11RenderTargetView*> Result;
	if (!IsVSMMode() || !HandleSet)
	{
		return Result;
	}

	TArray<TexturePoolHandle> Handles = HandleSet->Handles;
	Result.reserve(Handles.size());
	for (const TexturePoolHandle& Handle : Handles)
	{
		if (Handle.ArrayIndex < static_cast<uint32>(VSMTempRTVs.size()))
		{
			Result.push_back(VSMTempRTVs[Handle.ArrayIndex].Get());
		}
		else
		{
			Result.push_back(nullptr);
		}
	}

	return Result;
}

ID3D11ShaderResourceView* FTextureAtlasPool::GetDebugSRV(const TexturePoolHandle& InHandle)
{
	if (!Texture || !SRV || InHandle.ArrayIndex >= TextureLayerSize || !CreateDebugPassResources())
	{
		return nullptr;
	}

	const FAtlasUV UV = GetAtlasUV(InHandle);
	const uint32 Width = GetPreviewExtent(UV.u1, UV.u2, TextureSize);
	const uint32 Height = GetPreviewExtent(UV.v1, UV.v2, TextureSize);
	const uint32 CacheKey = MakeHandleDebugKey(InHandle);
	const uint64 SourceVersion = SliceDebugVersions[InHandle.ArrayIndex];

	auto& Cached = DebugResource[CacheKey];
	if (!Cached)
	{
		Cached = std::make_unique<SRVResource>();
	}

	SRVResource& DebugResourceRef = *Cached;
	if (DebugResourceRef.SRV &&
		DebugResourceRef.Width == Width &&
		DebugResourceRef.Height == Height &&
		DebugResourceRef.Version == SourceVersion &&
		DebugResourceRef.SourceInternalIndex == InHandle.InternalIndex &&
		DebugResourceRef.SourceArrayIndex == InHandle.ArrayIndex)
	{
		return DebugResourceRef.SRV.Get();
	}

	if (!CreateDebugResource(DebugResourceRef, Width, Height))
	{
		return nullptr;
	}

	FDebugRegionDraw Draw = {};
	Draw.UV = UV;
	Draw.SliceIndex = InHandle.ArrayIndex;
	Draw.Viewport.TopLeftX = 0.0f;
	Draw.Viewport.TopLeftY = 0.0f;
	Draw.Viewport.Width = static_cast<float>(Width);
	Draw.Viewport.Height = static_cast<float>(Height);
	Draw.Viewport.MinDepth = 0.0f;
	Draw.Viewport.MaxDepth = 1.0f;

	TArray<FDebugRegionDraw> Draws = { Draw };
	if (!RenderShadowDebugPreview(
		GetDeviceContext(),
		DebugConstantBuffer,
		SRV.Get(),
		DebugResourceRef.RTV.Get(),
		DebugPointClampSampler.Get(),
		DebugRasterizerState.Get(),
		DebugDepthStencilState.Get(),
		Draws))
	{
		return nullptr;
	}

	DebugResourceRef.Version = SourceVersion;
	DebugResourceRef.SourceInternalIndex = InHandle.InternalIndex;
	DebugResourceRef.SourceArrayIndex = InHandle.ArrayIndex;
	return DebugResourceRef.SRV.Get();
}

ID3D11ShaderResourceView* FTextureAtlasPool::GetDebugSRV(const TexturePoolHandleSet* InHandleSet)
{
	if (!Texture || !SRV || !InHandleSet || !InHandleSet->bIsValid || InHandleSet->Handles.empty() || !CreateDebugPassResources())
	{
		return nullptr;
	}

	TArray<FDebugRegionDraw> Draws;
	Draws.reserve(InHandleSet->Handles.size());

	uint32 TotalWidth = 0;
	uint32 MaxHeight = 0;
	for (const TexturePoolHandle& Handle : InHandleSet->Handles)
	{
		if (Handle.ArrayIndex >= TextureLayerSize)
		{
			return nullptr;
		}

		const FAtlasUV UV = GetAtlasUV(Handle);
		const uint32 PieceWidth = GetPreviewExtent(UV.u1, UV.u2, TextureSize);
		const uint32 PieceHeight = GetPreviewExtent(UV.v1, UV.v2, TextureSize);

		FDebugRegionDraw Draw = {};
		Draw.UV = UV;
		Draw.SliceIndex = Handle.ArrayIndex;
		Draw.Viewport.TopLeftX = static_cast<float>(TotalWidth);
		Draw.Viewport.TopLeftY = 0.0f;
		Draw.Viewport.Width = static_cast<float>(PieceWidth);
		Draw.Viewport.Height = static_cast<float>(PieceHeight);
		Draw.Viewport.MinDepth = 0.0f;
		Draw.Viewport.MaxDepth = 1.0f;

		TotalWidth += PieceWidth;
		MaxHeight = MaxHeight > PieceHeight ? MaxHeight : PieceHeight;
		Draws.push_back(Draw);
	}

	if (TotalWidth == 0 || MaxHeight == 0)
	{
		return nullptr;
	}

	auto& Cached = DebugResource[InHandleSet->InternalIndex];
	if (!Cached)
	{
		Cached = std::make_unique<SRVResource>();
	}

	SRVResource& DebugResourceRef = *Cached;
	if (DebugResourceRef.SRV &&
		DebugResourceRef.Width == TotalWidth &&
		DebugResourceRef.Height == MaxHeight &&
		DebugResourceRef.Version == InHandleSet->DebugVersion)
	{
		return DebugResourceRef.SRV.Get();
	}

	if (!CreateDebugResource(DebugResourceRef, TotalWidth, MaxHeight))
	{
		return nullptr;
	}

	if (!RenderShadowDebugPreview(
		GetDeviceContext(),
		DebugConstantBuffer,
		SRV.Get(),
		DebugResourceRef.RTV.Get(),
		DebugPointClampSampler.Get(),
		DebugRasterizerState.Get(),
		DebugDepthStencilState.Get(),
		Draws))
	{
		return nullptr;
	}

	DebugResourceRef.Version = InHandleSet->DebugVersion;
	DebugResourceRef.SourceInternalIndex = InHandleSet->InternalIndex;
	DebugResourceRef.SourceArrayIndex = static_cast<uint32>(-1);
	return DebugResourceRef.SRV.Get();
}

ID3D11ShaderResourceView* FTextureAtlasPool::GetDebugLayerSRV(uint32 SliceIndex)
{
	if (!Texture || !SRV || SliceIndex >= TextureLayerSize || !CreateDebugPassResources())
	{
		return nullptr;
	}

	const uint32 CacheKey = LayerDebugKeyPrefix | SliceIndex;
	const uint64 SourceVersion = SliceDebugVersions[SliceIndex];

	auto& Cached = DebugResource[CacheKey];
	if (!Cached)
	{
		Cached = std::make_unique<SRVResource>();
	}

	SRVResource& DebugResourceRef = *Cached;
	if (DebugResourceRef.SRV &&
		DebugResourceRef.Width == TextureSize &&
		DebugResourceRef.Height == TextureSize &&
		DebugResourceRef.Version == SourceVersion)
	{
		return DebugResourceRef.SRV.Get();
	}

	if (!CreateDebugResource(DebugResourceRef, TextureSize, TextureSize))
	{
		return nullptr;
	}

	FDebugRegionDraw Draw = {};
	Draw.UV = { SliceIndex, 0.0f, 0.0f, 1.0f, 1.0f };
	Draw.SliceIndex = SliceIndex;
	Draw.Viewport.TopLeftX = 0.0f;
	Draw.Viewport.TopLeftY = 0.0f;
	Draw.Viewport.Width = static_cast<float>(TextureSize);
	Draw.Viewport.Height = static_cast<float>(TextureSize);
	Draw.Viewport.MinDepth = 0.0f;
	Draw.Viewport.MaxDepth = 1.0f;

	TArray<FDebugRegionDraw> Draws = { Draw };
	if (!RenderShadowDebugPreview(
		GetDeviceContext(),
		DebugConstantBuffer,
		SRV.Get(),
		DebugResourceRef.RTV.Get(),
		DebugPointClampSampler.Get(),
		DebugRasterizerState.Get(),
		DebugDepthStencilState.Get(),
		Draws))
	{
		return nullptr;
	}

	DebugResourceRef.Version = SourceVersion;
	DebugResourceRef.SourceInternalIndex = SliceIndex;
	DebugResourceRef.SourceArrayIndex = SliceIndex;
	return DebugResourceRef.SRV.Get();
}

TComPtr<ID3D11Texture2D> FTextureAtlasPool::CreateTexture(ID3D11Device* Device)
{
	if (IsVSMMode())
	{
		return CreateVSMMomentTexture(Device);
	}

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = TextureSize;
	desc.Height = TextureSize;
	desc.MipLevels = 1;
	desc.ArraySize = TextureLayerSize;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = 0;
	desc.CPUAccessFlags = 0;
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;

	TComPtr<ID3D11Texture2D> NewTexture;
	HRESULT hr = Device->CreateTexture2D(&desc, nullptr, NewTexture.GetAddressOf());
	assert(SUCCEEDED(hr));

	return NewTexture;
}

TComPtr<ID3D11Texture2D> FTextureAtlasPool::CreateVSMDepthTexture(ID3D11Device* Device)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = TextureSize;
	desc.Height = TextureSize;
	desc.MipLevels = 1;
	desc.ArraySize = TextureLayerSize;
	desc.Format = DXGI_FORMAT_D32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = 0;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	desc.CPUAccessFlags = 0;

	TComPtr<ID3D11Texture2D> NewTexture;
	HRESULT hr = Device->CreateTexture2D(&desc, nullptr, NewTexture.GetAddressOf());
	assert(SUCCEEDED(hr));

	return NewTexture;
}

TComPtr<ID3D11Texture2D> FTextureAtlasPool::CreateVSMMomentTexture(ID3D11Device* Device)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = TextureSize;
	desc.Height = TextureSize;
	desc.MipLevels = 1;
	desc.ArraySize = TextureLayerSize;
	desc.Format = DXGI_FORMAT_R32G32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = 0;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.CPUAccessFlags = 0;

	TComPtr<ID3D11Texture2D> NewTexture;
	HRESULT hr = Device->CreateTexture2D(&desc, nullptr, NewTexture.GetAddressOf());
	assert(SUCCEEDED(hr));

	return NewTexture;
}

void FTextureAtlasPool::RebuildVSMMomentSRV(
	ID3D11Device* Device,
	ID3D11Texture2D* InTexture,
	TComPtr<ID3D11ShaderResourceView>& OutSRV)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = TextureLayerSize;

	OutSRV.Reset();
	HRESULT hr = Device->CreateShaderResourceView(InTexture, &srvDesc, OutSRV.GetAddressOf());
	assert(SUCCEEDED(hr));
}

void FTextureAtlasPool::RebuildSliceSRVs(
	ID3D11Device* Device,
	ID3D11Texture2D* InTexture,
	DXGI_FORMAT Format,
	TArray<TComPtr<ID3D11ShaderResourceView>>& OutSRVs)
{
	OutSRVs.clear();
	OutSRVs.resize(TextureLayerSize);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.ArraySize = 1;

	for (uint32 SliceIndex = 0; SliceIndex < TextureLayerSize; ++SliceIndex)
	{
		srvDesc.Texture2DArray.FirstArraySlice = SliceIndex;

		HRESULT hr = Device->CreateShaderResourceView(
			InTexture,
			&srvDesc,
			OutSRVs[SliceIndex].GetAddressOf());

		assert(SUCCEEDED(hr));
	}
}

void FTextureAtlasPool::RebuildVSMMomentRTVs(
	ID3D11Device* Device,
	ID3D11Texture2D* InTexture,
	TArray<TComPtr<ID3D11RenderTargetView>>& OutRTVs)
{
	OutRTVs.clear();
	OutRTVs.resize(TextureLayerSize);

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvDesc.Texture2DArray.MipSlice = 0;
	rtvDesc.Texture2DArray.ArraySize = 1;

	for (uint32 SliceIndex = 0; SliceIndex < TextureLayerSize; ++SliceIndex)
	{
		rtvDesc.Texture2DArray.FirstArraySlice = SliceIndex;

		HRESULT hr = Device->CreateRenderTargetView(
			InTexture,
			&rtvDesc,
			OutRTVs[SliceIndex].GetAddressOf());

		assert(SUCCEEDED(hr));
	}
}

void FTextureAtlasPool::RebuildVSMBlurResources(ID3D11Device* Device)
{
	// Blur shader를 연결할 때 사용하는 보조 리소스들이다.
	// 계약은 Raw(Texture/SRV) -> Temp(RTV/SRV) -> Filtered(RTV/SRV) 순서다.
	VSMFilteredTexture = CreateVSMMomentTexture(Device);
	VSMTempTexture = CreateVSMMomentTexture(Device);

	RebuildVSMMomentSRV(Device, VSMFilteredTexture.Get(), VSMFilteredSRV);
	RebuildVSMMomentSRV(Device, VSMTempTexture.Get(), VSMTempSRV);
	RebuildVSMMomentRTVs(Device, VSMFilteredTexture.Get(), VSMFilteredRTVs);
	RebuildVSMMomentRTVs(Device, VSMTempTexture.Get(), VSMTempRTVs);
}

void FTextureAtlasPool::RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	if (IsVSMMode())
	{
		RebuildVSMMomentSRV(Device, InTexture, SRV);
		RebuildSliceSRVs(Device, InTexture, DXGI_FORMAT_R32G32_FLOAT, SliceSRVs);
		RebuildRTVs(Device, InTexture);
		RebuildVSMBlurResources(Device);
	}
	else
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.ArraySize = TextureLayerSize;
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;

		SRV.Reset();
		HRESULT hr = Device->CreateShaderResourceView(InTexture, &srvDesc, SRV.GetAddressOf());
		assert(SUCCEEDED(hr));
		RebuildSliceSRVs(Device, InTexture, DXGI_FORMAT_R32_FLOAT, SliceSRVs);

		RTVs.clear();
		VSMFilteredTexture.Reset();
		VSMTempTexture.Reset();
		VSMFilteredSRV.Reset();
		VSMTempSRV.Reset();
		VSMFilteredRTVs.clear();
		VSMTempRTVs.clear();
	}
}

void FTextureAtlasPool::RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	if (!IsVSMMode())
	{
		VSMDepthTexture.Reset();
		FTexturePoolBase::RebuildDSV(Device, InTexture);
		UpdateMemoryStats();
		return;
	}

	VSMDepthTexture = CreateVSMDepthTexture(Device);

	DSVs.clear();
	DSVs.resize(TextureLayerSize);

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.MipSlice = 0;
	dsvDesc.Texture2DArray.ArraySize = 1;

	for (uint32 SliceIndex = 0; SliceIndex < TextureLayerSize; ++SliceIndex)
	{
		dsvDesc.Texture2DArray.FirstArraySlice = SliceIndex;

		HRESULT hr = Device->CreateDepthStencilView(
			VSMDepthTexture.Get(),
			&dsvDesc,
			DSVs[SliceIndex].GetAddressOf()
		);
		assert(SUCCEEDED(hr));
	}

	UpdateMemoryStats();
}

void FTextureAtlasPool::RebuildRTVs(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	RebuildVSMMomentRTVs(Device, InTexture, RTVs);
}

void FTextureAtlasPool::UpdateMemoryStats()
{
	uint64 NewMemory = 0;
	NewMemory += MemoryStats::CalculateTextureMemory(Texture.Get());
	NewMemory += MemoryStats::CalculateTextureMemory(VSMDepthTexture.Get());
	NewMemory += MemoryStats::CalculateTextureMemory(VSMFilteredTexture.Get());
	NewMemory += MemoryStats::CalculateTextureMemory(VSMTempTexture.Get());

	if (NewMemory > TrackedShadowAtlasMemory)
	{
		MemoryStats::AddShadowAtlasMemory(NewMemory - TrackedShadowAtlasMemory);
	}
	else if (TrackedShadowAtlasMemory > NewMemory)
	{
		MemoryStats::SubShadowAtlasMemory(TrackedShadowAtlasMemory - NewMemory);
	}

	TrackedShadowAtlasMemory = NewMemory;
}

void FTextureAtlasPool::OnSetTextureSize()
{
	DebugResource.clear();
}

void FTextureAtlasPool::OnSetTextureLayerSize()
{
	const uint32 TargetCount = GetTextureLayerSize();
	SliceDebugVersions.resize(TargetCount, 1);
}

bool FTextureAtlasPool::CreateDebugResource(SRVResource& OutResource, uint32 Width, uint32 Height)
{
	if (OutResource.Texture &&
		OutResource.RTV &&
		OutResource.SRV &&
		OutResource.Width == Width &&
		OutResource.Height == Height)
	{
		return true;
	}

	ID3D11Device* Device = GetDevice();
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

bool FTextureAtlasPool::CreateDebugPassResources()
{
	ID3D11Device* Device = GetDevice();
	if (!Device)
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

	return DebugConstantBuffer.GetBuffer() &&
		DebugPointClampSampler &&
		DebugRasterizerState &&
		DebugDepthStencilState;
}

void FTextureAtlasPool::MarkSliceDebugDirty(uint32 SliceIndex)
{
	if (SliceIndex < SliceDebugVersions.size())
	{
		++SliceDebugVersions[SliceIndex];
	}
}

void FTextureAtlasPool::MarkSliceDebugDirty(TexturePoolHandleSet* HandleSet)
{
	if (!HandleSet)
	{
		return;
	}

	for (const TexturePoolHandle& Handle : HandleSet->Handles)
	{
		MarkSliceDebugDirty(Handle.ArrayIndex);
	}
}

uint32 FTextureAtlasPool::MakeHandleDebugKey(const TexturePoolHandle& InHandle)
{
	const uint64 PackedHandle = (static_cast<uint64>(InHandle.ArrayIndex) << 32) | InHandle.InternalIndex;
	uint32 Key = HandleDebugKeyPrefix | (static_cast<uint32>(std::hash<uint64>{}(PackedHandle)) & 0x3fffffffu);

	while (true)
	{
		auto Found = DebugResource.find(Key);
		if (Found == DebugResource.end())
		{
			return Key;
		}

		const SRVResource* Resource = Found->second.get();
		if (Resource &&
			Resource->SourceInternalIndex == InHandle.InternalIndex &&
			Resource->SourceArrayIndex == InHandle.ArrayIndex)
		{
			return Key;
		}

		++Key;
		if (Key >= LayerDebugKeyPrefix)
		{
			Key = HandleDebugKeyPrefix;
		}
	}
}
