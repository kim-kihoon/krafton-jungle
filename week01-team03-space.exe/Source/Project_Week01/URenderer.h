#pragma once

#include "datatype.h"
#include <DirectXMath.h> 

#include <d3d11.h>
#include <d3dcompiler.h>

constexpr float WindowWidth = 1400.f;
constexpr float WindowHeight = 1050.f;
constexpr float GameWidth = 1050.f;
constexpr float GameHeight = 1050.f;

// 사용하는 구조체들에 대한 가정 (사용자 정의 구조체)
class URenderer {
    

public:
    URenderer() = default;
    ~URenderer() { Release(); }

    // 초기화 및 해제
    void Create(HWND hWindow);
    void Release();

    // 프레임 관리
    void Prepare();
    void SwapBuffer();

    // 셰이더 및 상수 버퍼 관련
    void CreateShader();
    void PrepareShader();
    void ReleaseShader();

    void CreateConstantBuffer();
    void UpdateConstant(const DirectX::XMMATRIX pXMMATRIX);
    void ReleaseConstantBuffer();

    // 버퍼 생성
    void CreateVertexBuffer(ID3D11Buffer*& vertexbuffer, FTextureVertex* vertices, UINT bytewidth);
    void CreateIndexBuffer(ID3D11Buffer*& indexBuffer, UINT* indices, UINT count);

    // 렌더링
    void ReleaseVertexBuffer(ID3D11Buffer* vertexBuffer);

    void textureRenderPrimitive(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, UINT numIndices, ID3D11ShaderResourceView * srv);

private:
    // 내부 초기화 메서드
    void CreateDeviceAndSwapChain(HWND hWindow);
    void ReleaseDeviceAndSwapChain();

    void CreateFrameBuffer();
    void ReleaseFrameBuffer();

    void CreateRasterizerState();
    void ReleaseRasterizerState();

    // 안쓰는 렌더러
    void RenderPrimitive(ID3D11Buffer* vertexBuffer, UINT vertexCount);

public:
    void indexRenderPrimitive(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, UINT numIndices);
public:
    // 주요 D3D11 인터페이스
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* DeviceContext = nullptr;
    IDXGISwapChain* SwapChain = nullptr;

    ID3D11Texture2D* FrameBuffer = nullptr;
    ID3D11RenderTargetView* FrameBufferRTV = nullptr;
    ID3D11RasterizerState* RasterizerState = nullptr;
    ID3D11Buffer* ConstantBuffer = nullptr;

    ID3D11VertexShader* SimpleVertexShader = nullptr;
    ID3D11PixelShader* SimplePixelShader = nullptr;
    ID3D11InputLayout* SimpleInputLayout = nullptr;
    ID3D11InputLayout* textureInputLayout = nullptr;

    //texture
    ID3D11SamplerState* samplerState = nullptr;
    void createSamplerState();

    // blend
    ID3D11BlendState* AlphaBlendState = nullptr;
    void CreateBlendState();

    // 렌더링 상태 데이터
    //FLOAT ClearColor[4] = { 0.05f, 0.02f, 0.1f, 1.0f };
    FLOAT ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3D11_VIEWPORT ViewportInfo{};
    
    unsigned int textureStride = static_cast<unsigned int>(sizeof(FTextureVertex));

};