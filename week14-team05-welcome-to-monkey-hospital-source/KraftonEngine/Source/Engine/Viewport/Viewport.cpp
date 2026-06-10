#include "Viewport/Viewport.h"

#include "Render/Resource/Buffer.h"

namespace
{
	constexpr uint32 DoFBokehDownsampleFactor = 2;
}

FViewport::~FViewport()
{
	ReleaseResources();
}

bool FViewport::Initialize(ID3D11Device* InDevice, uint32 InWidth, uint32 InHeight)
{
	Device = InDevice;
	Width = InWidth;
	Height = InHeight;

	return CreateResources();
}

void FViewport::Release()
{
	ReleaseResources();
	Device = nullptr;
	Width = 0;
	Height = 0;
}

void FViewport::Resize(uint32 InWidth, uint32 InHeight)
{
	if (InWidth == 0 || InHeight == 0) return;
	if (InWidth == Width && InHeight == Height) return;

	Width = InWidth;
	Height = InHeight;

	ReleaseResources();
	CreateResources();
}

void FViewport::RequestResize(uint32 InWidth, uint32 InHeight)
{
	if (InWidth == 0 || InHeight == 0) return;
	if (InWidth == Width && InHeight == Height)
	{
		bPendingResize = false;
		return;
	}

	PendingWidth = InWidth;
	PendingHeight = InHeight;
	bPendingResize = true;
}

bool FViewport::ApplyPendingResize()
{
	if (!bPendingResize) return false;

	bPendingResize = false;
	Resize(PendingWidth, PendingHeight);
	return true;
}

void FViewport::BeginRender(ID3D11DeviceContext* Ctx, const float ClearColor[4])
{
	if (!RTV) return;

	const float DefaultColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
	const float* Color = ClearColor ? ClearColor : DefaultColor;
	D3D11_VIEWPORT VPRect = GetViewportRect();

	Ctx->ClearRenderTargetView(RTV, Color);
	if (CoCRTV)
	{
		const float CoCClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Ctx->ClearRenderTargetView(CoCRTV, CoCClear);
	}
	if (DoFBackgroundRTV)
	{
		const float DoFClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Ctx->ClearRenderTargetView(DoFBackgroundRTV, DoFClear);
	}
	if (DoFForegroundRTV)
	{
		const float DoFClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Ctx->ClearRenderTargetView(DoFForegroundRTV, DoFClear);
	}
	if (DoFBokehRTV)
	{
		const float DoFClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Ctx->ClearRenderTargetView(DoFBokehRTV, DoFClear);
	}
	Ctx->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
	Ctx->OMSetRenderTargets(1, &RTV, DSV);
	Ctx->RSSetViewports(1, &VPRect);
}

