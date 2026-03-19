#include "pch.h"
#include "Renderer.h"
#include "World.h"
#include "Object.h"
#include "PrimitiveComponent.h"
#include "FEditorViewportClient.h"
#include "Actor.h"


URenderer* GRenderer = nullptr;

URenderer::URenderer(ID3D11Device* _Device, ID3D11DeviceContext* _DeviceContext, IDXGISwapChain* _SwapChain, FEditorViewportClient& _ViewportClient)
	: Device(_Device), DeviceContext(_DeviceContext), SwapChain(_SwapChain), ViewportClient(_ViewportClient)
{
	GRenderer = this;
}

void URenderer::Initialize()
{
	DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
	SwapChain->GetDesc(&SwapChainDesc);
	ViewportInfo = { 0.0f, 0.0f, (float)SwapChainDesc.BufferDesc.Width, (float)SwapChainDesc.BufferDesc.Height, 0.0f, 1.0f };

	CreateRenderTargetView();
	CreateDepthStensilView();
	CreateRasterizerState();
	CreateDepthStencilState();

	CreateShader(*Device, TEXT("ShaderW0.hlsl"), FVertexSimple::Elements, FVertexSimple::ElementNum);
	CreateConstantBuffer();

	CreateLineAxisBuffer();
	CreateGridBuffer();
	CreateGridShader();
	CreateAlphaBlendState();

}

void URenderer::BeginScene()
{
	if (!DepthStensilView)
	{
		return;
	}

	FLOAT ClearColor[4] = { 0.025f, 0.025f, 0.025f, 1.0f };
	DeviceContext->ClearRenderTargetView(BackBufferRTV, ClearColor);
	DeviceContext->ClearDepthStencilView(DepthStensilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);

	DeviceContext->RSSetViewports(1, &ViewportInfo);
	DeviceContext->RSSetState(RasterizerStateDefault);
	DeviceContext->OMSetRenderTargets(1, &BackBufferRTV, DepthStensilView);
	DeviceContext->OMSetDepthStencilState(DepthStencilState, 1);

	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
}

void URenderer::Render(UWorld* World)
{
	if (!World) return;

	DebugRenderList.Clear();

	// 셰이더
	DeviceContext->VSSetShader(SimpleVertexShader, nullptr, 0);
	DeviceContext->PSSetShader(SimplePixelShader, nullptr, 0);
	DeviceContext->IASetInputLayout(SimpleInputLayout);

	RenderAxisLine();
	RenderPrimitive(World);
#ifdef _DEBUG
	RenderDebug();
#endif // _DEBUG

}

void URenderer::EndScene()
{
	//프레임 제한 해제
	SwapChain->Present(0, 0);
	// 제한
	//SwapChain->Present(1, 0);
}

void URenderer::Release()
{
	ReleaseAlphaBlendState();
	ReleaseGridShader();
	ReleaseGridBuffer();
	ReleaseLineAxisBuffer();
	ReleaseConstantBuffer();
	ReleaseShader();

	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	ReleaseDepthStencilState();
	ReleaseRasterizerState();
	ReleaseDepthStensilView();
	ReleaseRenderTargetView();
}

void URenderer::UpdateConstantBuffer(ID3D11DeviceContext& Context, const FMatrix& MVP, const FVector4& Color)
{
	if (!ConstantBuffer) return;

	FConstantData Data;
	Data.MVP = MVP;
	Data.Color = Color;

	D3D11_MAPPED_SUBRESOURCE MSR;
	Context.Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MSR);
	memcpy(MSR.pData, &Data, sizeof(FConstantData));
	Context.Unmap(ConstantBuffer, 0);

	Context.VSSetConstantBuffers(0, 1, &ConstantBuffer);
	Context.PSSetConstantBuffers(0, 1, &ConstantBuffer);
}

