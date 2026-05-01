#pragma once

/*
	실제 렌더링을 담당하는 Class 입니다. (Rendering 최상위 클래스)
*/

#include "Render/Types/RenderTypes.h"

#include "Render/Pipeline/FrameContext.h"
#include "Render/Pipeline/DrawCommandBuilder.h"
#include "Render/Pipeline/PassRenderStateTable.h"
#include "Render/Pipeline/PassEventBuilder.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Culling/TileBasedLightCulling.h"
#include "Render/Culling/ClusteredLightCuller.h"
#include <wrl/client.h>

class FScene;

class FRenderer
{
public:
	void Create(HWND hWindow);
	void Release();

	// --- Render phase: 정렬 + GPU 제출 ---
	void BeginFrame();
	void Render(const FFrameContext& Frame, FScene& Scene);
	void EndFrame();

	FD3DDevice& GetFD3DDevice() { return Device; }

	// Collect 페이즈에서 커맨드 빌드를 담당하는 Builder
	FDrawCommandBuilder& GetBuilder() { return Builder; }

	// 뷰포트 리사이즈 후 렌더 상태 캐시 초기화
	void ResetRenderStateCache() { Resources.ResetRenderStateCache(); }

	// TileBasedLightCulling Dispatch에 필요한 리소스 접근자
	ID3D11Buffer* GetFrameBuffer() { return Resources.FrameBuffer.GetBuffer(); }
	ID3D11ShaderResourceView* GetLightBufferSRV() { return Resources.ForwardLights.LightBufferSRV; }
	FTileCullingResource& GetTileCullingResource() { return Resources.TileCullingResource; }
	uint32 GetNumLights() const { return Resources.LastNumLights; }
	FTileBasedLightCulling& GetTileBaseCulling() { return TileBasedCulling; }

	void BindTileCullingResources() { Resources.BindTileCullingBuffers(Device); }
	void UnbindTileCullingResources() { Resources.UnbindTileCullingBuffers(Device); }
	void DispatchClusterCullingResources();
	void BindClusterCullingResources();
	void UnbindClusterCullingResources();

private:
	enum class EShadowRenderTargetType
	{
		Atlas2D,
		CubeFace
	};

	//ShadowMap 렌더링에 필요한 정보 담는 녀석
	struct FShadowRenderTask
	{
		template<typename T>
		using TComPtr = Microsoft::WRL::ComPtr<T>;

		EShadowRenderTargetType TargetType = EShadowRenderTargetType::Atlas2D;
		FMatrix LightVP = FMatrix::Identity;
		FConvexVolume ShadowFrustum;
		D3D11_VIEWPORT Viewport = {};
		TComPtr<ID3D11DepthStencilView> DSV;
		TComPtr<ID3D11RenderTargetView> RTV;
		uint32 AtlasSliceIndex = static_cast<uint32>(-1);
		uint32 CubeIndex = static_cast<uint32>(-1);
		uint32 CubeTierIndex = static_cast<uint32>(-1);
		uint32 CubeFaceIndex = 0;
		FMatrix CameraVP = FMatrix::Identity;
		float ShadowDepthBias = 0.0f;
		float ShadowSlopeBias = 0.0f;
		bool bIsPSM = false;
		bool bPSMFlipNegativeW = false;
		bool bCullWithShadowFrustum = true;
	};

	//ShadowMap 렌더링에 필요한 정보 담는 녀석들을 담는 녀석
	struct FShadowPassData
	{
		FShadowFrameBindingData BindingData;
		TArray<FShadowRenderTask> RenderTasks;
		TArray<FEditorDebugLine> DebugLines;
	};

	struct FVSMBlurRegion
	{
		uint32 SliceIndex = static_cast<uint32>(-1);
		D3D11_BOX Box = {};
	};

	void BuildShadowPassData(const FFrameContext& Frame, FScene& Scene, FShadowPassData& OutShadowPassData);
	void RenderShadowPass(const FFrameContext& Frame, FScene& Scene, const FShadowPassData& ShadowPassData);
	void RenderVSMBlurPass(FTextureAtlasPool& AtlasPool, const FShadowPassData& ShadowPassData);
	void CleanupPassState(FStateCache& Cache);

private:
	FD3DDevice Device;

	FSystemResources Resources;
	FDrawCommandBuilder Builder;
	FPassRenderStateTable PassRenderStateTable;
	FPassEventBuilder PassEventBuilder;
	FConstantBuffer ShadowPassBuffer;
	FConstantBuffer VSMBlurPassBuffer;

	FTileBasedLightCulling TileBasedCulling;
	FClusteredLightCuller ClusteredLightCuller;
	uint64 ShadowAtlasFrameIndex = 1;
};