bool FViewport::CreateResources()
{
	if (!Device || Width == 0 || Height == 0) return false;

	// ── 렌더 타깃 텍스처 ──
	D3D11_TEXTURE2D_DESC TexDesc = {};
	TexDesc.Width = Width;
	TexDesc.Height = Height;
	TexDesc.MipLevels = 1;
	TexDesc.ArraySize = 1;
	TexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TexDesc.SampleDesc.Count = 1;
	TexDesc.Usage = D3D11_USAGE_DEFAULT;
	TexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = Device->CreateTexture2D(&TexDesc, nullptr, &RTTexture);
	if (FAILED(hr)) return false;
	RTTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportSceneColorTexture")), "ViewportSceneColorTexture");

	hr = Device->CreateRenderTargetView(RTTexture, nullptr, &RTV);
	if (FAILED(hr)) return false;
	RTV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportSceneColorRTV")), "ViewportSceneColorRTV");

	hr = Device->CreateShaderResourceView(RTTexture, nullptr, &SRV);
	if (FAILED(hr)) return false;
	SRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportSceneColorSRV")), "ViewportSceneColorSRV");

	// ── SceneColor 복사 텍스처 (FXAA 등 PostProcess용 CopyResource 대상) ──
	D3D11_TEXTURE2D_DESC SceneColorCopyDesc = TexDesc;
	SceneColorCopyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;  // SRV 읽기 전용
	hr = Device->CreateTexture2D(&SceneColorCopyDesc, nullptr, &SceneColorCopyTexture);
	if (FAILED(hr)) return false;
	SceneColorCopyTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportSceneColorCopyTexture")), "ViewportSceneColorCopyTexture");

	// ── 뎁스/스텐실 (TYPELESS → DSV + StencilSRV) ──
	D3D11_TEXTURE2D_DESC DepthDesc = {};
	DepthDesc.Width = Width;
	DepthDesc.Height = Height;
	DepthDesc.MipLevels = 1;
	DepthDesc.ArraySize = 1;
	DepthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	DepthDesc.SampleDesc.Count = 1;
	DepthDesc.Usage = D3D11_USAGE_DEFAULT;
	DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	hr = Device->CreateTexture2D(&DepthDesc, nullptr, &DepthTexture);
	if (FAILED(hr)) return false;
	DepthTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDepthTexture")), "ViewportDepthTexture");

	// DSV: D24_UNORM_S8_UINT 로 해석 (기존과 동일한 뎁스/스텐실 동작)
	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
	DSVDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DSVDesc.Texture2D.MipSlice = 0;

	hr = Device->CreateDepthStencilView(DepthTexture, &DSVDesc, &DSV);
	if (FAILED(hr)) return false;
	DSV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDSV")), "ViewportDSV");

	// SRV 포맷 (DepthCopy/StencilCopy 생성에 재사용)
	D3D11_SHADER_RESOURCE_VIEW_DESC DepthSRVDesc = {};
	DepthSRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	DepthSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	DepthSRVDesc.Texture2D.MipLevels = 1;
	DepthSRVDesc.Texture2D.MostDetailedMip = 0;

	D3D11_SHADER_RESOURCE_VIEW_DESC StencilSRVDesc = {};
	StencilSRVDesc.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
	StencilSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	StencilSRVDesc.Texture2D.MipLevels = 1;
	StencilSRVDesc.Texture2D.MostDetailedMip = 0;

	// ── Depth 복사 텍스처 (CopyResource 대상, SRV 전용) ──
	D3D11_TEXTURE2D_DESC CopyDesc = {};
	CopyDesc.Width = Width;
	CopyDesc.Height = Height;
	CopyDesc.MipLevels = 1;
	CopyDesc.ArraySize = 1;
	CopyDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	CopyDesc.SampleDesc.Count = 1;
	CopyDesc.Usage = D3D11_USAGE_DEFAULT;
	CopyDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	hr = Device->CreateTexture2D(&CopyDesc, nullptr, &DepthCopyTexture);
	if (FAILED(hr)) return false;
	DepthCopyTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDepthCopyTexture")), "ViewportDepthCopyTexture");

	hr = Device->CreateShaderResourceView(DepthCopyTexture, &DepthSRVDesc, &DepthCopySRV);
	if (FAILED(hr)) return false;
	DepthCopySRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDepthCopySRV")), "ViewportDepthCopySRV");

	hr = Device->CreateShaderResourceView(DepthCopyTexture, &StencilSRVDesc, &StencilCopySRV);
	if (FAILED(hr)) return false;
	StencilCopySRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportStencilCopySRV")), "ViewportStencilCopySRV");

	D3D11_SHADER_RESOURCE_VIEW_DESC SceneColorCopySRVDesc = {};
	SceneColorCopySRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SceneColorCopySRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SceneColorCopySRVDesc.Texture2D.MipLevels = 1;
	SceneColorCopySRVDesc.Texture2D.MostDetailedMip = 0;

	hr = Device->CreateShaderResourceView(SceneColorCopyTexture, &SceneColorCopySRVDesc, &SceneColorCopySRV);
	if (FAILED(hr)) return false;
	SceneColorCopySRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportSceneColorCopySRV")), "ViewportSceneColorCopySRV");

	// ── DoF CoC RT (R16_FLOAT) ──
	D3D11_TEXTURE2D_DESC CoCDesc = {};
	CoCDesc.Width = Width;
	CoCDesc.Height = Height;
	CoCDesc.MipLevels = 1;
	CoCDesc.ArraySize = 1;
	CoCDesc.Format = DXGI_FORMAT_R16_FLOAT;
	CoCDesc.SampleDesc.Count = 1;
	CoCDesc.Usage = D3D11_USAGE_DEFAULT;
	CoCDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	hr = Device->CreateTexture2D(&CoCDesc, nullptr, &CoCTexture);
	if (FAILED(hr)) return false;
	CoCTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportCoCTexture")), "ViewportCoCTexture");

	hr = Device->CreateRenderTargetView(CoCTexture, nullptr, &CoCRTV);
	if (FAILED(hr)) return false;
	CoCRTV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportCoCRTV")), "ViewportCoCRTV");

	hr = Device->CreateShaderResourceView(CoCTexture, nullptr, &CoCSRV);
	if (FAILED(hr)) return false;
	CoCSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportCoCSRV")), "ViewportCoCSRV");

	// ── DoF intermediate RTs ──
	D3D11_TEXTURE2D_DESC DoFLayerDesc = {};
	DoFLayerDesc.Width = Width;
	DoFLayerDesc.Height = Height;
	DoFLayerDesc.MipLevels = 1;
	DoFLayerDesc.ArraySize = 1;
	DoFLayerDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	DoFLayerDesc.SampleDesc.Count = 1;
	DoFLayerDesc.Usage = D3D11_USAGE_DEFAULT;
	DoFLayerDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	hr = Device->CreateTexture2D(&DoFLayerDesc, nullptr, &DoFBackgroundTexture);
	if (FAILED(hr)) return false;
	DoFBackgroundTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFBackgroundTexture")), "ViewportDoFBackgroundTexture");

	hr = Device->CreateRenderTargetView(DoFBackgroundTexture, nullptr, &DoFBackgroundRTV);
	if (FAILED(hr)) return false;
	DoFBackgroundRTV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFBackgroundRTV")), "ViewportDoFBackgroundRTV");

	hr = Device->CreateShaderResourceView(DoFBackgroundTexture, nullptr, &DoFBackgroundSRV);
	if (FAILED(hr)) return false;
	DoFBackgroundSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFBackgroundSRV")), "ViewportDoFBackgroundSRV");

	hr = Device->CreateTexture2D(&DoFLayerDesc, nullptr, &DoFForegroundTexture);
	if (FAILED(hr)) return false;
	DoFForegroundTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFForegroundTexture")), "ViewportDoFForegroundTexture");

	hr = Device->CreateRenderTargetView(DoFForegroundTexture, nullptr, &DoFForegroundRTV);
	if (FAILED(hr)) return false;
	DoFForegroundRTV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFForegroundRTV")), "ViewportDoFForegroundRTV");

	hr = Device->CreateShaderResourceView(DoFForegroundTexture, nullptr, &DoFForegroundSRV);
	if (FAILED(hr)) return false;
	DoFForegroundSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFForegroundSRV")), "ViewportDoFForegroundSRV");

	D3D11_TEXTURE2D_DESC DoFBokehDesc = DoFLayerDesc;
	DoFBokehWidth = (Width + DoFBokehDownsampleFactor - 1) / DoFBokehDownsampleFactor;
	DoFBokehHeight = (Height + DoFBokehDownsampleFactor - 1) / DoFBokehDownsampleFactor;
	DoFBokehDesc.Width = DoFBokehWidth;
	DoFBokehDesc.Height = DoFBokehHeight;
	DoFBokehDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	hr = Device->CreateTexture2D(&DoFBokehDesc, nullptr, &DoFBokehTexture);
	if (FAILED(hr)) return false;
	DoFBokehTexture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFBokehTexture")), "ViewportDoFBokehTexture");

	hr = Device->CreateRenderTargetView(DoFBokehTexture, nullptr, &DoFBokehRTV);
	if (FAILED(hr)) return false;
	DoFBokehRTV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFBokehRTV")), "ViewportDoFBokehRTV");

	hr = Device->CreateShaderResourceView(DoFBokehTexture, nullptr, &DoFBokehSRV);
	if (FAILED(hr)) return false;
	DoFBokehSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("ViewportDoFBokehSRV")), "ViewportDoFBokehSRV");

	// ── 뷰포트 렉트 ──
	ViewportRect.TopLeftX = 0.0f;
	ViewportRect.TopLeftY = 0.0f;
	ViewportRect.Width = static_cast<float>(Width);
	ViewportRect.Height = static_cast<float>(Height);
	ViewportRect.MinDepth = 0.0f;
	ViewportRect.MaxDepth = 1.0f;

	return true;
}