void URenderer::CreateRasterizerState()
{
	D3D11_RASTERIZER_DESC RasterizerDesc = {};
	RasterizerDesc.FillMode = D3D11_FILL_SOLID;
	RasterizerDesc.CullMode = D3D11_CULL_NONE;
	RasterizerDesc.DepthClipEnable = TRUE;

	Device->CreateRasterizerState(&RasterizerDesc, &RasterizerStateDefault);


	RasterizerDesc = {};
	RasterizerDesc.FillMode = D3D11_FILL_SOLID;
	RasterizerDesc.CullMode = D3D11_CULL_NONE;
	RasterizerDesc.FrontCounterClockwise = FALSE; 
	RasterizerDesc.DepthClipEnable = TRUE;
	RasterizerDesc.DepthBias = 10000;
	RasterizerDesc.SlopeScaledDepthBias = 1.0f;

	Device->CreateRasterizerState(&RasterizerDesc, &RasterizerStateOutline);


	RasterizerDesc = {};
	RasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
	RasterizerDesc.CullMode = D3D11_CULL_BACK;
	RasterizerDesc.FrontCounterClockwise = FALSE;
	RasterizerDesc.DepthClipEnable = TRUE;

	Device->CreateRasterizerState(&RasterizerDesc, &RasterizerStateDebug);

	DeviceContext->RSSetState(RasterizerStateDefault);

}

void URenderer::ReleaseRasterizerState()
{
	if (RasterizerStateDebug)
	{
		RasterizerStateDebug->Release();
		RasterizerStateDebug = nullptr;
	}

	if (RasterizerStateDefault)
	{
		RasterizerStateDefault->Release();
		RasterizerStateDefault = nullptr;
	}

	if (RasterizerStateOutline)
	{
		RasterizerStateOutline->Release();
		RasterizerStateOutline = nullptr;
	}
}


void URenderer::CreateRenderTargetView()
{
	SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&BackBuffer);

	// 렌더 타겟 뷰 생성
	D3D11_RENDER_TARGET_VIEW_DESC BackBufferRTVdesc = {};
	BackBufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	BackBufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	assert(BackBuffer);
	Device->CreateRenderTargetView(BackBuffer, &BackBufferRTVdesc, &BackBufferRTV);
}

void URenderer::ReleaseRenderTargetView()
{
	if (BackBufferRTV)
	{
		BackBufferRTV->Release();
		BackBufferRTV = nullptr;
	}

	if (BackBuffer)
	{
		BackBuffer->Release();
		BackBuffer = nullptr;
	}

}

void URenderer::CreateDepthStensilView()
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = static_cast<UINT>(ViewportInfo.Width);
	desc.Height = static_cast<UINT>(ViewportInfo.Height);

	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	Device->CreateTexture2D(&desc, nullptr, &DepthBuffer);

	D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
	viewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D.MipSlice = 0;

	assert(DepthBuffer);
	Device->CreateDepthStencilView(DepthBuffer, &viewDesc, &DepthStensilView);
}

void URenderer::ReleaseDepthStensilView()
{
	if (DepthStensilView)
	{
		DepthStensilView->Release();
		DepthStensilView = nullptr;
	}

	if (DepthBuffer)
	{
		DepthBuffer->Release();
		DepthBuffer = nullptr;
	}
}

void URenderer::CreateDepthStencilState()
{
	D3D11_DEPTH_STENCIL_DESC dsDesc = {};

	dsDesc.DepthEnable = TRUE;
	dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
	dsDesc.StencilEnable = FALSE;
	Device->CreateDepthStencilState(&dsDesc, &DepthStencilState);


	D3D11_DEPTH_STENCIL_DESC dsDescNoDepth = {};
	dsDescNoDepth.DepthEnable = FALSE;
	dsDescNoDepth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dsDescNoDepth.DepthFunc = D3D11_COMPARISON_LESS;
	dsDescNoDepth.StencilEnable = FALSE;

	Device->CreateDepthStencilState(&dsDesc, &DepthStencilStateNoDepth);


}

void URenderer::ReleaseDepthStencilState()
{
	if (DepthStencilState)
	{
		DepthStencilState->Release();
		DepthStencilState = nullptr;
	}

	if (DepthStencilStateNoDepth)
	{
		DepthStencilStateNoDepth->Release();
		DepthStencilStateNoDepth = nullptr;
	}
}

