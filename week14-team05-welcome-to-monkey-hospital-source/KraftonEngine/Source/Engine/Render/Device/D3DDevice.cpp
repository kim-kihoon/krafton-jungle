#include "D3DDevice.h"

#include <array>

//	Safe Release Macro
#define SAFE_RELEASE(Obj) if (Obj) { Obj->Release(); Obj = nullptr; }

void FD3DDevice::Create(HWND InHWindow)
{
	CreateDeviceAndSwapChain(InHWindow);
	CreateFrameBuffer();
	CreateDepthStencilBuffer();
}

void FD3DDevice::Release()
{
	// Idempotent shutdown. Some editor/viewport paths can already have torn down render targets
	// before the renderer itself is released. Never touch a null or already-released context.
	if (!Device && !DeviceContext && !SwapChain)
	{
		return;
	}

	// First detach everything from the immediate context. This is intentionally broader than
	// ClearState-only: D3D debug layer is much happier when RTV/DSV/SRV/UAV/IA/SO bindings are
	// explicitly nulled before owner objects start releasing their COM references.
	ReleaseImmediateContextBindings(false);

	ReleaseDepthStencilBuffer();
	ReleaseFrameBuffer();

	ReleaseDeviceAndSwapChain();
}

void FD3DDevice::ReleaseImmediateContextBindings(bool bFlush)
{
	ID3D11DeviceContext* Ctx = DeviceContext;
	if (!Ctx)
	{
		return;
	}

	// Output merger
	Ctx->OMSetRenderTargets(0, nullptr, nullptr);
	ID3D11RenderTargetView* NullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	Ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, NullRTVs, nullptr);

	// UAVs can be bound by compute/tile-culling passes. D3D11.0 exposes 8 CS UAV slots.
	ID3D11UnorderedAccessView* NullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
	UINT InitialCounts[D3D11_PS_CS_UAV_REGISTER_COUNT] = {};
	Ctx->CSSetUnorderedAccessViews(0, D3D11_PS_CS_UAV_REGISTER_COUNT, NullUAVs, InitialCounts);

	// Shader resources
	ID3D11ShaderResourceView* NullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
	Ctx->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, NullSRVs);
	Ctx->HSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, NullSRVs);
	Ctx->DSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, NullSRVs);
	Ctx->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, NullSRVs);
	Ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, NullSRVs);
	Ctx->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, NullSRVs);

	// Constant buffers
	ID3D11Buffer* NullCBs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
	Ctx->VSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, NullCBs);
	Ctx->HSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, NullCBs);
	Ctx->DSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, NullCBs);
	Ctx->GSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, NullCBs);
	Ctx->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, NullCBs);
	Ctx->CSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, NullCBs);

	// Samplers
	ID3D11SamplerState* NullSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
	Ctx->VSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, NullSamplers);
	Ctx->HSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, NullSamplers);
	Ctx->DSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, NullSamplers);
	Ctx->GSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, NullSamplers);
	Ctx->PSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, NullSamplers);
	Ctx->CSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, NullSamplers);

	// Shaders
	Ctx->VSSetShader(nullptr, nullptr, 0);
	Ctx->HSSetShader(nullptr, nullptr, 0);
	Ctx->DSSetShader(nullptr, nullptr, 0);
	Ctx->GSSetShader(nullptr, nullptr, 0);
	Ctx->PSSetShader(nullptr, nullptr, 0);
	Ctx->CSSetShader(nullptr, nullptr, 0);

	// Input assembler / stream output
	ID3D11Buffer* NullVBs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
	UINT NullStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
	UINT NullOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
	Ctx->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, NullVBs, NullStrides, NullOffsets);
	Ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	Ctx->IASetInputLayout(nullptr);
	ID3D11Buffer* NullSOTargets[D3D11_SO_BUFFER_SLOT_COUNT] = {};
	UINT NullSOOffsets[D3D11_SO_BUFFER_SLOT_COUNT] = {};
	Ctx->SOSetTargets(D3D11_SO_BUFFER_SLOT_COUNT, NullSOTargets, NullSOOffsets);

	Ctx->ClearState();
	if (bFlush)
	{
		Ctx->Flush();
	}
}

void FD3DDevice::BeginFrame()
{
	DeviceContext->ClearRenderTargetView(FrameBufferRTV, ClearColor);
	DeviceContext->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);

	DeviceContext->RSSetViewports(1, &ViewportInfo);
	DeviceContext->OMSetRenderTargets(1, &FrameBufferRTV, DepthStencilView);
}

void FD3DDevice::Present()
{
	UINT PresentFlags = bTearingSupported ? DXGI_PRESENT_ALLOW_TEARING : 0;
	SwapChain->Present(0, PresentFlags);
}

void FD3DDevice::OnResizeViewport(int Width, int Height)
{
	if (!SwapChain) return;

	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

	ReleaseFrameBuffer();
	ReleaseDepthStencilBuffer();

	SwapChain->ResizeBuffers(0, Width, Height, DXGI_FORMAT_UNKNOWN, SwapChainFlags);

	ViewportInfo.Width = static_cast<float>(Width);
	ViewportInfo.Height = static_cast<float>(Height);

	CreateFrameBuffer();
	CreateDepthStencilBuffer();
}

