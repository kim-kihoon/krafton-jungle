#pragma once
#include "Core/CoreTypes.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Render/Types/ViewTypes.h"

class UCameraComponent;
class FViewport;
class FPrimitiveSceneProxy;
class FGPUOcclusionCulling;

#include "Render/Pipeline/LODContext.h"


class FRenderBus
{
public:
	void Clear();

	// 프록시 직접 제출 — 포인터만 저장, 데이터는 프록시 소유
	void AddProxy(ERenderPass Pass, const FPrimitiveSceneProxy* Proxy);
	const TArray<const FPrimitiveSceneProxy*>& GetProxies(ERenderPass Pass) const;

	// Batcher 패스용 — 타입 안전한 전용 큐
	void AddFontEntry(FFontEntry&& Entry);
	void AddOverlayFontEntry(FFontEntry&& Entry);
	void AddSubUVEntry(FSubUVEntry&& Entry);
	void AddBillboardEntry(FBillboardEntry&& Entry);
	void AddAABBEntry(FAABBEntry&& Entry);
	void AddOBBEntry(FOBBEntry&& Entry);
	void AddGridEntry(FGridEntry&& Entry);
	void AddDebugLineEntry(FDebugLineEntry&& Entry);
	void AddFireBallEntry(FFireBallEntry& Entry);

	const TArray<FFontEntry>& GetFontEntries() const { return FontEntries; }
	const TArray<FFontEntry>& GetOverlayFontEntries() const { return OverlayFontEntries; }
	const TArray<FSubUVEntry>& GetSubUVEntries() const { return SubUVEntries; }
	const TArray<FBillboardEntry>& GetBillboardEntries() const { return BillboardEntries; }
	const TArray<FAABBEntry>& GetAABBEntries() const { return AABBEntries; }
	const TArray<FOBBEntry>& GetOBBEntries() const { return OBBEntries; }
	const TArray<FGridEntry>& GetGridEntries() const { return GridEntries; }
	const TArray<FDebugLineEntry>& GetDebugLineEntries() const { return DebugLineEntries; }
	const TArray<FFireBallEntry>& GetFireBallEntries() const { return FireBallEntries; }

	// Getter,Setter
	void SetCameraInfo(const UCameraComponent* Camera);
	void SetViewportInfo(const FViewport* VP);
	void SetViewportSize(float InWidth, float InHeight);
	void SetRenderSettings(const EViewMode NewViewMode, const FShowFlags NewShowFlags);

	const FMatrix& GetView() const { return View; }
	const FMatrix& GetProj() const { return Proj; }
	const FVector& GetCameraPosition() const { return CameraPosition; }
	const FVector& GetCameraForward() const { return CameraForward; }
	const FVector& GetCameraUp() const { return CameraUp; }
	const FVector& GetCameraRight() const { return CameraRight; }
	bool  IsOrtho()        const { return bIsOrtho; }
	bool  IsFixedOrtho()   const { return bIsOrtho && ViewportType != ELevelViewportType::Perspective && ViewportType != ELevelViewportType::FreeOrthographic; }
	float GetOrthoWidth()  const { return OrthoWidth; }
	bool  IsFXAAEnabled()  const { return bFXAAEnabled; }
	void  SetFXAAEnabled(bool bInEnabled) { bFXAAEnabled = bInEnabled; }
	ELevelViewportType GetViewportType() const { return ViewportType; }
	void SetViewportType(ELevelViewportType InType) { ViewportType = InType; }
	EViewMode GetViewMode() const { return ViewMode; }
	const FShowFlags& GetShowFlags() const { return ShowFlags; }
	const FVector& GetWireframeColor() const { return WireframeColor; }
	void SetWireframeColor(const FVector& InColor) { WireframeColor = InColor; }

	const float GetViewportWidth() const { return viewportWidth; }
	const float GetViewportHeight() const { return viewportHeight; }
	ID3D11RenderTargetView* GetViewportRTV()        const { return ViewportRTV; }
	ID3D11DepthStencilView* GetViewportDSV()        const { return ViewportDSV; }
	ID3D11ShaderResourceView* GetViewportStencilSRV() const { return ViewportStencilSRV; }
	ID3D11ShaderResourceView* GetViewportDepthSRV()   const { return ViewportDepthSRV; }

	// Height Fog
	void SetFogParams(const FHeightFogConstants& InFog) { FogParams = InFog; bHasFog = true; }
	const FHeightFogConstants& GetFogParams() const { return FogParams; }
	bool HasFog() const { return bHasFog; }


