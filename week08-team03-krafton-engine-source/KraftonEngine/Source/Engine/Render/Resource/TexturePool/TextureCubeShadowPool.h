#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Render/Resource/Buffer.h"
#include <d3d11.h>
#include <wrl/client.h>

struct FPointShadowFaceBasis
{
	FVector Forward;
	FVector Right;
	FVector Up;
};

class FTextureCubeShadowPool final
{
	template<typename T>
	using TComPtr = Microsoft::WRL::ComPtr<T>;

public:
	static constexpr uint32 InvalidCubeIndex = static_cast<uint32>(-1);
	static constexpr uint32 InvalidTierIndex = static_cast<uint32>(-1);
	static constexpr uint32 CubeFaceCount = 6;
	static constexpr uint32 TierCount = 4;

	struct FCubeShadowHandle
	{
		uint32 CubeIndex = InvalidCubeIndex;
		uint32 TierIndex = InvalidTierIndex;

		bool IsValid() const { return CubeIndex != InvalidCubeIndex && TierIndex != InvalidTierIndex; }
	};

	static FTextureCubeShadowPool& Get()
	{
		static FTextureCubeShadowPool Instance;
		return Instance;
	}

	FTextureCubeShadowPool(const FTextureCubeShadowPool&) = delete;
	FTextureCubeShadowPool& operator=(const FTextureCubeShadowPool&) = delete;
	FTextureCubeShadowPool(FTextureCubeShadowPool&&) = delete;
	FTextureCubeShadowPool& operator=(FTextureCubeShadowPool&&) = delete;

	void Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InBaseResolution, uint32 InitialCubeCapacity = 0);
	void Release();
	bool EnsureVSMMode(bool bUseVSM);

	FCubeShadowHandle Allocate(float ResolutionScale = 1.0f);
	void ReleaseHandle(FCubeShadowHandle Handle);

	ID3D11ShaderResourceView* GetSRV(uint32 TierIndex) const;
	ID3D11ShaderResourceView* GetFilteredVSMArraySRV(uint32 TierIndex) const;
	ID3D11ShaderResourceView* GetTempVSMArraySRV(uint32 TierIndex) const;
	ID3D11DepthStencilView* GetFaceDSV(FCubeShadowHandle Handle, uint32 FaceIndex) const;
	ID3D11RenderTargetView* GetFaceVSMRTV(FCubeShadowHandle Handle, uint32 FaceIndex) const;
	ID3D11RenderTargetView* GetTempFaceVSMRTV(FCubeShadowHandle Handle, uint32 FaceIndex) const;
	ID3D11RenderTargetView* GetFilteredFaceVSMRTV(FCubeShadowHandle Handle, uint32 FaceIndex) const;
	ID3D11ShaderResourceView* GetDebugSRV(FCubeShadowHandle Handle);
	static FPointShadowFaceBasis GetFaceBasis(uint32 FaceIndex);

	uint32 GetResolution(FCubeShadowHandle Handle) const;
	uint32 GetResolutionForTier(uint32 TierIndex) const;
	uint32 GetTierIndexForScale(float ResolutionScale) const;
	uint32 GetCapacity(uint32 TierIndex) const;
	uint32 GetAllocatedCount(uint32 TierIndex) const;
	bool IsInitialized() const { return Device != nullptr; }

private:
	FTextureCubeShadowPool() = default;
	~FTextureCubeShadowPool() = default;

	struct FTierPool
	{
		uint32 Resolution = 0;
		uint32 CubeCapacity = 0;
		uint32 AllocatedCount = 0;

		TComPtr<ID3D11Texture2D> Texture;
		TComPtr<ID3D11ShaderResourceView> SRV;
		TComPtr<ID3D11ShaderResourceView> DebugArraySRV;
		TComPtr<ID3D11Texture2D> TempMomentTexture;
		TComPtr<ID3D11Texture2D> FilteredMomentTexture;
		TComPtr<ID3D11ShaderResourceView> TempVSMArraySRV;
		TArray<TComPtr<ID3D11DepthStencilView>> FaceDSVs;
		TArray<TComPtr<ID3D11RenderTargetView>> TempFaceVSMRTVs;
		TArray<TComPtr<ID3D11RenderTargetView>> FilteredFaceVSMRTVs;
		TArray<uint8> AllocationFlags;
		TArray<uint32> FreeCubeIndices;
	};

	struct FDebugPreviewResource
	{
		TComPtr<ID3D11Texture2D> Texture;
		TComPtr<ID3D11RenderTargetView> RTV;
		TComPtr<ID3D11ShaderResourceView> SRV;
		uint32 Width = 0;
		uint32 Height = 0;
	};

	bool CreateDebugResource(FDebugPreviewResource& OutResource, uint32 Width, uint32 Height);
	bool CreateDebugPassResources();
	void Resize(uint32 TierIndex, uint32 NewCubeCapacity);
	bool RebuildResources(uint32 TierIndex, uint32 NewCubeCapacity);
	void UpdateMemoryStats();
	uint32 GetSliceIndex(FCubeShadowHandle Handle, uint32 FaceIndex) const;
	FTierPool* GetTier(uint32 TierIndex);
	const FTierPool* GetTier(uint32 TierIndex) const;

private:
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
	uint32 BaseResolution = 1024;
	bool bVSMMode = false;
	uint64 TrackedShadowCubeMemory = 0;
	FTierPool Tiers[TierCount];
	TMap<uint32, FDebugPreviewResource> DebugResources;
	FConstantBuffer DebugConstantBuffer;
	TComPtr<ID3D11SamplerState> DebugPointClampSampler;
	TComPtr<ID3D11RasterizerState> DebugRasterizerState;
	TComPtr<ID3D11DepthStencilState> DebugDepthStencilState;
};