ID3D11Device* FD3DDevice::GetDevice() const
{
	return Device;
}

ID3D11DeviceContext* FD3DDevice::GetDeviceContext() const
{
	return DeviceContext;
}

void FD3DDevice::CreateDeviceAndSwapChain(HWND InHWindow)
{
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferDesc.Width = 0;
	swapChainDesc.BufferDesc.Height = 0;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.OutputWindow = InHWindow;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	// Check tearing support for no-vsync with flip model
	IDXGIFactory5* Factory5 = nullptr;
	{
		IDXGIFactory1* Factory1 = nullptr;
		if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&Factory1)))
		{
			if (SUCCEEDED(Factory1->QueryInterface(__uuidof(IDXGIFactory5), (void**)&Factory5)))
			{
				Factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
					&bTearingSupported, sizeof(bTearingSupported));
			}
			Factory1->Release();
		}
	}

	if (bTearingSupported)
	{
		swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}

	SwapChainFlags = swapChainDesc.Flags;

	UINT CreateDeviceFlags = 0;
#ifdef _DEBUG
	CreateDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		CreateDeviceFlags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
		&swapChainDesc, &SwapChain, &Device, nullptr, &DeviceContext);

#ifdef _DEBUG
	ID3D11InfoQueue* InfoQueue = nullptr;
	if (SUCCEEDED(Device->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&InfoQueue)))
	{
		D3D11_MESSAGE_ID HideMessages[] =
		{
					 D3D11_MESSAGE_ID_DEVICE_DRAW_RENDERTARGETVIEW_NOT_SET, // Warning #3146081 무시
		};

		D3D11_INFO_QUEUE_FILTER Filter = {};
		Filter.DenyList.NumIDs = ARRAYSIZE(HideMessages);
		Filter.DenyList.pIDList = HideMessages;

		InfoQueue->AddStorageFilterEntries(&Filter);
		InfoQueue->Release();
	}
#endif

	// CPU가 GPU보다 1프레임 이상 앞서지 못하게 제한
	// (기본값 3 → Present 큐 깊이로 인한 FPS 톱니파 현상 방지)
	{
		IDXGIDevice1* DXGIDevice = nullptr;
		if (SUCCEEDED(Device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&DXGIDevice)))
		{
			DXGIDevice->SetMaximumFrameLatency(1);
			DXGIDevice->Release();
		}
	}

	if (Factory5) Factory5->Release();

	SwapChain->GetDesc(&swapChainDesc);

	ViewportInfo = { 0, 0, float(swapChainDesc.BufferDesc.Width), float(swapChainDesc.BufferDesc.Height), 0, 1 };
}

void FD3DDevice::ReleaseDeviceAndSwapChain()
{
	// Final detach/flush before releasing swap chain and context.
	ReleaseImmediateContextBindings(true);

	SAFE_RELEASE(SwapChain);
	SAFE_RELEASE(DeviceContext);

#ifdef _DEBUG
	// 릭 진단: Device 해제 직전 남아있는 D3D 객체 리포트
	if (Device)
	{
		ID3D11Debug* Debug = nullptr;
		if (SUCCEEDED(Device->QueryInterface(__uuidof(ID3D11Debug), (void**)&Debug)))
		{
			OutputDebugStringA("=== ReportLiveDeviceObjects BEGIN ===\n");
			Debug->ReportLiveDeviceObjects(
				static_cast<D3D11_RLDO_FLAGS>(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL));
			OutputDebugStringA("=== ReportLiveDeviceObjects END ===\n");
			Debug->Release();
		}
		else
		{
			OutputDebugStringA("=== ID3D11Debug QueryInterface FAILED ===\n");
		}
	}
#endif

	SAFE_RELEASE(Device);
}

void FD3DDevice::CreateFrameBuffer()
{
	SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&FrameBuffer);

	CD3D11_RENDER_TARGET_VIEW_DESC frameBufferRTVDesc = {};
	frameBufferRTVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	frameBufferRTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	Device->CreateRenderTargetView(FrameBuffer, &frameBufferRTVDesc, &FrameBufferRTV);
}

void FD3DDevice::ReleaseFrameBuffer()
{
	SAFE_RELEASE(FrameBufferRTV);
	SAFE_RELEASE(FrameBuffer);
}

void FD3DDevice::CreateDepthStencilBuffer()
{
	D3D11_TEXTURE2D_DESC depthStencilDesc = {};
	depthStencilDesc.Width = static_cast<uint32>(ViewportInfo.Width);
	depthStencilDesc.Height = static_cast<uint32>(ViewportInfo.Height);
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.ArraySize = 1;
	depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;	//	Depth 24bit + Stencil 8bit
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
	depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	Device->CreateTexture2D(&depthStencilDesc, nullptr, &DepthStencilBuffer);
	Device->CreateDepthStencilView(DepthStencilBuffer, nullptr, &DepthStencilView);
}

void FD3DDevice::ReleaseDepthStencilBuffer()
{
	SAFE_RELEASE(DepthStencilView);
	SAFE_RELEASE(DepthStencilBuffer);
}
