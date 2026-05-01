#include "RenderResources.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Resource/TexturePool/TextureAtlasPool.h"
#include "Render/Resource/TexturePool/TextureCubeShadowPool.h"
#include "Materials/MaterialManager.h"
#include "Render/Pipeline/ForwardLightData.h"
#include "Render/Pipeline/FrameContext.h"
#include "Render/Proxy/FScene.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Engine/Runtime/Engine.h"
#include "Core/Log.h"
#include "Profiling/Timer.h"
#include <cstring>
#include "Settings/EditorSettings.h"

//헤더에 선언된 함수들이 이동해서 변경량이 많아 보일 뿐임
//추가된 부분은 FShadowInfoResource밖에 없고 나머지 녀석들 변경점은 Structured Buffer에 넘길 내용 채워주기 추가됨

namespace
{
	template <typename T>
	void UpdateStructuredBuffer(
		ID3D11Device* InDevice,
		ID3D11DeviceContext* InDeviceContext,
		const TArray<T>& Data,
		uint32& InOutMaxCount,
		ID3D11Buffer*& InOutBuffer,
		ID3D11ShaderResourceView*& InOutSRV)
	{
		const uint32 RequiredCount = Data.empty() ? 1u : static_cast<uint32>(Data.size());
		if (InOutMaxCount < RequiredCount)
		{
			if (InOutBuffer) { InOutBuffer->Release(); InOutBuffer = nullptr; }
			if (InOutSRV) { InOutSRV->Release(); InOutSRV = nullptr; }

			uint32 NewCount = InOutMaxCount > 0 ? InOutMaxCount : 1u;
			while (NewCount < RequiredCount)
			{
				NewCount *= 2;
			}

			D3D11_BUFFER_DESC Desc = {};
			Desc.ByteWidth = sizeof(T) * NewCount;
			Desc.Usage = D3D11_USAGE_DYNAMIC;
			Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			Desc.StructureByteStride = sizeof(T);
			InDevice->CreateBuffer(&Desc, nullptr, &InOutBuffer);

			D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
			SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
			SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			SRVDesc.Buffer.NumElements = NewCount;
			InDevice->CreateShaderResourceView(InOutBuffer, &SRVDesc, &InOutSRV);

			InOutMaxCount = NewCount;
		}

		if (!InOutBuffer)
		{
			return;
		}

		if (!InDeviceContext)
		{
			return;
		}

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (FAILED(InDeviceContext->Map(InOutBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			return;
		}

		if (!Data.empty())
		{
			memcpy(Mapped.pData, Data.data(), sizeof(T) * Data.size());
		}
		else
		{
			memset(Mapped.pData, 0, sizeof(T));
		}

		InDeviceContext->Unmap(InOutBuffer, 0);
	}
}

void FLightingResource::Create(ID3D11Device* InDevice, uint32 InMaxLightCount)
{
	MaxLightCount = 0;
	UpdateStructuredBuffer<FLightInfo>(InDevice, nullptr, {}, MaxLightCount, LightBuffer, LightBufferSRV);
	if (InMaxLightCount > 1)
	{
		Release();

		D3D11_BUFFER_DESC Desc = {};
		Desc.ByteWidth = sizeof(FLightInfo) * InMaxLightCount;
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		Desc.StructureByteStride = sizeof(FLightInfo);
		InDevice->CreateBuffer(&Desc, nullptr, &LightBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.NumElements = InMaxLightCount;
		InDevice->CreateShaderResourceView(LightBuffer, &SRVDesc, &LightBufferSRV);
		MaxLightCount = InMaxLightCount;
	}
}

void FLightingResource::Update(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, const TArray<FLightInfo>& LightInfos)
{
	UpdateStructuredBuffer(InDevice, InDeviceContext, LightInfos, MaxLightCount, LightBuffer, LightBufferSRV);
}

void FLightingResource::Release()
{
	if (LightBuffer) { LightBuffer->Release(); LightBuffer = nullptr; }
	if (LightBufferSRV) { LightBufferSRV->Release(); LightBufferSRV = nullptr; }
	MaxLightCount = 0;
}

void FShadowInfoResource::Create(ID3D11Device* InDevice, uint32 InMaxCount)
{
	MaxCount = 0;
	UpdateStructuredBuffer<FShadowInfo>(InDevice, nullptr, {}, MaxCount, Buffer, SRV);
	if (InMaxCount > 1)
	{
		Release();

		D3D11_BUFFER_DESC Desc = {};
		Desc.ByteWidth = sizeof(FShadowInfo) * InMaxCount;
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		Desc.StructureByteStride = sizeof(FShadowInfo);
		InDevice->CreateBuffer(&Desc, nullptr, &Buffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.NumElements = InMaxCount;
		InDevice->CreateShaderResourceView(Buffer, &SRVDesc, &SRV);
		MaxCount = InMaxCount;
	}
}

void FShadowInfoResource::Update(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, const TArray<FShadowInfo>& InShadowInfos)
{
	UpdateStructuredBuffer(InDevice, InDeviceContext, InShadowInfos, MaxCount, Buffer, SRV);
}

void FShadowInfoResource::Release()
{
	if (Buffer) { Buffer->Release(); Buffer = nullptr; }
	if (SRV) { SRV->Release(); SRV = nullptr; }
	MaxCount = 0;
}

void FTileCullingResource::Create(ID3D11Device* Dev, uint32 InTileCountX, uint32 InTileCountY)
{
	Release();
	TileCountX = InTileCountX;
	TileCountY = InTileCountY;
	const uint32 NumTiles = TileCountX * TileCountY;

	auto MakeStructured = [&](
		uint32 ElemCount, uint32 Stride,
		ID3D11Buffer** OutBuf,
		ID3D11UnorderedAccessView** OutUAV,
		ID3D11ShaderResourceView** OutSRV)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = ElemCount * Stride;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = Stride;
		Dev->CreateBuffer(&bd, nullptr, OutBuf);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
		uavd.Format = DXGI_FORMAT_UNKNOWN;
		uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavd.Buffer.NumElements = ElemCount;
		Dev->CreateUnorderedAccessView(*OutBuf, &uavd, OutUAV);

		if (OutSRV)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
			srvd.Format = DXGI_FORMAT_UNKNOWN;
			srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvd.Buffer.NumElements = ElemCount;
			Dev->CreateShaderResourceView(*OutBuf, &srvd, OutSRV);
		}
	};

	MakeStructured(ETileCulling::MaxLightsPerTile * NumTiles, sizeof(uint32),
		&IndicesBuffer, &IndicesUAV, &IndicesSRV);
	MakeStructured(NumTiles, sizeof(uint32) * 2,
		&GridBuffer, &GridUAV, &GridSRV);
	MakeStructured(1, sizeof(uint32),
		&CounterBuffer, &CounterUAV, nullptr);
}

void FTileCullingResource::Release()
{
	if (IndicesSRV) { IndicesSRV->Release(); IndicesSRV = nullptr; }
	if (GridSRV) { GridSRV->Release(); GridSRV = nullptr; }
	if (IndicesUAV) { IndicesUAV->Release(); IndicesUAV = nullptr; }
	if (GridUAV) { GridUAV->Release(); GridUAV = nullptr; }
	if (CounterUAV) { CounterUAV->Release(); CounterUAV = nullptr; }
	if (IndicesBuffer) { IndicesBuffer->Release(); IndicesBuffer = nullptr; }
	if (GridBuffer) { GridBuffer->Release(); GridBuffer = nullptr; }
	if (CounterBuffer) { CounterBuffer->Release(); CounterBuffer = nullptr; }
	TileCountX = TileCountY = 0;
}

void FSystemResources::Create(ID3D11Device* InDevice)
{
	FrameBuffer.Create(InDevice, sizeof(FFrameConstants));
	LightingConstantBuffer.Create(InDevice, sizeof(FLightingCBData));
	ForwardLights.Create(InDevice, 32);
	ShadowInfos.Create(InDevice, 16);

	RasterizerStateManager.Create(InDevice);
	DepthStencilStateManager.Create(InDevice);
	BlendStateManager.Create(InDevice);
	SamplerStateManager.Create(InDevice);

	FMaterialManager::Get().Initialize(InDevice);
}

void FSystemResources::Release()
{
	SamplerStateManager.Release();
	BlendStateManager.Release();
	DepthStencilStateManager.Release();
	RasterizerStateManager.Release();

	FrameBuffer.Release();
	LightingConstantBuffer.Release();
	ForwardLights.Release();
	ShadowInfos.Release();
	TileCullingResource.Release();
}

void FSystemResources::UpdateFrameBuffer(FD3DDevice& Device, const FFrameContext& Frame)
{
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();

	FFrameConstants frameConstantData = {};
	frameConstantData.View = Frame.View;
	frameConstantData.Projection = Frame.Proj;
	frameConstantData.InvProj = Frame.Proj.GetInverse();
	frameConstantData.InvViewProj = (Frame.View * Frame.Proj).GetInverse();
	frameConstantData.bIsWireframe = (Frame.RenderOptions.ViewMode == EViewMode::Wireframe);
	frameConstantData.WireframeColor = Frame.WireframeColor;
	frameConstantData.CameraWorldPos = Frame.CameraPosition;

	if (GEngine && GEngine->GetTimer())
	{
		frameConstantData.Time = static_cast<float>(GEngine->GetTimer()->GetTotalTime());
	}

	FrameBuffer.Update(Ctx, &frameConstantData, sizeof(FFrameConstants));
	ID3D11Buffer* b0 = FrameBuffer.GetBuffer();
	Ctx->VSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
	Ctx->PSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
	Ctx->CSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
}

void FSystemResources::UpdateLightBuffer(FD3DDevice& Device, const FScene& Scene, const FFrameContext& Frame,
	const FClusterCullingState* ClusterState, const FShadowFrameBindingData* ShadowBindingData)
{
	ID3D11Device* Dev = Device.GetDevice();
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();

	FLightingCBData GlobalLightingData = {};
	GlobalLightingData.Directional.ShadowIndex = ShadowBindingData ? ShadowBindingData->DirectionalShadowIndex : -1;

	const FSceneEnvironment& Env = Scene.GetEnvironment();
	if (Env.HasGlobalAmbientLight())
	{
		FGlobalAmbientLightParams DirLightParams = Env.GetGlobalAmbientLightParams();
		GlobalLightingData.Ambient.Intensity = DirLightParams.Intensity;
		GlobalLightingData.Ambient.Color = DirLightParams.LightColor;
	}
	else
	{
		GlobalLightingData.Ambient.Intensity = 0.15f;
		GlobalLightingData.Ambient.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if (Env.HasGlobalDirectionalLight())
	{
		FGlobalDirectionalLightParams DirLightParams = Env.GetGlobalDirectionalLightParams();
		GlobalLightingData.Directional.Intensity = DirLightParams.Intensity;
		GlobalLightingData.Directional.Color = DirLightParams.LightColor;
		GlobalLightingData.Directional.Direction = DirLightParams.Direction;
	}

	const uint32 NumPointLights = Env.GetNumPointLights();
	const uint32 NumSpotLights = Env.GetNumSpotLights();

	TArray<FLightInfo> Infos;
	Infos.reserve(NumPointLights + NumSpotLights);
	uint32 NumUploadedPointLights = 0;
	uint32 NumUploadedSpotLights = 0;
	for (uint32 i = 0; i < NumPointLights; ++i)
	{
		const UPointLightComponent* PointLight = Env.GetPointLightOwner(i);
		const bool bRequiresShadowResource = PointLight && PointLight->IsCastShadow();
		const bool bHasShadowResource = ShadowBindingData
			&& i < ShadowBindingData->ActivePointLightMask.size()
			&& ShadowBindingData->ActivePointLightMask[i] != 0;
		if (ShadowBindingData && bRequiresShadowResource && !bHasShadowResource)
		{
			continue;
		}

		FLightInfo Info = Env.GetPointLight(i).ToLightInfo();
		if (ShadowBindingData && i < ShadowBindingData->PointLightShadowIndices.size())
		{
			Info.ShadowIndex = ShadowBindingData->PointLightShadowIndices[i];
		}
		Infos.push_back(Info);
		++NumUploadedPointLights;
	}
	for (uint32 i = 0; i < NumSpotLights; ++i)
	{
		const USpotLightComponent* SpotLight = Env.GetSpotLightOwner(i);
		const bool bRequiresShadowResource = SpotLight && SpotLight->IsCastShadow();
		const bool bHasShadowResource = ShadowBindingData
			&& i < ShadowBindingData->ActiveSpotLightMask.size()
			&& ShadowBindingData->ActiveSpotLightMask[i] != 0;
		if (ShadowBindingData && bRequiresShadowResource && !bHasShadowResource)
		{
			continue;
		}

		FLightInfo Info = Env.GetSpotLight(i).ToLightInfo();
		if (ShadowBindingData && i < ShadowBindingData->SpotLightShadowIndices.size())
		{
			Info.ShadowIndex = ShadowBindingData->SpotLightShadowIndices[i];
		}
		Infos.push_back(Info);
		++NumUploadedSpotLights;
	}

	GlobalLightingData.NumActivePointLights = NumUploadedPointLights;
	GlobalLightingData.NumActiveSpotLights = NumUploadedSpotLights;
	LastNumLights = static_cast<uint32>(Infos.size());

	GlobalLightingData.LightCullingMode = static_cast<uint32>(Frame.RenderOptions.LightCullingMode);
	GlobalLightingData.VisualizeLightCulling = Frame.RenderOptions.ViewMode == EViewMode::LightCulling ? 1u : 0u;
	GlobalLightingData.HeatMapMax = Frame.RenderOptions.HeatMapMax;
	if (ClusterState)
	{
		GlobalLightingData.ClusterCullingState = *ClusterState;
	}

	GlobalLightingData.NumTilesX = TileCullingResource.TileCountX;
	GlobalLightingData.NumTilesY = TileCullingResource.TileCountY;

	if (ShadowBindingData)
	{
		GlobalLightingData.ShadowMethod = ShadowBindingData->ShadowMethod;
		GlobalLightingData.NumCascades = ShadowBindingData->NumCascades;
		GlobalLightingData.CascadeSplits = ShadowBindingData->CascadeSplits;
		for (uint32 i = 0; i < 4; ++i)
		{
			GlobalLightingData.CascadeMatrices[i] = ShadowBindingData->CascadeMatrices[i];
		}
	}

	LightingConstantBuffer.Update(Ctx, &GlobalLightingData, sizeof(FLightingCBData));
	ID3D11Buffer* b4 = LightingConstantBuffer.GetBuffer();
	Ctx->VSSetConstantBuffers(ECBSlot::Lighting, 1, &b4);
	Ctx->PSSetConstantBuffers(ECBSlot::Lighting, 1, &b4);
	Ctx->CSSetConstantBuffers(ECBSlot::Lighting, 1, &b4);

	ForwardLights.Update(Dev, Ctx, Infos);
	Ctx->VSSetShaderResources(ELightTexSlot::AllLights, 1, &ForwardLights.LightBufferSRV);
	Ctx->PSSetShaderResources(ELightTexSlot::AllLights, 1, &ForwardLights.LightBufferSRV);

	if (ShadowBindingData)
	{
		ShadowInfos.Update(Dev, Ctx, ShadowBindingData->ShadowInfos);
	}
	else
	{
		TArray<FShadowInfo> EmptyInfos;
		ShadowInfos.Update(Dev, Ctx, EmptyInfos);
	}

	if (Frame.RenderOptions.LightCullingMode == ELightCullingMode::Tile)
	{
		BindTileCullingBuffers(Device);
	}
	else
	{
		UnbindTileCullingBuffers(Device);
	}
}

void FSystemResources::BindTileCullingBuffers(FD3DDevice& Device)
{
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	Ctx->VSSetShaderResources(ELightTexSlot::TileLightIndices, 1, &TileCullingResource.IndicesSRV);
	Ctx->VSSetShaderResources(ELightTexSlot::TileLightGrid, 1, &TileCullingResource.GridSRV);
	Ctx->PSSetShaderResources(ELightTexSlot::TileLightIndices, 1, &TileCullingResource.IndicesSRV);
	Ctx->PSSetShaderResources(ELightTexSlot::TileLightGrid, 1, &TileCullingResource.GridSRV);
	Ctx->VSSetShaderResources(ELightTexSlot::AllLights, 1, &ForwardLights.LightBufferSRV);
	Ctx->PSSetShaderResources(ELightTexSlot::AllLights, 1, &ForwardLights.LightBufferSRV);
}

void FSystemResources::UnbindTileCullingBuffers(FD3DDevice& Device)
{
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	ID3D11ShaderResourceView* NullSRVs[2] = {};
	Ctx->VSSetShaderResources(ELightTexSlot::TileLightIndices, 2, NullSRVs);
	Ctx->PSSetShaderResources(ELightTexSlot::TileLightIndices, 2, NullSRVs);
	Ctx->CSSetShaderResources(ELightTexSlot::TileLightIndices, 2, NullSRVs);
}

void FSystemResources::BindShadowResources(FD3DDevice& Device, FTextureAtlasPool& AtlasPool)
{
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	ID3D11ShaderResourceView* ShadowInfoSRV = ShadowInfos.SRV;
	ID3D11ShaderResourceView* ShadowAtlasSRV = AtlasPool.IsVSMMode()
		? AtlasPool.GetFilteredSRV()
		: AtlasPool.GetSRV();
	ID3D11ShaderResourceView* ShadowCubeSRVs[FTextureCubeShadowPool::TierCount] =
	{
		FTextureCubeShadowPool::Get().GetSRV(0),
		FTextureCubeShadowPool::Get().GetSRV(1),
		FTextureCubeShadowPool::Get().GetSRV(2),
		FTextureCubeShadowPool::Get().GetSRV(3)
	};

	Ctx->PSSetShaderResources(ESystemTexSlot::ShadowInfos, 1, &ShadowInfoSRV);
	Ctx->PSSetShaderResources(ESystemTexSlot::ShadowAtlasArray, 1, &ShadowAtlasSRV);
	Ctx->PSSetShaderResources(ESystemTexSlot::ShadowCubeArrayTier0, FTextureCubeShadowPool::TierCount, ShadowCubeSRVs);
	static const FTextureAtlasPool* LastBindLoggedPool = nullptr;
	//if (LastBindLoggedPool != &AtlasPool)
	//{
	//	LastBindLoggedPool = &AtlasPool;
	//	UE_LOG("[ShadowAtlas] Bind SRV Pool=%p", static_cast<void*>(&AtlasPool));
	//}
}

void FSystemResources::UnbindShadowResources(FD3DDevice& Device)
{
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	ID3D11ShaderResourceView* NullSRVs[2 + FTextureCubeShadowPool::TierCount] = {};
	Ctx->PSSetShaderResources(ESystemTexSlot::ShadowInfos, 2 + FTextureCubeShadowPool::TierCount, NullSRVs);
}

void FSystemResources::BindSystemSamplers(FD3DDevice& Device)
{
	SamplerStateManager.BindSystemSamplers(Device.GetDeviceContext());
}

void FSystemResources::SetDepthStencilState(FD3DDevice& Device, EDepthStencilState InState)
{
	DepthStencilStateManager.Set(Device.GetDeviceContext(), InState);
}

void FSystemResources::SetBlendState(FD3DDevice& Device, EBlendState InState)
{
	BlendStateManager.Set(Device.GetDeviceContext(), InState);
}

void FSystemResources::SetRasterizerState(FD3DDevice& Device, ERasterizerState InState)
{
	RasterizerStateManager.Set(Device.GetDeviceContext(), InState);
}

void FSystemResources::ResetRenderStateCache()
{
	RasterizerStateManager.ResetCache();
	DepthStencilStateManager.ResetCache();
	BlendStateManager.ResetCache();
}

void FSystemResources::UnbindSystemTextures(FD3DDevice& Device)
{
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	ID3D11ShaderResourceView* nullSRV = nullptr;
	Ctx->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &nullSRV);
	Ctx->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &nullSRV);
	Ctx->PSSetShaderResources(ESystemTexSlot::GBufferNormal, 1, &nullSRV);
	Ctx->PSSetShaderResources(ESystemTexSlot::Stencil, 1, &nullSRV);
	Ctx->PSSetShaderResources(ESystemTexSlot::CullingHeatmap, 1, &nullSRV);
}
