#include "Renderer.h"
#include "Component/CameraComponent.h"
#include "Component/LineBatchComponent.h"
#include "World.h"
#include "ResourceManager.h"
#include "EngineStatics.h"
#include "Viewport.h"
#include "Editor/Editor.h"

FRenderState URenderer::RenderState;

void URenderer::Create(HWND hWindow, int32 InitWidth, int32 InitHeight) {
	CreateDeviceAndSwapChain(hWindow, InitWidth, InitHeight);

	CreateFrameBuffer();

	CreateRasterizerState();
	CreateDepthStencilState();
	CreateBlendState();

	CreateConstantBuffer();
	CreateSamplerState();

	FViewport::OnResizeDelegate.AddRaw(this, &URenderer::OnResize);
	Editor::OnEditorConfigLoaded.AddRaw(this, &URenderer::ApplyConfig);
	Editor::OnEditorConfigSaveReady.AddRaw(this, &URenderer::GatherConfig);
}

void URenderer::CreateDeviceAndSwapChain(HWND hWindow, int32 InitWidth, int32 InitHeight) {
	D3D_FEATURE_LEVEL featurelevels[] = { D3D_FEATURE_LEVEL_11_0 };

	UINT Width = InitWidth;
	UINT Height = InitHeight;

	DXGI_SWAP_CHAIN_DESC swapchaindesc = {};
	swapchaindesc.BufferDesc.Width = Width;
	swapchaindesc.BufferDesc.Height = Height;
	swapchaindesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapchaindesc.SampleDesc.Count = 1;
	swapchaindesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchaindesc.BufferCount = 2;
	swapchaindesc.OutputWindow = hWindow;
	swapchaindesc.Windowed = TRUE;
	swapchaindesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
		featurelevels, ARRAYSIZE(featurelevels), D3D11_SDK_VERSION,
		&swapchaindesc, SwapChain.GetAddressOf(), Device.GetAddressOf(), nullptr, DeviceContext.GetAddressOf());

	SwapChain->GetDesc(&swapchaindesc);

	ViewportInfo = { 0.0f,
					0.0f,
					(float)swapchaindesc.BufferDesc.Width,
					(float)swapchaindesc.BufferDesc.Height,
					0.0f,
					1.0f };
}

void URenderer::CreateBlendState()
{
	D3D11_BLEND_DESC desc = {};
	desc.AlphaToCoverageEnable = FALSE;
	desc.IndependentBlendEnable = FALSE;

	D3D11_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];

	//rt.BlendEnable = TRUE;
	// 불투명 상태에서의 블렌딩 설정
	rt.BlendEnable = FALSE;
	rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	Device->CreateBlendState(&desc, BlendState.GetAddressOf());

	// 반투명 상태에서의 블렌딩 설정
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D11_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D11_BLEND_ONE;
	rt.DestBlendAlpha = D3D11_BLEND_ZERO;
	rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	Device->CreateBlendState(&desc, BlendStateAlpha.GetAddressOf());
}

void URenderer::ReleaseDeviceAndSwapChain() {
	if (DeviceContext) {
		DeviceContext->Flush();
	}
	SwapChain.Reset();
	DeviceContext.Reset();
	Device.Reset();
}

void URenderer::CreateFrameBuffer() {
	SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)FrameBuffer.GetAddressOf()); //백버퍼 가져오기

	D3D11_RENDER_TARGET_VIEW_DESC framebufferRTVdesc = {};
	framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	framebufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	Device->CreateRenderTargetView(FrameBuffer.Get(), &framebufferRTVdesc,
		FrameBufferRTV.GetAddressOf());

	D3D11_TEXTURE2D_DESC depthBufferDesc = {};
	FrameBuffer->GetDesc(&depthBufferDesc);
	depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	depthBufferDesc.CPUAccessFlags = 0;
	depthBufferDesc.MiscFlags = 0;

	Device->CreateTexture2D(&depthBufferDesc, nullptr, DepthStencilBuffer.GetAddressOf());
	if (DepthStencilBuffer)
	{
		Device->CreateDepthStencilView(DepthStencilBuffer.Get(), nullptr, DepthStencilView.GetAddressOf());
	}
}

