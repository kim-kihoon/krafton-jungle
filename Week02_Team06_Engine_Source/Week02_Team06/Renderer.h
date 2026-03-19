#pragma once

extern class URenderer* GRenderer;

class UWorld;
class FEditorViewportClient;
class UPrimitiveComponent;

class URenderer
{
public:
	URenderer(ID3D11Device* _Device, ID3D11DeviceContext* _DeviceContext, IDXGISwapChain* _SwapChain, FEditorViewportClient& _ViewportClient);
	~URenderer() = default;

public:
	void Initialize();

	void BeginScene();

	void Render(UWorld* World);
	
	void EndScene();

	void Release();

	void OnResize(UINT width, UINT height);

public:
	void UpdateConstantBuffer(ID3D11DeviceContext& Context, const FMatrix& MVP, const FVector4& Color = FVector4());

private:
	void CreateRasterizerState();
	void ReleaseRasterizerState();

	void CreateRenderTargetView();
	void ReleaseRenderTargetView();

	void CreateDepthStensilView();
	void ReleaseDepthStensilView();

	void CreateDepthStencilState();
	void ReleaseDepthStencilState();

	void CreateShader(ID3D11Device& Device, const std::wstring& Filename, const D3D11_INPUT_ELEMENT_DESC Layout[], int ElemnetNum);
	void ReleaseShader();

	void CreateConstantBuffer();
	void ReleaseConstantBuffer();

	void CreateLineAxisBuffer();
	void ReleaseLineAxisBuffer();

	void CreateGridBuffer();
	void ReleaseGridBuffer();

	void CreateGridShader();
	void ReleaseGridShader();

	void CreateAlphaBlendState();
	void ReleaseAlphaBlendState();

private:
	void UpdateConstantBuffer(const FConstantData& Data);
	void RenderAxisLine();
	void RenderGrid();
	void RenderPrimitive(UWorld* World);

#ifdef _DEBUG
	void RenderDebug();
#endif

private:
	ID3D11Device* Device = { nullptr };
	ID3D11DeviceContext* DeviceContext = { nullptr };
	IDXGISwapChain* SwapChain = { nullptr };

	ID3D11Texture2D* BackBuffer = { nullptr };
	ID3D11RenderTargetView* BackBufferRTV = { nullptr };

	ID3D11Texture2D* DepthBuffer = { nullptr };
	ID3D11DepthStencilView* DepthStensilView = { nullptr };

	ID3D11RasterizerState* RasterizerStateDefault     = { nullptr };
	ID3D11RasterizerState* RasterizerStateOutline     = { nullptr };
	ID3D11RasterizerState* RasterizerStateDebug		  = { nullptr };

	ID3D11DepthStencilState* DepthStencilStateNoDepth = { nullptr };
	ID3D11DepthStencilState* DepthStencilState = { nullptr };

	ID3D11Buffer* ConstantBuffer = { nullptr };

	D3D11_VIEWPORT ViewportInfo = {};


	//쉐이더
	ID3D11InputLayout*  SimpleInputLayout  = nullptr;
	ID3D11VertexShader* SimpleVertexShader = nullptr;
	ID3D11PixelShader*  SimplePixelShader  = nullptr;

	//라인
	ID3D11Buffer* LineAxisBuffer = { nullptr };

	//그리드
	ID3D11Buffer*       GridBuffer        = { nullptr };
	int                 GridVertexCount   = 0;
	ID3D11VertexShader* GridVertexShader  = { nullptr };
	ID3D11PixelShader*  GridPixelShader   = { nullptr };
	ID3D11BlendState*   AlphaBlendState   = { nullptr };

	FEditorViewportClient& ViewportClient;

	TArray<UPrimitiveComponent*> DebugRenderList = {};
};

