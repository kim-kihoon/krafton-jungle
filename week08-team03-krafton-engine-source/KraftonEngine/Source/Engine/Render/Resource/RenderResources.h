#pragma once
#include "Render/Resource/Buffer.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Render/Pipeline/ForwardLightData.h"

#include "Render/Resource/RasterizerStateManager.h"
#include "Render/Resource/DepthStencilStateManager.h"
#include "Render/Resource/BlendStateManager.h"
#include "Render/Resource/SamplerStateManager.h"

/*
	시스템 레벨 GPU 리소스를 관리하는 구조체입니다.
	프레임 공용 CB (Frame, Lighting), 라이트 StructuredBuffer,
	렌더 상태 오브젝트(DSS/Blend/Rasterizer/Sampler),
	시스템 텍스처 언바인딩(t16-t19)을 소유합니다.
	셰이더별 CB(Gizmo, PostProcess 등)는 각 소유자(Proxy, Builder)가 직접 관리합니다.
*/

class FD3DDevice;
class FScene;
class FTextureAtlasPool;
struct FFrameContext;

struct FShadowFrameBindingData
{
	TArray<FShadowInfo> ShadowInfos;
	TArray<int32> PointLightShadowIndices;
	TArray<int32> SpotLightShadowIndices;
	TArray<uint8> ActivePointLightMask;
	TArray<uint8> ActiveSpotLightMask;
	int32 DirectionalShadowIndex = -1;

	uint32 ShadowMethod = 0;
	uint32 NumCascades = 0;
	FVector4 CascadeSplits;
	FShadowMatrixGPU CascadeMatrices[4];
};

struct FLightingResource
{
	ID3D11Buffer* LightBuffer = nullptr;
	ID3D11ShaderResourceView* LightBufferSRV = nullptr;
	uint32 MaxLightCount = 0;

	void Create(ID3D11Device* InDevice, uint32 InMaxLightCount);
	void Update(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, const TArray<FLightInfo>& LightInfos);
	void Release();
};

struct FShadowInfoResource
{
	ID3D11Buffer* Buffer = nullptr;
	ID3D11ShaderResourceView* SRV = nullptr;
	uint32 MaxCount = 0;

	void Create(ID3D11Device* InDevice, uint32 InMaxCount);
	void Update(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, const TArray<FShadowInfo>& ShadowInfos);
	void Release();
};

struct FTileCullingResource
{
	ID3D11Buffer* IndicesBuffer = nullptr;
	ID3D11Buffer* GridBuffer = nullptr;
	ID3D11Buffer* CounterBuffer = nullptr;

	ID3D11UnorderedAccessView* IndicesUAV = nullptr;
	ID3D11UnorderedAccessView* GridUAV = nullptr;
	ID3D11UnorderedAccessView* CounterUAV = nullptr;

	ID3D11ShaderResourceView* IndicesSRV = nullptr;
	ID3D11ShaderResourceView* GridSRV = nullptr;

	uint32 TileCountX = 0;
	uint32 TileCountY = 0;

	void Create(ID3D11Device* Dev, uint32 InTileCountX, uint32 InTileCountY);
	void Release();
};

struct FSystemResources
{
	// --- Frame CB (b0) ---
	FConstantBuffer FrameBuffer;				// b0 — ECBSlot::Frame

	// --- Lighting ---
	FConstantBuffer LightingConstantBuffer;
	FLightingResource ForwardLights;
	FShadowInfoResource ShadowInfos;
	FTileCullingResource TileCullingResource;
	uint32 LastNumLights = 0;

	// --- Render State Managers ---
	FRasterizerStateManager RasterizerStateManager;
	FDepthStencilStateManager DepthStencilStateManager;
	FBlendStateManager BlendStateManager;
	FSamplerStateManager SamplerStateManager;		// s0-s2

	void Create(ID3D11Device* InDevice);
	void Release();

	// 렌더 상태 전환
	void SetDepthStencilState(FD3DDevice& Device, EDepthStencilState InState);
	void SetBlendState(FD3DDevice& Device, EBlendState InState);
	void SetRasterizerState(FD3DDevice& Device, ERasterizerState InState);

	// 리사이즈 시 렌더 상태 캐시 무효화
	void ResetRenderStateCache();

	// 프레임 공용 CB 업데이트 + 바인딩 (b0)
	void UpdateFrameBuffer(FD3DDevice& Device, const FFrameContext& Frame);
	void UpdateLightBuffer(FD3DDevice& Device, const FScene& Scene, const FFrameContext& Frame,
		const FClusterCullingState* ClusterState = nullptr,
		const FShadowFrameBindingData* ShadowBindingData = nullptr);

	// s0-s2 시스템 샘플러 일괄 바인딩 (프레임 1회)
	void BindSystemSamplers(FD3DDevice& Device);

	// 타일 컬링 결과 SRV 바인딩 (t9, t10) — Renderer::Render 시작 시 호출
	void BindTileCullingBuffers(FD3DDevice& Device);
	void UnbindTileCullingBuffers(FD3DDevice& Device);

	// 쉐도우 리소스 SRV 바인딩 (t21, t22, t23, t24, t25)
	void BindShadowResources(FD3DDevice& Device, FTextureAtlasPool& AtlasPool);
	void UnbindShadowResources(FD3DDevice& Device);

	// 시스템 텍스처 슬롯 언바인딩 (t16-t19)
	void UnbindSystemTextures(FD3DDevice& Device);
};
