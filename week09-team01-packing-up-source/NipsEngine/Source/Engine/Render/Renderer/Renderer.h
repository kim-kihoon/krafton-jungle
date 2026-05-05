#pragma once

/*
	실제 렌더링을 담당하는 Class 입니다. (Rendering 최상위 클래스)
*/

#include "Render/Common/RenderTypes.h"
#include "Render/Resource/VertexTypes.h"

#include "Render/Scene/RenderBus.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Resource/Buffer.h"
#include "Render/Resource/RenderResources.h"
#include "Render/LineBatcher.h"
#include "Render/FontBatcher.h"
#include "Render/SubUVBatcher.h"

#include <cstddef>
#include <functional>

#include "Render/Renderer/RenderFlow/RenderPipeline.h"

class FShaderBindingInstance;

/**
 * Renderer 가 Viewport 별로 소유하는 데이터를 나타내는 구조체
 */
struct FViewportRenderResource
{
	TComPtr<ID3D11Texture2D> ColorTex;
	TComPtr<ID3D11RenderTargetView> ColorRTV;
	TComPtr<ID3D11ShaderResourceView> ColorSRV;

	TComPtr<ID3D11Texture2D> NormalTex;
	TComPtr<ID3D11RenderTargetView> NormalRTV;
	TComPtr<ID3D11ShaderResourceView> NormalSRV;

	TComPtr<ID3D11Texture2D> LightTex;
	TComPtr<ID3D11RenderTargetView> LightRTV;
	TComPtr<ID3D11ShaderResourceView> LightSRV;

	TComPtr<ID3D11Texture2D> FogTex;
	TComPtr<ID3D11RenderTargetView> FogRTV;
	TComPtr<ID3D11ShaderResourceView> FogSRV;

	TComPtr<ID3D11Texture2D> WorldPosTex;
	TComPtr<ID3D11RenderTargetView> WorldPosRTV;
	TComPtr<ID3D11ShaderResourceView> WorldPosSRV;

	TComPtr<ID3D11Texture2D> FXAATex;
	TComPtr<ID3D11RenderTargetView> FXAARTV;
	TComPtr<ID3D11ShaderResourceView> FXAASRV;

	TComPtr<ID3D11Texture2D> SelectionMaskTex;
	TComPtr<ID3D11RenderTargetView> SelectionMaskRTV;
	TComPtr<ID3D11ShaderResourceView> SelectionMaskSRV;

	TComPtr<ID3D11Texture2D> DepthTex;
	TComPtr<ID3D11DepthStencilView> DepthStencilView;
	TComPtr<ID3D11ShaderResourceView> DepthStencilSRV;

	uint32 Width = 0;
	uint32 Height = 0;

	FRenderTargetSet RenderTargetSet;

	FRenderTargetSet& GetView()
	{
		RenderTargetSet.SceneColorRTV = ColorRTV.Get();
		RenderTargetSet.SceneColorSRV = ColorSRV.Get();
		RenderTargetSet.FinalRTV = ColorRTV.Get();
		RenderTargetSet.FinalSRV = ColorSRV.Get();

		RenderTargetSet.SceneNormalRTV = NormalRTV.Get();
		RenderTargetSet.SceneNormalSRV = NormalSRV.Get();

		RenderTargetSet.SceneLightRTV = LightRTV.Get();
		RenderTargetSet.SceneLightSRV = LightSRV.Get();

		RenderTargetSet.SceneFogRTV = FogRTV.Get();
		RenderTargetSet.SceneFogSRV = FogSRV.Get();

		RenderTargetSet.SceneWorldPosRTV = WorldPosRTV.Get();
		RenderTargetSet.SceneWorldPosSRV = WorldPosSRV.Get();

		RenderTargetSet.SceneFXAARTV = FXAARTV.Get();
		RenderTargetSet.SceneFXAASRV = FXAASRV.Get();

		RenderTargetSet.SceneDepthSRV = DepthStencilSRV.Get();
		RenderTargetSet.SelectionMaskRTV = SelectionMaskRTV.Get();
		RenderTargetSet.SelectionMaskSRV = SelectionMaskSRV.Get();
		RenderTargetSet.DepthStencilView = DepthStencilView.Get();
		RenderTargetSet.Width = static_cast<float>(Width);
		RenderTargetSet.Height = static_cast<float>(Height);
		return RenderTargetSet;
	}

};

// 패스별 Batcher 바인딩 — Clear → Collect 패턴
struct FPassBatcherBinding
{
	std::function<void()> Clear;
	std::function<void(const FRenderCommand&, const FRenderBus&)> Collect;