void URenderer::CreateShader(ID3D11Device& Device, const std::wstring& Filename, const D3D11_INPUT_ELEMENT_DESC Layout[], int ElementNum)
{
	ID3DBlob* VertexShaderCSO = nullptr;
	ID3DBlob* PixelShaderCSO = nullptr;

	// Vertex Shader 컴파일
	HRESULT hr = D3DCompileFromFile(Filename.c_str(), nullptr, nullptr, "VS_MAIN", "vs_5_0", 0, 0, &VertexShaderCSO, nullptr);
	assert(hr != E_FAIL && "vs compile err");

	hr = Device.CreateVertexShader(VertexShaderCSO->GetBufferPointer(), VertexShaderCSO->GetBufferSize(), nullptr, &SimpleVertexShader);
	assert(hr != E_FAIL && "vs creation failed");

	// 레이아웃 생성
	hr = Device.CreateInputLayout(Layout, ElementNum, VertexShaderCSO->GetBufferPointer(), VertexShaderCSO->GetBufferSize(), &SimpleInputLayout);
	VertexShaderCSO->Release();
	assert(hr != E_FAIL && "inputLayout creation failed");

	// Pixel Shader 컴파일
	hr = D3DCompileFromFile(Filename.c_str(), nullptr, nullptr, "PS_MAIN", "ps_5_0", 0, 0, &PixelShaderCSO, nullptr);
	assert(hr != E_FAIL && "ps compile err");

	hr = Device.CreatePixelShader(PixelShaderCSO->GetBufferPointer(), PixelShaderCSO->GetBufferSize(), nullptr, &SimplePixelShader);
	PixelShaderCSO->Release();
	assert(hr != E_FAIL && "ps creation failed");
}


void URenderer::ReleaseShader()
{
	if (SimplePixelShader)
	{
		SimplePixelShader->Release();
		SimplePixelShader = nullptr;
	}

	if (SimpleInputLayout)
	{
		SimpleInputLayout->Release();
		SimpleInputLayout = nullptr;
	}

	if (SimpleVertexShader)
	{
		SimpleVertexShader->Release();
		SimpleVertexShader = nullptr;
	}
}

void URenderer::CreateConstantBuffer()
{
	D3D11_BUFFER_DESC constantbufferdesc = {};
	constantbufferdesc.ByteWidth = (sizeof(FConstantData) + 0xf) & 0xfffffff0; // 16바이트 정렬
	constantbufferdesc.Usage = D3D11_USAGE_DYNAMIC; // 프레임마다 CPU가 갱신해주어야 하므로
	constantbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // Usage는 사용 패턴만 정의. CPU가 접근할 수 있도록 플래그도 설정해주어야 함
	constantbufferdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	Device->CreateBuffer(&constantbufferdesc, nullptr, &ConstantBuffer);
}

void URenderer::ReleaseConstantBuffer()
{
	if (ConstantBuffer)
	{
		ConstantBuffer->Release();
		ConstantBuffer = nullptr;
	}
}

void URenderer::CreateLineAxisBuffer()
{
	FVertexSimple Axis_Vertices[2] =
	{
		{0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f}, {0.f, 1000.f,  0.f,  0.f, 1.f, 0.f, 1.f}
	};

	D3D11_BUFFER_DESC vertexBufferDesc = {};
	vertexBufferDesc.ByteWidth = sizeof(FVertexSimple) * 2;
	vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA vertexBufferSRD = { Axis_Vertices };

	Device->CreateBuffer(&vertexBufferDesc, &vertexBufferSRD, &LineAxisBuffer);
}

void URenderer::ReleaseLineAxisBuffer()
{
	LineAxisBuffer->Release();
}

void URenderer::CreateGridBuffer()
{
	const float S = 1000.f;
	FVertexSimple GridQuad[6] =
	{
		{ -S, -0.01f, -S,  0,0,0,0 },
		{  S, -0.01f, -S,  0,0,0,0 },
		{  S, -0.01f,  S,  0,0,0,0 },
		{ -S, -0.01f, -S,  0,0,0,0 },
		{  S, -0.01f,  S,  0,0,0,0 },
		{ -S, -0.01f,  S,  0,0,0,0 },
	};
	GridVertexCount = 6;

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth  = sizeof(GridQuad);
	desc.Usage      = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags  = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA srd = { GridQuad };
	Device->CreateBuffer(&desc, &srd, &GridBuffer);
}

void URenderer::ReleaseGridBuffer()
{
	if (GridBuffer)
	{
		GridBuffer->Release();
		GridBuffer = nullptr;
	}
}