void URenderer::ReleaseFrameBuffer() {
	DepthStencilView.Reset();
	DepthStencilBuffer.Reset();
	FrameBuffer.Reset();
	FrameBufferRTV.Reset();
}

void URenderer::CreateRasterizerState() {
	D3D11_RASTERIZER_DESC rasterizerdesc = {};
	
	// Solid Mode
	rasterizerdesc.FillMode = D3D11_FILL_SOLID;
	rasterizerdesc.CullMode = D3D11_CULL_BACK;
	Device->CreateRasterizerState(&rasterizerdesc, RasterizerState.GetAddressOf());

	rasterizerdesc.CullMode = D3D11_CULL_NONE;
	Device->CreateRasterizerState(&rasterizerdesc, RasterizerStateNoCull.GetAddressOf());

	// Wireframe Mode
	rasterizerdesc.FillMode = D3D11_FILL_WIREFRAME;
	rasterizerdesc.CullMode = D3D11_CULL_NONE;
	Device->CreateRasterizerState(&rasterizerdesc, RasterizerStateWireframeNoCull.GetAddressOf());
}

void URenderer::ReleaseRasterizerState() {
	RasterizerState.Reset();
	RasterizerStateNoCull.Reset();
}

void URenderer::CreateDepthStencilState() {
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_GREATER;
	depthStencilDesc.StencilEnable = FALSE;

	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	Device->CreateDepthStencilState(&depthStencilDesc, DepthStencilState.GetAddressOf());

	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	Device->CreateDepthStencilState(&depthStencilDesc, DepthStencilStateNoWrite.GetAddressOf());
}

void URenderer::ReleaseDepthStencilState() {
	DepthStencilState.Reset();
	DepthStencilStateNoWrite.Reset();
}

void URenderer::ReleaseBlendState()
{
	BlendState.Reset();
}

void URenderer::Release() {
	FViewport::OnResizeDelegate.RemoveRaw(this);

	ReleaseSamplerState();
	ReleaseBlendState();
	ReleaseDepthStencilState();
	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	ReleaseRasterizerState();
	ReleaseFrameBuffer();
	ReleaseDeviceAndSwapChain();
	ReleaseConstantBuffer();
}

void URenderer::SwapBuffer() { SwapChain->Present(0, 0); }

void URenderer::OnResize(int32 Width, int32 Height)
{
	if (!Device || !SwapChain) return;

	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	ReleaseFrameBuffer();

	HRESULT hr = SwapChain->ResizeBuffers(0, Width, Height, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) 
	{
		return;
	}

	CreateFrameBuffer();

	ViewportInfo.Width = float(Width);
	ViewportInfo.Height = float(Height);
	ViewportInfo.MinDepth = 0.0f;
	ViewportInfo.MaxDepth = 1.0f;
	ViewportInfo.TopLeftX = 0.0f;
	ViewportInfo.TopLeftY = 0.0f;
	DeviceContext->RSSetViewports(1, &ViewportInfo);
}

void URenderer::Prepare() {
	DeviceContext->ClearRenderTargetView(FrameBufferRTV.Get(), ClearColor);
	DeviceContext->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
	DeviceContext->RSSetViewports(1, &ViewportInfo);
	DeviceContext->RSSetState(RasterizerState.Get());
	DeviceContext->OMSetRenderTargets(1, FrameBufferRTV.GetAddressOf(), DepthStencilView.Get());
	DeviceContext->OMSetDepthStencilState(DepthStencilState.Get(), 0);
	float blendFactor[4] = { 0,0,0,0 };
	DeviceContext->OMSetBlendState(BlendState.Get(), blendFactor, 0xffffffff);
}

void URenderer::CreateConstantBuffer() {
	FrameConstantBuffer.Create(Device.Get(), sizeof(FFrameConstant), 0);
	FrameConstantBuffer.Bind(DeviceContext.Get());
	ObjectConstantBuffer.Create(Device.Get(), sizeof(FObjectConstant), 1);
	ObjectConstantBuffer.Bind(DeviceContext.Get());

	FGridConstantBuffer.Create(Device.Get(), sizeof(FGridConstant), 2);
	FGridConstantBuffer.Bind(DeviceContext.Get());

	SubUVConstantBuffer.Create(Device.Get(), sizeof(FSubUVConstant), 3);
	SubUVConstantBuffer.Bind(DeviceContext.Get());
}

