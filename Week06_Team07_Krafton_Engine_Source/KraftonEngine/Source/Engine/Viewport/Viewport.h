#pragma once

#include "Render/Types/RenderTypes.h"

class FViewportClient;

// UE의 FViewport 대응 — 오프스크린 렌더 타깃 + D3D 리소스
class FViewport
{
public:
	FViewport() = default;
	~FViewport();

	// D3D 리소스 생성/해제/리사이즈
	bool Initialize(ID3D11Device* InDevice, uint32 InWidth, uint32 InHeight);
	void Release();
	void Resize(uint32 InWidth, uint32 InHeight);

	// 지연 리사이즈 — ImGui 렌더 중에 요청, RenderViewport 직전에 적용
	void RequestResize(uint32 InWidth, uint32 InHeight);
	bool ApplyPendingResize();

	// 오프스크린 RT 클리어 + 바인딩 (렌더 시작 시 호출)
	void BeginRender(ID3D11DeviceContext* Ctx, const float ClearColor[4] = nullptr);

	// ViewportClient 참조
	void SetClient(FViewportClient* InClient) { ViewportClient = InClient; }
	FViewportClient* GetClient() const { return ViewportClient; }

	// 크기
	uint32 GetWidth() const { return Width; }
	uint32 GetHeight() const { return Height; }

	// D3D 리소스 접근자
	ID3D11RenderTargetView* GetRTV() const { return RTV; }
	ID3D11ShaderResourceView* GetSRV() const { return bHasPostProcessed && CurrentSRV ? CurrentSRV : SRV; }
	ID3D11RenderTargetView* GetBaseRTV() const { return RTV; }
	ID3D11ShaderResourceView* GetBaseSRV() const { return SRV; }
	ID3D11RenderTargetView* GetPostProcessRTV1() const { return PRTV1; }
	ID3D11ShaderResourceView* GetPostProcessSRV1() const { return PSRV1; }
	ID3D11RenderTargetView* GetPostProcessRTV2() const { return PRTV2; }
	ID3D11ShaderResourceView* GetPostProcessSRV2() const { return PSRV2; }

	ID3D11ShaderResourceView* GetDepthSRV() const { return DepthSRV; }
	ID3D11ShaderResourceView* GetStencilSRV() const { return StencilSRV; }
	ID3D11DepthStencilView* GetDSV() const { return DSV; }
	const D3D11_VIEWPORT& GetViewportRect() const { return ViewportRect; }

	void SetHasPostProcessed(bool bInHasPP) { bHasPostProcessed = bInHasPP; }

	void SetCurrentRenderTarget(ID3D11RenderTargetView* InRTV, ID3D11ShaderResourceView* InSRV)
	{
		CurrentRTV = InRTV;
		CurrentSRV = InSRV;
	}
	ID3D11RenderTargetView* GetCurrentRTV() const { return CurrentRTV; }
	ID3D11ShaderResourceView* GetCurrentSRV() const { return CurrentSRV; }

private:
	bool CreateResources();
	void ReleaseResources();

private:
	FViewportClient* ViewportClient = nullptr;

	ID3D11Device* Device = nullptr;

	// 렌더 타깃 1 (기본)
	ID3D11Texture2D* RTTexture = nullptr;
	ID3D11RenderTargetView* RTV = nullptr;
	ID3D11ShaderResourceView* SRV = nullptr;

	// 렌더 타깃 2 (PostProcess 결과용)
	ID3D11Texture2D* PSTexture1 = nullptr;
	ID3D11RenderTargetView* PRTV1 = nullptr;
	ID3D11ShaderResourceView* PSRV1 = nullptr;

	// 렌더 타깃 3 (PostProcess Ping-Pong용)
	ID3D11Texture2D* PSTexture2 = nullptr;
	ID3D11RenderTargetView* PRTV2 = nullptr;
	ID3D11ShaderResourceView* PSRV2 = nullptr;

	// 렌더 타깃 추적용
	ID3D11RenderTargetView* CurrentRTV = nullptr;
	ID3D11ShaderResourceView* CurrentSRV = nullptr;


	bool bHasPostProcessed = false;

	// 뎁스/스텐실 (TYPELESS 텍스처 → DSV + DepthSRV + StencilSRV 분리)
	ID3D11Texture2D* DepthTexture = nullptr;
	ID3D11DepthStencilView* DSV = nullptr;
	ID3D11ShaderResourceView* DepthSRV = nullptr;		// Hi-Z / GPU Occlusion에서 뎁스 읽기용
	ID3D11ShaderResourceView* StencilSRV = nullptr;	// PostProcess에서 스텐실 읽기용

	D3D11_VIEWPORT ViewportRect = {};

	uint32 Width = 0;
	uint32 Height = 0;

	// 지연 리사이즈 요청
	uint32 PendingWidth = 0;
	uint32 PendingHeight = 0;
	bool bPendingResize = false;
};