	explicit operator bool() const { return Clear != nullptr && Collect != nullptr; }
};

class FRenderer
{
public:
	void Create(HWND hWindow);
	void CreateResources();
	void Release();

	void PrepareBatchers(const FRenderBus& InRenderBus);
	void BeginFrame();
	// Viewport 로부터 RTV, SRV 등 정보를 받아서 세팅
	void BeginViewportFrame(FRenderTargetSet* InRenderTargetSet);
	void Render(const FRenderBus& InRenderBus);
	void PresentToBackBuffer(const ID3D11ShaderResourceView* FinalSRV);
	void RenderToCurrentTarget(const std::function<void(int32 Width, int32 Height)>& RenderCallback);
	void EndFrame();
	void UseBackBufferRenderTargets();
	
	void UseViewportRenderTargets(FRenderTargetSet* InRenderTargetSet);
	void InvalidateSceneFinalTargets();

	FD3DDevice& GetFD3DDevice() { return Device; }
	FRenderResources& GetResources() { return Resources; }
	FLineBatcher& GetEditorLineBatcher() { return EditorLineBatcher; }

	const ID3D11RenderTargetView*   GetCurrentSceneRTV() const { return SceneFinalRTV.Get(); }
	const ID3D11ShaderResourceView* GetCurrentSceneSRV() const { return SceneFinalSRV.Get(); }

	// 현재는 Resource 를 Handle 이 아니라, 고정된 4개의 Viewport 에 대한 Index 를 통해 관리
	// 추가로 VP 를 받아서 원래 해당하는 Resource 를 찾아야하는데 현재는 Index 로 찾는 중
	FViewportRenderResource& AcquireViewportResource(uint32 W, uint32 H, int32 Index);
	void InitializeViewportResource(uint32 Width, uint32 Height, int32 Index);
	void ReleaseViewportResource(int32 Index);

private:
	void InitializePassBatchers();
	void UpdateSceneLightBuffer(const FRenderBus& InRenderBus);

private:
	FD3DDevice Device;
	FRenderTargetSet* CurrentRenderTargets = nullptr;
	FRenderResources Resources;
	FLineBatcher   EditorLineBatcher;
	FLineBatcher   GridLineBatcher;
	FFontBatcher   FontBatcher;
	FSubUVBatcher  SubUVBatcher;
	FStructuredBuffer SceneLightBuffer;
	TArray<FGPULight> SceneGlobalLightUploadScratch;

	/** 모든 Render Pass 를 관리할 객체 */
	FRenderPipeline RenderPipeline;
	std::shared_ptr<FRenderPassContext> RenderPassContext;
	std::shared_ptr<FShaderBindingInstance> ViewportPresentShaderBinding;

	// 패스별 커맨드 정렬이 필요한 경우 정렬된 복사본 반환, 아니면 원본 참조
	const TArray<FRenderCommand>& GetAlignedCommands(ERenderPass Pass, const TArray<FRenderCommand>& Commands);
	TArray<FRenderCommand> SortedCommandBuffer;  // 재할당 방지용 멤버 버퍼

	FPassBatcherBinding PassBatchers[(uint32)ERenderPass::MAX];

	//	Primitive and Gizmo Input Layout
	D3D11_INPUT_ELEMENT_DESC PrimitiveInputLayout[2] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  static_cast<uint32>(offsetof(FVertex, Position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FVertex, Color)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	// StaticMesh (FNormalVertex) Input Layout
	D3D11_INPUT_ELEMENT_DESC NormalVertexInputLayout[6] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FNormalVertex, Position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<uint32>(offsetof(FNormalVertex, Color)),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FNormalVertex, Normal)),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<uint32>(offsetof(FNormalVertex, UVs)),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FNormalVertex, Tangent)),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BITANGENT",0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<uint32>(offsetof(FNormalVertex, Bitangent)),D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	D3D11_INPUT_ELEMENT_DESC TextureVertexInputLayout[2] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	// FinalRTV 는 Render Pass 구성에 따라 달라지므로 Renderer 내에서 보관
	TComPtr<ID3D11RenderTargetView> SceneFinalRTV = nullptr;
	TComPtr<ID3D11ShaderResourceView> SceneFinalSRV = nullptr;
	constexpr static uint32 MaxRTVCount = 3;
	// Directional, Ambient 같은 전역 Light 개수 제한
	constexpr static uint32 MaxSceneGlobalLightCount = 64;

	// 지금은 4개 Viewport 고정 존재 상황이라 다음과 같이 처리
	FViewportRenderResource ViewportResources[4];
};