void URenderer::CreateGridShader()
{
	ID3DBlob* VSBlob = nullptr;
	ID3DBlob* PSBlob = nullptr;

	HRESULT hr = D3DCompileFromFile(TEXT("ShaderGrid.hlsl"), nullptr, nullptr, "VS_GRID", "vs_5_0", 0, 0, &VSBlob, nullptr);
	assert(SUCCEEDED(hr) && "ShaderGrid VS compile failed");
	Device->CreateVertexShader(VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), nullptr, &GridVertexShader);
	VSBlob->Release();

	hr = D3DCompileFromFile(TEXT("ShaderGrid.hlsl"), nullptr, nullptr, "PS_GRID", "ps_5_0", 0, 0, &PSBlob, nullptr);
	assert(SUCCEEDED(hr) && "ShaderGrid PS compile failed");
	Device->CreatePixelShader(PSBlob->GetBufferPointer(), PSBlob->GetBufferSize(), nullptr, &GridPixelShader);
	PSBlob->Release();
}

void URenderer::ReleaseGridShader()
{
	if (GridVertexShader) { GridVertexShader->Release(); GridVertexShader = nullptr; }
	if (GridPixelShader)  { GridPixelShader->Release();  GridPixelShader  = nullptr; }
}

void URenderer::CreateAlphaBlendState()
{
	D3D11_BLEND_DESC desc = {};
	desc.RenderTarget[0].BlendEnable           = TRUE;
	desc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
	desc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
	desc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
	desc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
	desc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
	desc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
	desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	Device->CreateBlendState(&desc, &AlphaBlendState);
}

void URenderer::ReleaseAlphaBlendState()
{
	if (AlphaBlendState) { AlphaBlendState->Release(); AlphaBlendState = nullptr; }
}

void URenderer::UpdateConstantBuffer(const FConstantData& Data)
{
	D3D11_MAPPED_SUBRESOURCE MSR;
	DeviceContext->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MSR);
	memcpy(MSR.pData, &Data, sizeof(FConstantData));
	DeviceContext->Unmap(ConstantBuffer, 0);
	DeviceContext->VSSetConstantBuffers(0, 1, &ConstantBuffer);
}

void URenderer::RenderGrid()
{
	FVector CameraOffset = { ViewportClient.GetViewLocation().X, 0.f, ViewportClient.GetViewLocation().Z };
	FMatrix LocationMatrix = FMatrix::MakeTranslation(CameraOffset);

	FMatrix  MVP     = LocationMatrix * ViewportClient.GetViewMatrix() * ViewportClient.GetProjectionMatrix();
	FVector  CamPos = ViewportClient.GetViewLocation();

	UpdateConstantBuffer({ MVP, FVector4(CamPos.X, CamPos.Y, CamPos.Z, 0.f) });

	DeviceContext->VSSetShader(GridVertexShader, nullptr, 0);
	DeviceContext->PSSetShader(GridPixelShader,  nullptr, 0);
	DeviceContext->IASetInputLayout(SimpleInputLayout);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	float BlendFactor[4] = {};
	DeviceContext->OMSetBlendState(AlphaBlendState, BlendFactor, 0xffffffff);

	UINT Stride = sizeof(FVertexSimple), Offset = 0;
	DeviceContext->IASetVertexBuffers(0, 1, &GridBuffer, &Stride, &Offset);
	DeviceContext->Draw(GridVertexCount, 0);

	DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	DeviceContext->VSSetShader(SimpleVertexShader, nullptr, 0);
	DeviceContext->PSSetShader(SimplePixelShader,  nullptr, 0);
}

void URenderer::RenderAxisLine()
{
	if (!ConstantBuffer) return;

	// 셰이더 기반 그리드 렌더링
	RenderGrid();

	// 축라인 렌더
	FMatrix VP = ViewportClient.GetViewMatrix() * ViewportClient.GetProjectionMatrix();
	UpdateConstantBuffer({ VP, FVector4() });

	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	UINT Stride = sizeof(FVertexSimple), Offset = 0;
	DeviceContext->IASetVertexBuffers(0, 1, &LineAxisBuffer, &Stride, &Offset);
	DeviceContext->Draw(2, 0);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}