	// PostProcess 전용 (FXAA 등)
	ID3D11RenderTargetView* GetPostProcessRTV1()    const { return PostProcessRTV1; }
	ID3D11RenderTargetView* GetPostProcessRTV2()    const { return PostProcessRTV2; }
	ID3D11ShaderResourceView* GetPostProcessSRV1()    const { return PostProcessSRV1; }
	ID3D11ShaderResourceView* GetPostProcessSRV2()    const { return PostProcessSRV2; }
	ID3D11ShaderResourceView* GetBaseSRV()      const { return BaseSRV; }
	ID3D11RenderTargetView* GetCurrentRTV() const { return CurrentRTV; }
	ID3D11ShaderResourceView* GetCurrentSRV() const { return CurrentSRV; }
	bool HasPostProcessOutput() const { return bHasPostProcessOutput; }
	void SetCurrentPostProcessTarget(ID3D11RenderTargetView* InRTV, ID3D11ShaderResourceView* InSRV)
	{
		CurrentRTV = InRTV;
		CurrentSRV = InSRV;
		bHasPostProcessOutput = (InSRV != nullptr && InSRV != BaseSRV);
	}


	// GPU Occlusion Culling — set by render pipeline, read by collector
	void SetOcclusionCulling(FGPUOcclusionCulling* InOcclusion) { OcclusionCulling = InOcclusion; }
	const FGPUOcclusionCulling* GetOcclusionCulling() const { return OcclusionCulling; }
	FGPUOcclusionCulling* GetOcclusionCullingMutable() const { return OcclusionCulling; }

	// LOD Update Context — WorldTick에서 설정, Collect에서 사용
	void SetLODContext(const FLODUpdateContext& InCtx) { LODContext = InCtx; }
	const FLODUpdateContext& GetLODContext() const { return LODContext; }

private:
	// 프록시 패스 큐 — 포인터만 저장, 데이터는 프록시 소유
	TArray<const FPrimitiveSceneProxy*> ProxyQueues[(uint32)ERenderPass::MAX];

	// Batcher 패스 큐
	TArray<FFontEntry>  FontEntries;
	TArray<FFontEntry>  OverlayFontEntries;
	TArray<FSubUVEntry> SubUVEntries;
	TArray<FBillboardEntry> BillboardEntries;
	TArray<FAABBEntry>  AABBEntries;
	TArray<FOBBEntry>  OBBEntries;
	TArray<FGridEntry>  GridEntries;
	TArray<FDebugLineEntry> DebugLineEntries;

	// ConstantBuffer Batch용
	TArray<FFireBallEntry> FireBallEntries;

	FMatrix View;
	FMatrix Proj;
	FVector CameraPosition;
	FVector CameraForward;
	FVector CameraRight;
	FVector CameraUp;

	float viewportWidth = 0.0f;
	float viewportHeight = 0.0f;

	bool  bIsOrtho = false;
	float  OrthoWidth = 10.0f;
	bool   bFXAAEnabled = false;
	ELevelViewportType ViewportType = ELevelViewportType::Perspective;

	// PostProcess용 뷰포트 D3D 리소스 (프레임 내 유효)
	ID3D11RenderTargetView* ViewportRTV = nullptr;
	ID3D11DepthStencilView* ViewportDSV = nullptr;
	ID3D11ShaderResourceView* ViewportStencilSRV = nullptr;
	ID3D11ShaderResourceView* ViewportDepthSRV = nullptr;
	ID3D11ShaderResourceView* BaseSRV = nullptr;

	ID3D11RenderTargetView* PostProcessRTV1 = nullptr;
	ID3D11ShaderResourceView* PostProcessSRV1 = nullptr;
	ID3D11RenderTargetView* PostProcessRTV2 = nullptr;
	ID3D11ShaderResourceView* PostProcessSRV2 = nullptr;
	ID3D11RenderTargetView* CurrentRTV = nullptr;
	ID3D11ShaderResourceView* CurrentSRV = nullptr;
	bool bHasPostProcessOutput = false;



	// Height Fog
	FHeightFogConstants FogParams;
	bool bHasFog = false;

	// GPU Occlusion
	FGPUOcclusionCulling* OcclusionCulling = nullptr;

	// LOD
	FLODUpdateContext LODContext;

	//Editor Settings
	EViewMode ViewMode;
	FShowFlags ShowFlags;
	FVector WireframeColor = FVector(0.0f, 0.0f, 0.7f);
};
