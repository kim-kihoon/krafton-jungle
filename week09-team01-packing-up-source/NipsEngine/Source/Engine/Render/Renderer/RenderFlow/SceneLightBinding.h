#pragma once

#include "LightCullingPass.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Common/ComPtr.h"
#include "Render/Scene/RenderBus.h"
#include <cstring>
#include <utility>

#include "ShadowAtlasManager.h"

namespace SceneLightBinding
{
	constexpr uint32 SpotShadowInfoRegister = 6;
	constexpr uint32 DirectionalShadowInfoRegister = 7;
    constexpr uint32 PointShadowInfoRegister = 8;	
    constexpr uint32 SpotShadowConstantsRegister = 11;
	constexpr uint32 SpotShadowMapRegister = 12;
	constexpr uint32 DirectionalShadowMapRegister = 13;
    constexpr uint32 PointShadowConstantsRegister = 14;

    constexpr uint32 SpotShadowVSMMapRegister = 15;
    constexpr uint32 DirectionalShadowVSMMapRegister = 16;
    constexpr uint32 PointShadowMapRegister = 17;
    constexpr uint32 PointShadowVSMMapRegister = 18;

	struct FVisibleLightConstants
	{
		uint32 TileCountX = 0;
		uint32 TileCountY = 0;
		uint32 TileSize = 0;
		uint32 MaxLightsPerTile = 0;
		uint32 LightCount = 0;
		float Padding[3] = { 0.0f, 0.0f, 0.0f };
	};

	struct FSpotShadowInfoConstants
	{
		uint32 SpotShadowCount = 0;
        uint32 ShadowFilterType = 0;
		float Padding[2] = { 0.0f, 0.0f };
	};

	struct FDirectionalShadowInfoConstants
	{
		FMatrix LightViewProj[MAX_CASCADE_COUNT];
		FVector4 SplitDistances;
		FVector4 CascadeRadius;

		float ShadowBias = 0.001f;
        float ShadowSlopeBias = 0.5f;
        float ShadowSharpen = 0.0f;
		uint32 bCascadeDebug = 0;

		uint32 bHasShadowMap = 0;
        uint32 ShadowFilterType = 0;
		uint32 ShadowMode = 0;
		float Padding = 0.0f;
	};

    struct FPointShadowInfoConstants
    {
        uint32 PointShadowCount = 0;
        uint32 PointAtlasResolution = 0;
        uint32 ShadowFilterType = 0;
        float Padding = 0.0f;
    };

