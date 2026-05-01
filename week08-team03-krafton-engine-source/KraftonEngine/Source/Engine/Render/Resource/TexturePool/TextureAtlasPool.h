#pragma once

#include "TexturePool.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/ViewTypes.h"
#include "Render/Resource/TexturePool/UVManager/TexturePoolAllocator.h"

class FTextureAtlasPool final : public FTexturePoolBase
{
	template<typename T>
	using TComPtr = Microsoft::WRL::ComPtr<T>;

public:
	FTextureAtlasPool() = default;
	~FTextureAtlasPool();
	FTextureAtlasPool(const FTextureAtlasPool&) = delete;
	FTextureAtlasPool& operator=(const FTextureAtlasPool&) = delete;
	FTextureAtlasPool(FTextureAtlasPool&&) = delete;
	FTextureAtlasPool& operator=(FTextureAtlasPool&&) = delete;

	void Release();
	void Initialize(
		ID3D11Device* InDevice,
		ID3D11DeviceContext* InDeviceContext,
		uint32 InTextureSize,
		uint32 InAllocatorMinBlockSize = 256) override;
	void EnsureAtlasMode(EShadowFilterMode InFilterMode);

	FAtlasUV GetAtlasUV(const TexturePoolHandle& InHandle)
	{
		FTexturePoolAllocatorBase* Allocator = GetAllocator();
		return Allocator ? Allocator->GetAtlasUV(InHandle) : FAtlasUV{};
	}
	TArray<FAtlasUV> GetAtlasUVArray(const TexturePoolHandleSet* InHandleSet);

	ID3D11ShaderResourceView* GetSRV() { return SRV.Get(); }
	ID3D11ShaderResourceView* GetSliceSRV(uint32 SliceIndex) { return SliceIndex < static_cast<uint32>(SliceSRVs.size()) ? SliceSRVs[SliceIndex].Get() : nullptr; }
	ID3D11ShaderResourceView* GetRawSRV() { return SRV.Get(); }
	ID3D11ShaderResourceView* GetFilteredSRV() { return VSMFilteredSRV.Get(); }
	ID3D11ShaderResourceView* GetTempSRV() { return VSMTempSRV.Get(); }
	TArray<ID3D11ShaderResourceView*> GetSliceSRVs(TexturePoolHandleSet* HandleSet);
	ID3D11RenderTargetView* GetFilteredRTV(uint32 SliceIndex) { return SliceIndex < static_cast<uint32>(VSMFilteredRTVs.size()) ? VSMFilteredRTVs[SliceIndex].Get() : nullptr; }
	ID3D11RenderTargetView* GetTempRTV(uint32 SliceIndex) { return SliceIndex < static_cast<uint32>(VSMTempRTVs.size()) ? VSMTempRTVs[SliceIndex].Get() : nullptr; }
	TArray<ID3D11DepthStencilView*> GetDSVs(TexturePoolHandleSet* HandleSet);
	TArray<ID3D11RenderTargetView*> GetRTVs(TexturePoolHandleSet* HandleSet);
	TArray<ID3D11RenderTargetView*> GetFilteredRTVs(TexturePoolHandleSet* HandleSet);
	TArray<ID3D11RenderTargetView*> GetTempRTVs(TexturePoolHandleSet* HandleSet);

	ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) override;
	ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandleSet* InHandleSet) override;
	ID3D11ShaderResourceView* GetDebugLayerSRV(uint32 SliceIndex);
	bool IsVSMMode() const { return CurrentFilterMode == EShadowFilterMode::VSM; }

protected:
	std::unique_ptr<FTexturePoolAllocatorBase> CreateAllocator() override;
	TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) override;
	void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;
	void RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;
	void RebuildRTVs(ID3D11Device* Device, ID3D11Texture2D* InTexture);

	void OnSetTextureSize() override;
	void OnSetTextureLayerSize() override;

private:
	bool CreateDebugResource(SRVResource& OutResource, uint32 Width, uint32 Height);
	bool CreateDebugPassResources();

	void MarkSliceDebugDirty(uint32 SliceIndex);
	void MarkSliceDebugDirty(TexturePoolHandleSet* HandleSet);

	uint32 MakeHandleDebugKey(const TexturePoolHandle& InHandle);
	void RecreateAtlasResources();
	TComPtr<ID3D11Texture2D> CreateVSMDepthTexture(ID3D11Device* Device);
	TComPtr<ID3D11Texture2D> CreateVSMMomentTexture(ID3D11Device* Device);
	void RebuildVSMMomentSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture, TComPtr<ID3D11ShaderResourceView>& OutSRV);
	void RebuildSliceSRVs(ID3D11Device* Device, ID3D11Texture2D* InTexture, DXGI_FORMAT Format, TArray<TComPtr<ID3D11ShaderResourceView>>& OutSRVs);
	void RebuildVSMMomentRTVs(ID3D11Device* Device, ID3D11Texture2D* InTexture, TArray<TComPtr<ID3D11RenderTargetView>>& OutRTVs);
	void RebuildVSMBlurResources(ID3D11Device* Device);
	void UpdateMemoryStats();

private:
	EShadowFilterMode CurrentFilterMode = EShadowFilterMode::PCF;
	TComPtr<ID3D11Texture2D> VSMDepthTexture;
	TComPtr<ID3D11Texture2D> VSMFilteredTexture;
	TComPtr<ID3D11Texture2D> VSMTempTexture;
	TComPtr<ID3D11ShaderResourceView> VSMFilteredSRV;
	TComPtr<ID3D11ShaderResourceView> VSMTempSRV;
	TArray<TComPtr<ID3D11ShaderResourceView>> SliceSRVs;
	TArray<TComPtr<ID3D11RenderTargetView>> RTVs;
	TArray<TComPtr<ID3D11RenderTargetView>> VSMFilteredRTVs;
	TArray<TComPtr<ID3D11RenderTargetView>> VSMTempRTVs;
	TArray<uint64> SliceDebugVersions;
	FConstantBuffer DebugConstantBuffer;
	TComPtr<ID3D11SamplerState> DebugPointClampSampler;
	TComPtr<ID3D11RasterizerState> DebugRasterizerState;
	TComPtr<ID3D11DepthStencilState> DebugDepthStencilState;
	uint64 TrackedShadowAtlasMemory = 0;
};