void URenderer::OnResize(UINT width, UINT height)
{

	if (width == 0 || height == 0) return;

	ID3D11RenderTargetView* nullRTV = nullptr;
	DeviceContext->OMSetRenderTargets(1, &nullRTV, nullptr);

	ReleaseRenderTargetView();
	ReleaseDepthStensilView();

	DeviceContext->Flush();

	HRESULT hr = SwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) {
		return;
	}

	ViewportInfo.Width = (float)width;
	ViewportInfo.Height = (float)height;
	   
	CreateRenderTargetView();
	CreateDepthStensilView();

	DeviceContext->RSSetViewports(1, &ViewportInfo);
	DeviceContext->OMSetRenderTargets(1, &BackBufferRTV, DepthStensilView);

	ViewportClient.SetAspectRatio((float)width / (float)height);

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)width, (float)height);

}

void URenderer::RenderPrimitive(UWorld* World)
{
	FMatrix VP = ViewportClient.GetViewMatrix() * ViewportClient.GetProjectionMatrix();
	TArray<AActor*> Actors = World->CurrentLevel->Actors;

	// 화면 위(최상단)에 렌더링할 기즈모 컴포넌트들을 담아둘 대기열
	TArray<UPrimitiveComponent*> ForegroundPrimitives;

	for (size_t i = 0; i < Actors.Size(); ++i)
	{
		TArray<UPrimitiveComponent*> Primitives = Actors[i]->GetComponentArrayByClass<UPrimitiveComponent>();

		for (uint32 j = 0; j < Primitives.Size(); ++j)
		{
			UPrimitiveComponent* Primitive = Primitives[j];
			if (!Primitive || !Primitive->GetMesh()) continue;

			if (Primitive->IsAlwaysOnTop())
			{
				ForegroundPrimitives.PushBack(Primitive);
				continue;
			}

			if (Primitive->IsDebugMode())
			{
				DebugRenderList.PushBack(Primitive);
				continue;
			}

			FMatrix Model = Primitive->GetComponentTransform();

			// 아웃라인 패스
			if (Actors[i]->IsSelected())
			{
				FMatrix OutlineModel = FMatrix::MakeScale(FVector(1.05f, 1.05f, 1.05f)) * Model;
				UpdateConstantBuffer({ OutlineModel * VP, FVector4(FVector(1.f, 0.22f, 0.f), 1.f) });
				DeviceContext->RSSetState(RasterizerStateOutline);
				Primitive->Render(*DeviceContext);
			}
			// 원본 패스
			UpdateConstantBuffer({ Model * VP, Primitive->GetColor() });
			DeviceContext->RSSetState(RasterizerStateDefault);
			Primitive->Render(*DeviceContext);
		}
	}

	if (ForegroundPrimitives.Size() > 0)
	{
		DeviceContext->ClearDepthStencilView(DepthStensilView, D3D11_CLEAR_DEPTH, 1.f, 0);

		for (uint32 i = 0; i < ForegroundPrimitives.Size(); ++i)
		{
			UPrimitiveComponent* Primitive = ForegroundPrimitives[i];
			FMatrix Model = Primitive->GetComponentTransform();

			// 기즈모는 외곽선 없이 본체만 강제로 위에 덧그립니다.
			UpdateConstantBuffer({ Model * VP, Primitive->GetColor() });
			DeviceContext->RSSetState(RasterizerStateDefault);
			Primitive->Render(*DeviceContext);

		}
		DeviceContext->OMSetDepthStencilState(DepthStencilState, 1);
		DeviceContext->RSSetState(RasterizerStateDefault);
	}

}

#ifdef  _DEBUG
void URenderer::RenderDebug()
{
	FMatrix VP = ViewportClient.GetViewMatrix() * ViewportClient.GetProjectionMatrix();

	DeviceContext->OMSetDepthStencilState(DepthStencilStateNoDepth, 0);
	DeviceContext->RSSetState(RasterizerStateDebug);

	for (size_t i = 0; i < DebugRenderList.Size(); ++i)
	{
		FMatrix Model = DebugRenderList[i]->GetComponentTransform();
		UpdateConstantBuffer({ Model * VP,  FVector4(FVector(0.f, 1.f, 0.f), 1.f) });

		DebugRenderList[i]->Render(*DeviceContext);
	}

	DeviceContext->OMSetDepthStencilState(DepthStencilState, 1);
	DeviceContext->RSSetState(RasterizerStateDefault);
}
#endif //  _DEBUG