	inline bool EnsureVisibleLightConstantBuffer(ID3D11Device* Device, TComPtr<ID3D11Buffer>& VisibleLightConstantBuffer)
	{
		if (VisibleLightConstantBuffer)
		{
			return true;
		}

		D3D11_BUFFER_DESC Desc = {};
		Desc.ByteWidth = sizeof(FVisibleLightConstants);
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, VisibleLightConstantBuffer.GetAddressOf()));
	}

	inline bool EnsureSpotShadowInfoConstantBuffer(ID3D11Device* Device, TComPtr<ID3D11Buffer>& SpotShadowInfoConstantBuffer)
	{
		if (SpotShadowInfoConstantBuffer)
		{
			return true;
		}

		D3D11_BUFFER_DESC Desc = {};
		Desc.ByteWidth = sizeof(FSpotShadowInfoConstants);
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, SpotShadowInfoConstantBuffer.GetAddressOf()));
	}

    inline bool EnsurePointShadowInfoConstantBuffer(ID3D11Device* Device, TComPtr<ID3D11Buffer>& PointShadowInfoConstantBuffer)
	{
	    if (PointShadowInfoConstantBuffer)
	    {
	        return true;
	    }

	    D3D11_BUFFER_DESC Desc = {};
	    Desc.ByteWidth = sizeof(FPointShadowInfoConstants);
	    Desc.Usage = D3D11_USAGE_DYNAMIC;
	    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	    return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, PointShadowInfoConstantBuffer.GetAddressOf()));
	}

	inline bool EnsureDirectionalShadowInfoCB(ID3D11Device* Device, TComPtr<ID3D11Buffer>& DirectionalShadowInfoCB)
	{
		if (DirectionalShadowInfoCB)
		{
			return true;
		}

		D3D11_BUFFER_DESC Desc = {};
		Desc.ByteWidth = sizeof(FDirectionalShadowInfoConstants);
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, DirectionalShadowInfoCB.GetAddressOf()));
	}

	inline bool EnsureSpotShadowConstantsBuffer(
		ID3D11Device* Device,
		uint32 RequiredCount,
		TComPtr<ID3D11Buffer>& SpotShadowConstantsBuffer,
		TComPtr<ID3D11ShaderResourceView>& SpotShadowConstantsSRV,
		uint32& SpotShadowConstantsCapacity)
	{
		if (RequiredCount == 0)
		{
			return true;
		}

		if (RequiredCount <= SpotShadowConstantsCapacity && SpotShadowConstantsBuffer && SpotShadowConstantsSRV)
		{
			return true;
		}

		D3D11_BUFFER_DESC BufferDesc = {};
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.ByteWidth = sizeof(FSpotShadowConstants) * RequiredCount;
		BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufferDesc.StructureByteStride = sizeof(FSpotShadowConstants);

		TComPtr<ID3D11Buffer> NewBuffer;
		if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, NewBuffer.GetAddressOf())))
		{
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = RequiredCount;

		TComPtr<ID3D11ShaderResourceView> NewSRV;
		if (FAILED(Device->CreateShaderResourceView(NewBuffer.Get(), &SRVDesc, NewSRV.GetAddressOf())))
		{
			return false;
		}

		SpotShadowConstantsBuffer = std::move(NewBuffer);
		SpotShadowConstantsSRV = std::move(NewSRV);
		SpotShadowConstantsCapacity = RequiredCount;
		return true;
	}

    inline bool EnsurePointShadowConstantsBuffer(
        ID3D11Device* Device,
        uint32 RequiredCount,
        TComPtr<ID3D11Buffer>& PointShadowConstantsBuffer,
        TComPtr<ID3D11ShaderResourceView>& PointShadowConstantsSRV,
        uint32& PointShadowConstantsCapacity)
	{
	    if (RequiredCount == 0)
	    {
	        return true;
	    }

	    if (RequiredCount <= PointShadowConstantsCapacity && PointShadowConstantsBuffer && PointShadowConstantsSRV)
	    {
	        return true;
	    }

	    D3D11_BUFFER_DESC BufferDesc = {};
	    BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	    BufferDesc.ByteWidth = sizeof(FPointShadowConstants) * RequiredCount;
	    BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	    BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	    BufferDesc.StructureByteStride = sizeof(FPointShadowConstants);

	    TComPtr<ID3D11Buffer> NewBuffer;
	    if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, NewBuffer.GetAddressOf())))
	    {
	        return false;
	    }

	    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	    SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	    SRVDesc.Buffer.FirstElement = 0;
	    SRVDesc.Buffer.NumElements = RequiredCount;

	    TComPtr<ID3D11ShaderResourceView> NewSRV;
	    if (FAILED(Device->CreateShaderResourceView(NewBuffer.Get(), &SRVDesc, NewSRV.GetAddressOf())))
	    {
	        return false;
	    }

	    PointShadowConstantsBuffer = std::move(NewBuffer);
	    PointShadowConstantsSRV = std::move(NewSRV);
	    PointShadowConstantsCapacity = RequiredCount;
	    return true;
	}

	inline void BindSpotShadowResources(
		const FRenderPassContext* Context,
		TComPtr<ID3D11Buffer>& SpotShadowInfoConstantBuffer,
		TComPtr<ID3D11Buffer>& SpotShadowConstantsBuffer,
		TComPtr<ID3D11ShaderResourceView>& SpotShadowConstantsSRV,
		uint32& SpotShadowConstantsCapacity)
	{
		if (Context == nullptr || Context->Device == nullptr || Context->DeviceContext == nullptr)
		{
			return;
		}

		uint32 SpotShadowCount = 0;
		ID3D11ShaderResourceView* SpotShadowMapSRV = nullptr;
        ID3D11ShaderResourceView* SpotShadowMapVSMSRV = nullptr;
		if (Context->RenderTargets != nullptr)
		{
			SpotShadowCount = Context->RenderTargets->SpotShadowCount;
			SpotShadowMapSRV = Context->RenderTargets->SpotShadowSRV;
            SpotShadowMapVSMSRV = Context->RenderTargets->SpotShadowVSMSRV;
		}

		const TArray<FSpotShadowConstants>* SpotShadows = nullptr;
		if (Context->RenderBus != nullptr)
		{
			SpotShadows = &Context->RenderBus->GetCastShadowSpotLights();
			if (SpotShadowCount > SpotShadows->size())
			{
				SpotShadowCount = static_cast<uint32>(SpotShadows->size());
			}
		}
		else
		{
			SpotShadowCount = 0;
		}

		if (!EnsureSpotShadowInfoConstantBuffer(Context->Device, SpotShadowInfoConstantBuffer))
		{
			return;
		}

		if (!EnsureSpotShadowConstantsBuffer(
			Context->Device,
			SpotShadowCount,
			SpotShadowConstantsBuffer,
			SpotShadowConstantsSRV,
			SpotShadowConstantsCapacity))
		{
			SpotShadowCount = 0;
		}

		FSpotShadowInfoConstants InfoConstants = {};
		InfoConstants.SpotShadowCount = SpotShadowCount;
        InfoConstants.ShadowFilterType = (uint32)Context->RenderBus->GetShadowFilterType();

		D3D11_MAPPED_SUBRESOURCE MappedInfo = {};
		if (SUCCEEDED(Context->DeviceContext->Map(SpotShadowInfoConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedInfo)))
		{
			std::memcpy(MappedInfo.pData, &InfoConstants, sizeof(InfoConstants));
			Context->DeviceContext->Unmap(SpotShadowInfoConstantBuffer.Get(), 0);
		}

		if (SpotShadowCount > 0 && SpotShadows != nullptr && SpotShadowConstantsBuffer)
		{
			D3D11_MAPPED_SUBRESOURCE MappedShadows = {};
			if (SUCCEEDED(Context->DeviceContext->Map(SpotShadowConstantsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedShadows)))
			{
				std::memcpy(MappedShadows.pData, SpotShadows->data(), sizeof(FSpotShadowConstants) * SpotShadowCount);
				Context->DeviceContext->Unmap(SpotShadowConstantsBuffer.Get(), 0);
			}
		}

		ID3D11Buffer* InfoCBuffer = SpotShadowInfoConstantBuffer.Get();
		Context->DeviceContext->PSSetConstantBuffers(SpotShadowInfoRegister, 1, &InfoCBuffer);
		Context->DeviceContext->VSSetConstantBuffers(SpotShadowInfoRegister, 1, &InfoCBuffer);

		ID3D11ShaderResourceView* ShadowConstantsSRV = SpotShadowCount > 0 ? SpotShadowConstantsSRV.Get() : nullptr;
		Context->DeviceContext->PSSetShaderResources(SpotShadowConstantsRegister, 1, &ShadowConstantsSRV);
		Context->DeviceContext->VSSetShaderResources(SpotShadowConstantsRegister, 1, &ShadowConstantsSRV);

		ID3D11ShaderResourceView* ShadowMapSRV = SpotShadowCount > 0 ? SpotShadowMapSRV : nullptr;
		Context->DeviceContext->PSSetShaderResources(SpotShadowMapRegister, 1, &ShadowMapSRV);

		ID3D11ShaderResourceView* ShadowMapVSMSRV = SpotShadowCount > 0 ? SpotShadowMapVSMSRV : nullptr;
        Context->DeviceContext->PSSetShaderResources(SpotShadowVSMMapRegister, 1, &ShadowMapVSMSRV);
	}

	inline void BindDirectionalShadowResources(const FRenderPassContext* Context, TComPtr<ID3D11Buffer>& DirectionalShadowInfoCB)
	{
		if (Context == nullptr || Context->Device == nullptr || Context->DeviceContext == nullptr)
		{
			return;
		}

		if (!EnsureDirectionalShadowInfoCB(Context->Device, DirectionalShadowInfoCB))
		{
			return;
		}

		const FDirectionalShadowConstants* DirShadow = nullptr;
		if (Context->RenderBus != nullptr)
		{
			DirShadow = Context->RenderBus->GetDirectionalShadow();
		}

		ID3D11ShaderResourceView* ShadowMapSRV = nullptr;
        ID3D11ShaderResourceView* ShadowMapVSMSRV = nullptr;
		if (Context->RenderTargets != nullptr && DirShadow != nullptr)
		{
			ShadowMapSRV = Context->RenderTargets->DirectionalShadowSRV;
            ShadowMapVSMSRV = Context->RenderTargets->DirectionalShadowVSMSRV;
		}

		static_assert(sizeof(FDirectionalShadowInfoConstants) == sizeof(FDirectionalShadowConstants),
			"FDirectionalShadowInfoConstants and FDirectionalShadowConstants layout mismatch");

		FDirectionalShadowInfoConstants InfoConstants = {};
		if (DirShadow != nullptr)
		{
			for(int i = 0; i < MAX_CASCADE_COUNT; ++i)
			{
				InfoConstants.LightViewProj[i] = DirShadow->LightViewProj[i];
			}

			InfoConstants.SplitDistances = DirShadow->SplitDistances;
			InfoConstants.CascadeRadius = DirShadow->CascadeRadius;
			InfoConstants.ShadowBias = DirShadow->ShadowBias;
            InfoConstants.ShadowSlopeBias = DirShadow->ShadowSlopeBias;
            InfoConstants.ShadowSharpen = DirShadow->ShadowSharpen;
		    InfoConstants.bCascadeDebug = DirShadow->bCascadeDebug;
		    InfoConstants.ShadowMode = DirShadow->ShadowMode;
		}

		InfoConstants.bHasShadowMap = (ShadowMapSRV != nullptr) ? 1u : 0u;
        InfoConstants.ShadowFilterType = (uint32)Context->RenderBus->GetShadowFilterType();

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(Context->DeviceContext->Map(DirectionalShadowInfoCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			std::memcpy(Mapped.pData, &InfoConstants, sizeof(InfoConstants));
			Context->DeviceContext->Unmap(DirectionalShadowInfoCB.Get(), 0);
		}

		ID3D11Buffer* InfoCBuffer = DirectionalShadowInfoCB.Get();
		Context->DeviceContext->PSSetConstantBuffers(DirectionalShadowInfoRegister, 1, &InfoCBuffer);
		Context->DeviceContext->VSSetConstantBuffers(DirectionalShadowInfoRegister, 1, &InfoCBuffer);

		Context->DeviceContext->PSSetShaderResources(DirectionalShadowMapRegister, 1, &ShadowMapSRV);
		Context->DeviceContext->VSSetShaderResources(DirectionalShadowMapRegister, 1, &ShadowMapSRV);

		Context->DeviceContext->PSSetShaderResources(DirectionalShadowVSMMapRegister, 1, &ShadowMapVSMSRV);
	}

    inline void BindPointShadowResources(
		const FRenderPassContext* Context,
		TComPtr<ID3D11Buffer>& PointShadowInfoConstantBuffer,
		TComPtr<ID3D11Buffer>& PointShadowConstantsBuffer,
		TComPtr<ID3D11ShaderResourceView>& PointShadowConstantsSRV,
		uint32& PointShadowConstantsCapacity)
	{
		if (Context == nullptr || Context->Device == nullptr || Context->DeviceContext == nullptr)
		{
			return;
		}

		uint32 PointShadowCount = 0;
		ID3D11ShaderResourceView* PointShadowMapSRV = nullptr;
        ID3D11ShaderResourceView* PointShadowMapVSMSRV = nullptr;
		if (Context->RenderTargets != nullptr)
		{
			PointShadowCount = Context->RenderTargets->PointShadowCount;
			PointShadowMapSRV = Context->RenderTargets->PointShadowSRV;
            PointShadowMapVSMSRV = Context->RenderTargets->PointShadowVSMSRV;
		}

		const TArray<FPointShadowConstants>* PointShadows = nullptr;
		if (Context->RenderBus != nullptr)
		{
			PointShadows = &Context->RenderBus->GetCastShadowPointLights();
			if (PointShadowCount > PointShadows->size())
			{
				PointShadowCount = static_cast<uint32>(PointShadows->size());
			}
		}
		else
		{
			PointShadowCount = 0;
		}

		if (!EnsurePointShadowInfoConstantBuffer(Context->Device, PointShadowInfoConstantBuffer))
		{
			return;
		}

		if (!EnsurePointShadowConstantsBuffer(
			Context->Device,
			PointShadowCount,
			PointShadowConstantsBuffer,
			PointShadowConstantsSRV,
			PointShadowConstantsCapacity))
		{
			PointShadowCount = 0;
		}

		FPointShadowInfoConstants InfoConstants = {};
		InfoConstants.PointShadowCount = PointShadowCount;
	    InfoConstants.PointAtlasResolution = FShadowAtlasManager::PointAtlasResolution;
        InfoConstants.ShadowFilterType = (uint32)Context->RenderBus->GetShadowFilterType();

		D3D11_MAPPED_SUBRESOURCE MappedInfo = {};
		if (SUCCEEDED(Context->DeviceContext->Map(PointShadowInfoConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedInfo)))
		{
			std::memcpy(MappedInfo.pData, &InfoConstants, sizeof(InfoConstants));
			Context->DeviceContext->Unmap(PointShadowInfoConstantBuffer.Get(), 0);
		}

		if (PointShadowCount > 0 && PointShadows != nullptr && PointShadowConstantsBuffer)
		{
			D3D11_MAPPED_SUBRESOURCE MappedShadows = {};
			if (SUCCEEDED(Context->DeviceContext->Map(PointShadowConstantsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedShadows)))
			{
				std::memcpy(MappedShadows.pData, PointShadows->data(), sizeof(FPointShadowConstants) * PointShadowCount);
				Context->DeviceContext->Unmap(PointShadowConstantsBuffer.Get(), 0);
			}
		}

		ID3D11Buffer* InfoCBuffer = PointShadowInfoConstantBuffer.Get();
		Context->DeviceContext->PSSetConstantBuffers(PointShadowInfoRegister, 1, &InfoCBuffer);
		Context->DeviceContext->VSSetConstantBuffers(PointShadowInfoRegister, 1, &InfoCBuffer);

		ID3D11ShaderResourceView* ShadowConstantsSRV = PointShadowCount > 0 ? PointShadowConstantsSRV.Get() : nullptr;
		Context->DeviceContext->PSSetShaderResources(PointShadowConstantsRegister, 1, &ShadowConstantsSRV);
		Context->DeviceContext->VSSetShaderResources(PointShadowConstantsRegister, 1, &ShadowConstantsSRV);

		ID3D11ShaderResourceView* ShadowMapSRV = PointShadowCount > 0 ? PointShadowMapSRV : nullptr;
		Context->DeviceContext->PSSetShaderResources(PointShadowMapRegister, 1, &ShadowMapSRV);

        ID3D11ShaderResourceView* ShadowMapVSMSRV = PointShadowCount > 0 ? PointShadowMapVSMSRV : nullptr;
        Context->DeviceContext->PSSetShaderResources(PointShadowVSMMapRegister, 1, &ShadowMapVSMSRV);
	}

	inline void BindResources(const FRenderPassContext* Context, TComPtr<ID3D11Buffer>& VisibleLightConstantBuffer)
	{
		if (Context == nullptr || Context->Device == nullptr || Context->DeviceContext == nullptr)
		{
			return;
		}

		const FLightCullingOutputs& Outputs = FLightCullingPass::GetOutputs();
		if (!EnsureVisibleLightConstantBuffer(Context->Device, VisibleLightConstantBuffer))
		{
			return;
		}

		FVisibleLightConstants Constants = {};
		Constants.TileCountX = Outputs.TileCountX;
		Constants.TileCountY = Outputs.TileCountY;
		Constants.TileSize = Outputs.TileSize;
		Constants.MaxLightsPerTile = Outputs.MaxLightsPerTile;
		Constants.LightCount = Outputs.LightCount;

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (SUCCEEDED(Context->DeviceContext->Map(VisibleLightConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			std::memcpy(Mapped.pData, &Constants, sizeof(Constants));
			Context->DeviceContext->Unmap(VisibleLightConstantBuffer.Get(), 0);
		}

		ID3D11ShaderResourceView* SRVs[3] =
		{
			Outputs.LightBufferSRV,
			Outputs.TileLightCountSRV,
			Outputs.TileLightIndexSRV
		};
		
		Context->DeviceContext->PSSetShaderResources(8, 3, SRVs);
		ID3D11Buffer* CBuffer = VisibleLightConstantBuffer.Get();
		Context->DeviceContext->PSSetConstantBuffers(4, 1, &CBuffer);

		Context->DeviceContext->VSSetShaderResources(8, 3, SRVs);
        Context->DeviceContext->VSSetConstantBuffers(4, 1, &CBuffer);
	}

	inline void BindResources(
		const FRenderPassContext* Context,
		TComPtr<ID3D11Buffer>& VisibleLightConstantBuffer,
		TComPtr<ID3D11Buffer>& DirectionalShadowConstantBuffer,
		TComPtr<ID3D11Buffer>& SpotShadowInfoConstantBuffer,
		TComPtr<ID3D11Buffer>& SpotShadowConstantsBuffer,
		TComPtr<ID3D11ShaderResourceView>& SpotShadowConstantsSRV,
		uint32& SpotShadowConstantsCapacity,
		TComPtr<ID3D11Buffer>& PointShadowInfoConstantBuffer,
        TComPtr<ID3D11Buffer>& PointShadowConstantsBuffer,
        TComPtr<ID3D11ShaderResourceView>& PointShadowConstantsSRV,
        uint32& PointShadowConstantsCapacity)
	{
		BindResources(Context, VisibleLightConstantBuffer);
		BindDirectionalShadowResources(Context, DirectionalShadowConstantBuffer);
		BindSpotShadowResources(Context, SpotShadowInfoConstantBuffer, SpotShadowConstantsBuffer, SpotShadowConstantsSRV, SpotShadowConstantsCapacity);
		BindPointShadowResources(Context, PointShadowInfoConstantBuffer, PointShadowConstantsBuffer, PointShadowConstantsSRV, PointShadowConstantsCapacity);
	}

	inline void UnbindResources(ID3D11DeviceContext* DeviceContext)
	{
		if (DeviceContext == nullptr)
		{
			return;
		}

		ID3D11ShaderResourceView* NullSRVs[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		DeviceContext->PSSetShaderResources(8, 6, NullSRVs);
		DeviceContext->VSSetShaderResources(8, 6, NullSRVs);

		ID3D11Buffer* NullCB = nullptr;
		DeviceContext->PSSetConstantBuffers(4, 1, &NullCB);
		DeviceContext->VSSetConstantBuffers(4, 1, &NullCB);
		DeviceContext->PSSetConstantBuffers(SpotShadowInfoRegister, 1, &NullCB);
		DeviceContext->VSSetConstantBuffers(SpotShadowInfoRegister, 1, &NullCB);
	    DeviceContext->PSSetConstantBuffers(PointShadowInfoRegister, 1, &NullCB);
	    DeviceContext->VSSetConstantBuffers(PointShadowInfoRegister, 1, &NullCB);
		DeviceContext->PSSetConstantBuffers(DirectionalShadowInfoRegister, 1, &NullCB);
		DeviceContext->VSSetConstantBuffers(DirectionalShadowInfoRegister, 1, &NullCB);
	}
}