void URenderer::ReleaseConstantBuffer() {
	FrameConstantBuffer.Release();
	ObjectConstantBuffer.Release();
	FGridConstantBuffer.Release();
	SubUVConstantBuffer.Release();
}

void URenderer::CreateSamplerState()
{
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	Device->CreateSamplerState(&samplerDesc, SamplerState.GetAddressOf());
}

void URenderer::ReleaseSamplerState()
{
	if (SamplerState)
	{
		SamplerState.Reset();
	}
}

void URenderer::SetRenderCamera(class UCameraComponent* camera)
{
	renderCamera = camera;
}

void URenderer::Render()
{
	if (renderCamera == nullptr) return;

	UEngineStatics::TotalDrawCalls = 0;

	FFrameConstant FrameConst;
	FrameConst.Projection = renderCamera->GetProjectionMatrix();
	FrameConst.View = renderCamera->GetViewMatrix();
	FrameConst.CameraRight[0] = renderCamera->SideDirection.x;
	FrameConst.CameraRight[1] = renderCamera->SideDirection.y;
	FrameConst.CameraRight[2] = renderCamera->SideDirection.z;
	FrameConst.CameraUp[0] = renderCamera->UpDirection.x;
	FrameConst.CameraUp[1] = renderCamera->UpDirection.y;
	FrameConst.CameraUp[2] = renderCamera->UpDirection.z;
	FrameConstantBuffer.Update(DeviceContext.Get(), &FrameConst, sizeof(FFrameConstant)); 

	RenderGrid();

	TArray<RenderObject*> OpaqueObjects;
	TArray<RenderObject*> OpaqueNoCullObjects;
	TArray<RenderObject*> TransparentObjects;
	TArray<RenderObject*> NoDepthRenderObjects;

	for (RenderObject* RenderObj : FLevel::GetInstance().RenderObjects)
	{
		if (!RenderObj->bIsVisible) continue;
		
		if (!RenderObj->bDepthEnabled)
		{
			NoDepthRenderObjects.push_back(RenderObj);
		}
		else if (RenderObj->bIsSelected) // TODO: 선택 효과 변경되면 이 부분도 수정 필요
		{
			TransparentObjects.push_back(RenderObj);
		}
		else if (!RenderObj->bBackfaceCulling)
		{
			OpaqueNoCullObjects.push_back(RenderObj);
		}
		else
		{
			OpaqueObjects.push_back(RenderObj);
		}
	}

	EViewMode ViewMode = RenderState.ViewMode;
	uint32 ViewModeIndex = static_cast<uint32>(ViewMode);
	uint32 ShowFlags = RenderState.ShowFlags;

	if (ViewMode == EViewMode::Wireframe) DeviceContext->RSSetState(RasterizerStateWireframeNoCull.Get());
	else DeviceContext->RSSetState(RasterizerState.Get());

	FObjectConstant ObjectConst;
	float blendFactor[4] = { 0, 0, 0, 0 };

	DeviceContext->OMSetDepthStencilState(DepthStencilState.Get(), 0);
	DeviceContext->OMSetBlendState(BlendState.Get(), blendFactor, 0xffffffff);

	for (RenderObject* RenderObj : OpaqueObjects)
	{
		if (!(ShowFlags & (uint32)RenderObj->ShowFlag)) continue;
		ObjectConst.World = RenderObj->World;
		ObjectConst.Color = RenderObj->Color;
		ObjectConst.ViewMode = ViewModeIndex;
		ObjectConstantBuffer.Update(DeviceContext.Get(), &ObjectConst, sizeof(FObjectConstant));
		RenderObj->Render(DeviceContext.Get());
	}

	DeviceContext->RSSetState(RasterizerStateNoCull.Get());

	for (RenderObject* RenderObj : OpaqueNoCullObjects)
	{
		if (!(ShowFlags & (uint32)RenderObj->ShowFlag)) continue;
		ObjectConst.World = RenderObj->World;
		ObjectConst.Color = RenderObj->Color;
		ObjectConst.ViewMode = ViewModeIndex;
		ObjectConstantBuffer.Update(DeviceContext.Get(), &ObjectConst, sizeof(FObjectConstant));
		RenderObj->Render(DeviceContext.Get());
	}

	// Transparent Objects Rendering: Depth Write OFF, Blending ON
	DeviceContext->OMSetDepthStencilState(DepthStencilStateNoWrite.Get(), 0);
	if (ViewMode == EViewMode::Wireframe) DeviceContext->RSSetState(RasterizerStateWireframeNoCull.Get());
	else DeviceContext->RSSetState(RasterizerState.Get());
	DeviceContext->OMSetBlendState(BlendStateAlpha.Get(), blendFactor, 0xffffffff);

	for (RenderObject* RenderObj : TransparentObjects)
	{
		if (!(ShowFlags & (uint32)RenderObj->ShowFlag)) continue;
		ObjectConst.World = RenderObj->World;
		ObjectConst.Color = RenderObj->Color;
		ObjectConst.ViewMode = (uint32)ViewModeIndex;
		ObjectConstantBuffer.Update(DeviceContext.Get(), &ObjectConst, sizeof(FObjectConstant));
		RenderObj->Render(DeviceContext.Get());
	}

	// Just clear depth for no-depth objects, keep blending state same as transparent objects for line rendering
	// For depth test in gizmo, we just clear depth view.
	DeviceContext->OMSetDepthStencilState(DepthStencilState.Get(), 0);
	DeviceContext->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
	DeviceContext->RSSetState(RasterizerStateNoCull.Get());
	DeviceContext->OMSetBlendState(BlendState.Get(), blendFactor, 0xffffffff);

	for (RenderObject* RenderObj : NoDepthRenderObjects)
	{
		if (!(ShowFlags & (uint32)RenderObj->ShowFlag)) continue;
		ObjectConst.World = RenderObj->World;
		ObjectConst.Color = RenderObj->Color;
		ObjectConst.ViewMode = (uint32)EViewMode::Unlit;
		ObjectConstantBuffer.Update(DeviceContext.Get(), &ObjectConst, sizeof(FObjectConstant));
		RenderObj->Render(DeviceContext.Get());
	}
}

