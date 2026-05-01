#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "TexturePoolTypes.h"
#include "UVManager/Allocator/TexturePoolAllocatorBase.h"
#include "d3d11.h"
#include <memory>
#include <wrl/client.h>

class FTexturePoolBase
{
	template<typename T>
	using TComPtr = Microsoft::WRL::ComPtr<T>;

#pragma region InternalStruct
public:
	using TexturePoolHandle = FTexturePoolHandle;
	using TexturePoolHandleSet = FTexturePoolHandleSet;
	using TexturePoolHandleRequest = FTexturePoolHandleRequest;

protected:
	struct SRVResource
	{
		TComPtr<ID3D11Texture2D> Texture;
		TComPtr<ID3D11RenderTargetView> RTV;
		TComPtr<ID3D11ShaderResourceView> SRV;
		uint32 Width = 0;
		uint32 Height = 0;
		uint64 Version = 0;
		uint32 SourceInternalIndex = static_cast<uint32>(-1);
		uint32 SourceArrayIndex = static_cast<uint32>(-1);
	};
#pragma endregion

public:
	virtual ~FTexturePoolBase();

	virtual void Initialize(
		ID3D11Device* InDevice,
		ID3D11DeviceContext* InDeviceContext,
		uint32 InTextureSize,
		uint32 InAllocatorMinBlockSize = 32);
	uint32 GetTextureSize() const { return TextureSize; }
	uint32 GetAllocatedLayerCount() const { return TextureLayerSize; }
	uint32 GetAllocatorMinBlockSize() const;

	virtual TexturePoolHandleSet* GetTextureHandle(TexturePoolHandleRequest HandleRequest);
	virtual TexturePoolHandleSet* TryGetTextureHandleNoResize(TexturePoolHandleRequest HandleRequest);
	bool CanAllocateTextureHandleSet(const TexturePoolHandleRequest& HandleRequest) const;
	float EstimateAllocationCost(const TexturePoolHandleRequest& HandleRequest) const;
	uint32 GetAllocatorFreeRectCount() const;
	uint64 GetAllocatorTotalFreeArea() const;
	uint64 GetAllocatorLargestFreeRectArea() const;
	float GetAllocatorFragmentationRatio() const;
	void GetAllocatorFreeRects(TArray<FAtlasDebugRect>& OutRects) const;
	void GetAllocatorAllocatedRects(TArray<FAtlasDebugRect>& OutRects) const;
	virtual void ReleaseHandleSet(TexturePoolHandleSet* InHandleSet);
	virtual ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) = 0;
	virtual ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandleSet* InHandleSet) = 0;

protected:
	virtual std::unique_ptr<FTexturePoolAllocatorBase> CreateAllocator() = 0;
	virtual TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) = 0;
	virtual void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) = 0;
	virtual void RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture);
	void MarkDebugDirty(TexturePoolHandleSet* InHandleSet);

	void ResizeLayer() { ResizeLayer(TextureLayerSize * 2); }
	void ResizeLayer(uint32 InNewLayerSize);

	virtual void BroadCastHandlesUnvalid();

	virtual void OnSetTextureSize() = 0;
	virtual void OnSetTextureLayerSize() = 0;

	virtual uint32 GetTextureLayerSize() { return TextureLayerSize; }
	ID3D11Device* GetDevice() const { return Device; }
	ID3D11DeviceContext* GetDeviceContext() const { return DeviceContext; }
	FTexturePoolAllocatorBase* GetAllocator() const { return Allocator.get(); }

private:
	void SetTextureLayerSize(uint32 InTextureLayerSize);
	void SetTextureSize(uint32 InTextureSize);

protected:
	uint32 TextureLayerSize = 1;
	uint32 TextureSize = static_cast<uint32>(-1);

	TComPtr<ID3D11ShaderResourceView> SRV;
	TArray<TComPtr<ID3D11DepthStencilView>> DSVs;
	TMap<uint32, std::unique_ptr<SRVResource>> DebugResource;
	TComPtr<ID3D11Texture2D> Texture;

private:
	std::unique_ptr<FTexturePoolAllocatorBase> Allocator;
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
};
