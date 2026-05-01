#include "TexturePool.h"
#include "UVManager/TexturePoolAllocator.h"

void FTexturePoolHandleSet::Release()
{
	if (Pool)
	{
		Pool->ReleaseHandleSet(this);
	}
}

FTexturePoolBase::~FTexturePoolBase() = default;

void FTexturePoolBase::Initialize(
	ID3D11Device* InDevice,
	ID3D11DeviceContext* InDeviceContext,
	uint32 InTextureSize,
	uint32 InAllocatorMinBlockSize)
{
	Device = InDevice;
	DeviceContext = InDeviceContext;
	Allocator = CreateAllocator();

	if (Allocator)
	{
		Allocator->Initialize(InTextureSize, 1, InAllocatorMinBlockSize);
	}

	SetTextureSize(InTextureSize);
	SetTextureLayerSize(1);

	Texture = CreateTexture(Device);
	RebuildSRV(Device, Texture.Get());
	RebuildDSV(Device, Texture.Get());
}

FTexturePoolBase::TexturePoolHandleSet* FTexturePoolBase::GetTextureHandle(TexturePoolHandleRequest HandleRequest)
{
	if (!Allocator)
	{
		return nullptr;
	}

	std::unique_ptr<TexturePoolHandleSet> HandleSet = std::make_unique<TexturePoolHandleSet>(
		this,
		Allocator->ReserveHandleSetId());

	for (uint32 Size : HandleRequest.Sizes)
	{
		TexturePoolHandle Handle;
		bool bAllocated = false;
		while (!bAllocated)
		{
			if (Allocator->AllocateHandle(static_cast<float>(Size), Handle))
			{
				bAllocated = true;
				break;
			}

			ResizeLayer();
		}

		HandleSet->Handles.push_back(Handle);
	}

	HandleSet->bIsValid = true;
	HandleSet->AllocatedSizes = HandleRequest.Sizes;
	return Allocator->RegisterHandleSet(std::move(HandleSet));
}

FTexturePoolBase::TexturePoolHandleSet* FTexturePoolBase::TryGetTextureHandleNoResize(TexturePoolHandleRequest HandleRequest)
{
	if (!Allocator)
	{
		return nullptr;
	}

	std::unique_ptr<TexturePoolHandleSet> HandleSet = std::make_unique<TexturePoolHandleSet>(
		this,
		Allocator->ReserveHandleSetId());

	for (uint32 Size : HandleRequest.Sizes)
	{
		TexturePoolHandle Handle;
		if (!Allocator->AllocateHandle(static_cast<float>(Size), Handle))
		{
			for (const TexturePoolHandle& AllocatedHandle : HandleSet->Handles)
			{
				Allocator->ReleaseHandle(AllocatedHandle);
			}
			return nullptr;
		}

		HandleSet->Handles.push_back(Handle);
	}

	HandleSet->bIsValid = true;
	HandleSet->AllocatedSizes = HandleRequest.Sizes;
	return Allocator->RegisterHandleSet(std::move(HandleSet));
}

bool FTexturePoolBase::CanAllocateTextureHandleSet(const TexturePoolHandleRequest& HandleRequest) const
{
	return Allocator ? Allocator->CanAllocateHandleSet(HandleRequest) : false;
}

float FTexturePoolBase::EstimateAllocationCost(const TexturePoolHandleRequest& HandleRequest) const
{
	return Allocator ? Allocator->EstimateAllocationCost(HandleRequest) : 0.0f;
}

uint32 FTexturePoolBase::GetAllocatorMinBlockSize() const
{
	return Allocator ? Allocator->GetMinBlockSize() : 0;
}

uint32 FTexturePoolBase::GetAllocatorFreeRectCount() const
{
	return Allocator ? Allocator->GetFreeRectCount() : 0;
}

uint64 FTexturePoolBase::GetAllocatorTotalFreeArea() const
{
	return Allocator ? Allocator->GetTotalFreeArea() : 0;
}

uint64 FTexturePoolBase::GetAllocatorLargestFreeRectArea() const
{
	return Allocator ? Allocator->GetLargestFreeRectArea() : 0;
}

float FTexturePoolBase::GetAllocatorFragmentationRatio() const
{
	return Allocator ? Allocator->GetFragmentationRatio() : 1.0f;
}

void FTexturePoolBase::GetAllocatorFreeRects(TArray<FAtlasDebugRect>& OutRects) const
{
	if (Allocator)
	{
		Allocator->GetFreeRects(OutRects);
	}
}

void FTexturePoolBase::GetAllocatorAllocatedRects(TArray<FAtlasDebugRect>& OutRects) const
{
	if (Allocator)
	{
		Allocator->GetAllocatedRects(OutRects);
	}
}

void FTexturePoolBase::ReleaseHandleSet(TexturePoolHandleSet* InHandleSet)
{
	if (!Allocator || !InHandleSet)
	{
		return;
	}

	TArray<TexturePoolHandle> Handles = InHandleSet->Handles;
	for (const TexturePoolHandle& Handle : Handles)
	{
		Allocator->ReleaseHandle(Handle);
	}

	DebugResource.erase(InHandleSet->InternalIndex);
	Allocator->UnregisterHandleSet(InHandleSet->InternalIndex);
}

void FTexturePoolBase::RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	DSVs.clear();
	DSVs.resize(TextureLayerSize);

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;

	dsvDesc.Texture2DArray.MipSlice = 0;
	dsvDesc.Texture2DArray.ArraySize = 1;

	const uint32 TotalSlices = GetTextureLayerSize();
	for (UINT slice = 0; slice < TotalSlices; ++slice)
	{
		dsvDesc.Texture2DArray.FirstArraySlice = slice;

		HRESULT hr = Device->CreateDepthStencilView(
			InTexture,
			&dsvDesc,
			DSVs[slice].GetAddressOf()
		);

		assert(SUCCEEDED(hr));
	}
}

void FTexturePoolBase::ResizeLayer(uint32 InNewLayerSize)
{
	const uint32 OldTotalSlices = TextureLayerSize;
	TComPtr<ID3D11Texture2D> OldTexture = Texture;

	SetTextureLayerSize(InNewLayerSize);
	TComPtr<ID3D11Texture2D> NewTexture = CreateTexture(Device);

	for (uint32 slice = 0; slice < OldTotalSlices; ++slice)
	{
		DeviceContext->CopySubresourceRegion(
			NewTexture.Get(),
			slice,
			0, 0, 0,
			OldTexture.Get(),
			slice,
			nullptr
		);
	}

	Texture = std::move(NewTexture);
	RebuildSRV(Device, Texture.Get());
	RebuildDSV(Device, Texture.Get());

	BroadCastHandlesUnvalid();
}

void FTexturePoolBase::BroadCastHandlesUnvalid()
{
	DebugResource.clear();
	if (Allocator)
	{
		Allocator->InvalidateAllHandleSets();
	}
}

void FTexturePoolBase::MarkDebugDirty(TexturePoolHandleSet* InHandleSet)
{
	if (!InHandleSet)
	{
		return;
	}

	++InHandleSet->DebugVersion;
}

void FTexturePoolBase::SetTextureLayerSize(uint32 InTextureLayerSize)
{
	TextureLayerSize = InTextureLayerSize;
	if (Allocator)
	{
		Allocator->SetLayerCount(TextureLayerSize);
	}
	OnSetTextureLayerSize();
}

void FTexturePoolBase::SetTextureSize(uint32 InTextureSize)
{
	TextureSize = InTextureSize;
	if (Allocator)
	{
		Allocator->SetSize(TextureSize);
	}
	OnSetTextureSize();
}