// 셰이더를 사용한 grid
void URenderer::RenderGrid()
{
	if (!(RenderState.ShowFlags & (uint32)EShowFlag::Grid)) return;

	if (GridMaterial == nullptr)
	{
		GridMaterial = &ResourceManager::GetInstance()->GetMaterial(L"Asset/Shader/Grid.hlsl");
	}

	// 매터리얼이나 컴파일된 셰이더가 없다면 그리지 않고 바로 리턴 (크래시 방지)
	if (GridMaterial == nullptr || GridMaterial->VertexShader == nullptr) return;

	auto camera = UCameraComponent::GetMainCamera();
	if (camera == nullptr)
		return;

	if (gridSize <= 5.0f) gridSize = 5.0f;

	FGridConstant GridConst = {};
	FVector CamPos = renderCamera->GetRelativeLocation();
	GridConst.CameraPos[0] = CamPos.x;
	GridConst.CameraPos[1] = CamPos.y;
	GridConst.CameraPos[2] = CamPos.z;
	GridConst.GridRadius = GridRadius; // 페이드 아웃 반경 (원하는 대로 조절)
	GridConst.GridSize = gridSize;

	FGridConstantBuffer.Update(DeviceContext.Get(), &GridConst, sizeof(FGridConstant));

	// 2. 렌더링 상태 설정
	// 그리드가 다른 오브젝트를 가리지 않도록 Depth Write는 끄고(NoWrite), 알파 블렌딩 적용
	float blendFactor[4] = { 0, 0, 0, 0 };
	DeviceContext->OMSetDepthStencilState(DepthStencilStateNoWrite.Get(), 0);
	DeviceContext->OMSetBlendState(BlendStateAlpha.Get(), blendFactor, 0xffffffff);
	DeviceContext->RSSetState(RasterizerStateNoCull.Get()); // 양면을 모두 렌더링

	// 3. 버텍스 버퍼 바인딩 해제 및 셰이더 세팅
	// 정점 데이터 없이 SV_VertexID 만으로 렌더링할 것이므로 버퍼를 비워줍니다.
	UINT stride = 0;
	UINT offset = 0;
	ID3D11Buffer* nullBuffer = nullptr;
	DeviceContext->IASetVertexBuffers(0, 1, &nullBuffer, &stride, &offset);
	DeviceContext->IASetInputLayout(nullptr);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	DeviceContext->VSSetShader(GridMaterial->VertexShader.Get(), nullptr, 0);
	DeviceContext->PSSetShader(GridMaterial->PixelShader.Get(), nullptr, 0);

	// 4. 드로우 콜 (정점 6개 = 거대한 사각형 1개)
	DeviceContext->Draw(6, 0);
	UEngineStatics::TotalDrawCalls++;
}

