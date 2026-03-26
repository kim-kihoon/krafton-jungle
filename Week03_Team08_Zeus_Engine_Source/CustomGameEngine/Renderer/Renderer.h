#pragma once

#include "Renderer/RenderState.h"
#include "ConstantBuffer.h"
#include "Level.h"
#include "Math/Matrix.h"
#include "VertexSimple.h"
#include <d3d11.h>
#include "EngineTypes.h"
#include "Math/Color.h"
#include "DynamicFontAtlas.h"

struct EditorConfig;

class URenderer {
public:
	TComPtr<ID3D11Device> Device = nullptr;
	TComPtr<ID3D11DeviceContext> DeviceContext = nullptr;
	TComPtr<IDXGISwapChain> SwapChain = nullptr;

	TComPtr<ID3D11Texture2D> FrameBuffer = nullptr;
	TComPtr<ID3D11RenderTargetView> FrameBufferRTV = nullptr;
	TComPtr<ID3D11Texture2D> DepthStencilBuffer = nullptr;
	TComPtr<ID3D11DepthStencilView> DepthStencilView = nullptr;

	TComPtr<ID3D11RasterizerState> RasterizerState = nullptr;
	TComPtr<ID3D11RasterizerState> RasterizerStateNoCull = nullptr;
	TComPtr<ID3D11RasterizerState> RasterizerStateWireframe = nullptr;
	TComPtr<ID3D11RasterizerState> RasterizerStateWireframeNoCull = nullptr;

	TComPtr<ID3D11DepthStencilState> DepthStencilState = nullptr;
	TComPtr<ID3D11DepthStencilState> DepthStencilStateNoWrite = nullptr;

	TComPtr<ID3D11ShaderResourceView> FontAtlasSRV = nullptr;
	TComPtr<ID3D11SamplerState> SamplerState = nullptr;

	TComPtr<ID3D11BlendState> BlendState = nullptr;
	TComPtr<ID3D11BlendState> BlendStateAlpha = nullptr;

	FLOAT ClearColor[4] = { 0.025f, 0.025f, 0.025f, 1.0f };
	D3D11_VIEWPORT ViewportInfo;

public:
	void Create(HWND hWindow, int32 InitWidth, int32 InitHeight);
	void CreateDeviceAndSwapChain(HWND hWindow, int32 InitWidth, int32 InitHeight);
	void CreateFrameBuffer();
	void CreateRasterizerState();
	void CreateDepthStencilState();
	void CreateBlendState();
	void CreateConstantBuffer();
	void CreateSamplerState();

	void ReleaseDeviceAndSwapChain();
	void ReleaseFrameBuffer();
	void ReleaseRasterizerState();
	void ReleaseDepthStencilState();
	void ReleaseBlendState();
	void ReleaseConstantBuffer();
	void ReleaseSamplerState();
	void Release();

	void SwapBuffer();
	void OnResize(int32 Width, int32 Height);

public:

	void Prepare();


public:
	struct FFrameConstant
	{
		FMatrix View;
		FMatrix Projection;
		float CameraRight[3];
		float _pad0;
		float CameraUp[3];
		float _pad1;
	};
	struct FObjectConstant
	{
		FMatrix World;
		FColor Color;
		uint32 bIsSelected;
		uint32 ViewMode;
		float Padding[2];
	};
	struct FGridConstant
	{
		float CameraPos[3];
		float GridRadius;    // 페이드 아웃 반경
		float GridSize;      // 그리드 간격 (추가!)
		float Padding[3];    // 16바이트(float 4개) 정렬을 위한 더미 데이터
	};
	struct FSubUVConstant
	{
		float UVOffset[2];
		float UVScale[2];
	};

	FConstantBuffer FrameConstantBuffer; //View Proj
	FConstantBuffer ObjectConstantBuffer; //World
	FConstantBuffer FGridConstantBuffer;
	FConstantBuffer SubUVConstantBuffer;


public:
	void SetRenderCamera(class UCameraComponent* camera);
	void Render();
	void RenderGrid();

	int GetViewMode() const { return static_cast<int>(RenderState.ViewMode);  }
	void SetViewMode(int InIndex) 
	{ 
		RenderState.ViewMode = static_cast<EViewMode>(InIndex); 
	}
	FLevel drawLevel;

public:
	FDynamicFontAtlas FontAtlas;

	// TODO: RenderState 전역화. 따로 Renderer에서 분리해서 관리할지 고민
	static FRenderState& GetRenderState() { return RenderState; }

	void SetShowFlag(EShowFlag flag, bool bShow)
	{
		if (bShow) RenderState.ShowFlags |= (uint32)flag;
		else RenderState.ShowFlags &= ~(uint32)flag;
	}

	uint32* GetShowFlagsPtr() { return &RenderState.ShowFlags; }
	uint32 GetShowFlags() const { return RenderState.ShowFlags; }

	void ApplyConfig(const EditorConfig& config);
	void GatherConfig(EditorConfig& config);

	float GridRadius = 500.0f;
	float gridSize = 10.0f;


private:
	static FRenderState RenderState;
	
	UCameraComponent* renderCamera = nullptr;

	FMaterial* GridMaterial = nullptr;
};