void FViewport::ReleaseResources()
{
	if (DoFBokehSRV) { DoFBokehSRV->Release(); DoFBokehSRV = nullptr; }
	if (DoFBokehRTV) { DoFBokehRTV->Release(); DoFBokehRTV = nullptr; }
	if (DoFBokehTexture) { DoFBokehTexture->Release(); DoFBokehTexture = nullptr; }
	DoFBokehWidth = 0;
	DoFBokehHeight = 0;
	if (DoFForegroundSRV) { DoFForegroundSRV->Release(); DoFForegroundSRV = nullptr; }
	if (DoFForegroundRTV) { DoFForegroundRTV->Release(); DoFForegroundRTV = nullptr; }
	if (DoFForegroundTexture) { DoFForegroundTexture->Release(); DoFForegroundTexture = nullptr; }
	if (DoFBackgroundSRV) { DoFBackgroundSRV->Release(); DoFBackgroundSRV = nullptr; }
	if (DoFBackgroundRTV) { DoFBackgroundRTV->Release(); DoFBackgroundRTV = nullptr; }
	if (DoFBackgroundTexture) { DoFBackgroundTexture->Release(); DoFBackgroundTexture = nullptr; }
	if (CoCSRV) { CoCSRV->Release(); CoCSRV = nullptr; }
	if (CoCRTV) { CoCRTV->Release(); CoCRTV = nullptr; }
	if (CoCTexture) { CoCTexture->Release(); CoCTexture = nullptr; }
	if (StencilCopySRV) { StencilCopySRV->Release(); StencilCopySRV = nullptr; }
	if (DepthCopySRV) { DepthCopySRV->Release(); DepthCopySRV = nullptr; }
	if (DepthCopyTexture) { DepthCopyTexture->Release(); DepthCopyTexture = nullptr; }
	if (DSV) { DSV->Release(); DSV = nullptr; }
	if (DepthTexture) { DepthTexture->Release(); DepthTexture = nullptr; }
	if (SRV) { SRV->Release(); SRV = nullptr; }
	if (RTV) { RTV->Release(); RTV = nullptr; }
	if (RTTexture) { RTTexture->Release(); RTTexture = nullptr; }
	if (SceneColorCopySRV) { SceneColorCopySRV->Release(); SceneColorCopySRV = nullptr; }
	if (SceneColorCopyTexture) { SceneColorCopyTexture->Release(); SceneColorCopyTexture = nullptr; }
}