//void URenderer::RenderGrid()
//{
//	if (!(RenderState.ShowFlags & (uint32)EShowFlag::Grid)) return;
//
//	if (GridMaterial == nullptr)
//	{
//		GridMaterial = &ResourceManager::GetInstance()->GetMaterial(L"Asset/Shader/Grid.hlsl");
//	}
//
//	// 매터리얼이나 컴파일된 셰이더가 없다면 그리지 않고 바로 리턴 (크래시 방지)
//	if (GridMaterial == nullptr || GridMaterial->VertexShader == nullptr) return;
//
//	auto camera = UCameraComponent::GetMainCamera();
//	if (camera == nullptr)
//		return;
//
//	FVector camPosition = camera->GetRelativeLocation();
//
//	float gridRadius = GridRadius;
//	if (gridSize <= 5.0f) gridSize = 5.0f;
//
//	FGridConstant GridConst = {};
//	GridConst.CameraPos[0] = camPosition.x;
//	GridConst.CameraPos[1] = camPosition.y;
//	GridConst.CameraPos[2] = camPosition.z;
//	GridConst.GridRadius = gridRadius;
//	GridConst.GridSize = gridSize;
//	FGridConstantBuffer.Update(DeviceContext.Get(), &GridConst, sizeof(FGridConstant));
//
//	int halfLineCount = static_cast<int>(gridRadius / gridSize);
//	float ActualRadius = halfLineCount * gridSize;
//	float snappedX = std::floor(camPosition.x / gridSize) * gridSize;
//	float snappedY = std::floor(camPosition.y / gridSize) * gridSize;
//
//	FColor color = FColor(0.3f, 0.3f, 0.3f, 0.3f);
//
//	for (int i = -halfLineCount; i <= halfLineCount; ++i)
//	{
//		// 1. 가로선 (X축 평행선)
//		float currentY = snappedY + (i * gridSize);
//		FVector StartX(snappedX - ActualRadius, currentY, 0.0f);
//		FVector EndX(snappedX + ActualRadius, currentY, 0.0f);
//
//		GetWorld().DrawDebugLine(StartX, EndX, color, false, EShowFlag::Grid);
//
//		// 2. 세로선 (Y축 평행선)
//		float currentX = snappedX + (i * gridSize);
//		FVector StartY(currentX, snappedY - ActualRadius, 0.0f);
//		FVector EndY(currentX, snappedY + ActualRadius, 0.0f);
//
//		GetWorld().DrawDebugLine(StartY, EndY, color, false, EShowFlag::Grid);
//	}
//}

void URenderer::ApplyConfig(const EditorConfig& config)
{
	gridSize = config.GridSize;
	RenderState.ViewMode = static_cast<EViewMode>(config.ViewMode);
	RenderState.ShowFlags = config.ShowFlags;
}

void URenderer::GatherConfig(EditorConfig& config)
{
	config.GridSize = gridSize;
	config.ViewMode = static_cast<uint32>(RenderState.ViewMode);
	config.ShowFlags = RenderState.ShowFlags;
}