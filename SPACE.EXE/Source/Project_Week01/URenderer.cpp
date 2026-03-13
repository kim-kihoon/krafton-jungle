#include "URenderer.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

void URenderer::Create(HWND hWindow) {
    CreateDeviceAndSwapChain(hWindow);
    CreateFrameBuffer();
    CreateRasterizerState();
}

void URenderer::CreateDeviceAndSwapChain(HWND hWindow) {
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

    DXGI_SWAP_CHAIN_DESC swapchaindesc = {};
    swapchaindesc.BufferDesc.Width = 0;
    swapchaindesc.BufferDesc.Height = 0;
    swapchaindesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapchaindesc.SampleDesc.Count = 1;
    swapchaindesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchaindesc.BufferCount = 2;
    swapchaindesc.OutputWindow = hWindow;
    swapchaindesc.Windowed = TRUE;
    swapchaindesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        //D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
        &swapchaindesc, &SwapChain, &Device, nullptr, &DeviceContext
    );

    SwapChain->GetDesc(&swapchaindesc);

    ViewportInfo = { 0.0f, 0.0f, (float)swapchaindesc.BufferDesc.Width * 0.75f, (float)swapchaindesc.BufferDesc.Height, 0.0f, 1.0f };
}

void URenderer::CreateFrameBuffer() {
    SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&FrameBuffer);

    D3D11_RENDER_TARGET_VIEW_DESC framebufferRTVdesc = {};
    //framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    framebufferRTVdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    framebufferRTVdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    Device->CreateRenderTargetView(FrameBuffer, &framebufferRTVdesc, &FrameBufferRTV);
}

void URenderer::CreateRasterizerState() {
    D3D11_RASTERIZER_DESC rasterizerdesc = {};
    rasterizerdesc.FillMode = D3D11_FILL_SOLID;
    rasterizerdesc.CullMode = D3D11_CULL_BACK;

    Device->CreateRasterizerState(&rasterizerdesc, &RasterizerState);
}

void URenderer::CreateShader() {
    ID3DBlob* vertexshaderCSO = nullptr;
    ID3DBlob* pixelshaderCSO = nullptr;

    D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainVS", "vs_5_0", 0, 0, &vertexshaderCSO, nullptr);
    Device->CreateVertexShader(vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), nullptr, &SimpleVertexShader);

    D3DCompileFromFile(L"ShaderW0.hlsl", nullptr, nullptr, "mainPS", "ps_5_0", 0, 0, &pixelshaderCSO, nullptr);
    Device->CreatePixelShader(pixelshaderCSO->GetBufferPointer(), pixelshaderCSO->GetBufferSize(), nullptr, &SimplePixelShader);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    Device->CreateInputLayout(layout, ARRAYSIZE(layout), vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), &SimpleInputLayout);
    D3D11_INPUT_ELEMENT_DESC texLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    Device->CreateInputLayout(texLayout, ARRAYSIZE(texLayout), vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), &textureInputLayout);

    if (vertexshaderCSO) vertexshaderCSO->Release();
    if (pixelshaderCSO) pixelshaderCSO->Release();
}

void URenderer::CreateConstantBuffer() {
    D3D11_BUFFER_DESC constantbufferdesc = {};
    // 16바이트 정렬 보정 로직 보강
    constantbufferdesc.ByteWidth = sizeof(DirectX::XMMATRIX);
    constantbufferdesc.Usage = D3D11_USAGE_DYNAMIC;
    constantbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    constantbufferdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    Device->CreateBuffer(&constantbufferdesc, nullptr, &ConstantBuffer);
}

void URenderer::Prepare() {
    DeviceContext->ClearRenderTargetView(FrameBufferRTV, ClearColor);
    DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    DeviceContext->RSSetViewports(1, &ViewportInfo);
    DeviceContext->RSSetState(RasterizerState);
    DeviceContext->OMSetRenderTargets(1, &FrameBufferRTV, nullptr);
}

void URenderer::PrepareShader() {
    DeviceContext->VSSetShader(SimpleVertexShader, nullptr, 0);
    DeviceContext->PSSetShader(SimplePixelShader, nullptr, 0);

    createSamplerState();

    if (ConstantBuffer) {
        DeviceContext->VSSetConstantBuffers(0, 1, &ConstantBuffer);
    }
}

// renderer 는 constant를 update하기만 할 것
void URenderer::UpdateConstant(DirectX::XMMATRIX pXMMATRIX) {
    if (ConstantBuffer) {
        D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
        if (SUCCEEDED(DeviceContext->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR))) {
            DirectX::XMMATRIX transposed = DirectX::XMMatrixTranspose(pXMMATRIX);
            memcpy(constantbufferMSR.pData, &transposed, sizeof(DirectX::XMMATRIX));
            DeviceContext->Unmap(ConstantBuffer, 0);
        }
    }
}

void URenderer::CreateVertexBuffer(ID3D11Buffer *& vertexbuffer , FTextureVertex* vertices, UINT bytewidth) {
    D3D11_BUFFER_DESC vertexbufferdesc = {};
    vertexbufferdesc.ByteWidth = bytewidth;
    vertexbufferdesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexbufferdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexbufferSRD = { vertices };
    Device->CreateBuffer(&vertexbufferdesc, &vertexbufferSRD, &vertexbuffer);
}




void URenderer::SwapBuffer() {
    SwapChain->Present(1, 0);
}

// --- Release 함수들 ---

void URenderer::Release() {
    if (DeviceContext) DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    ReleaseConstantBuffer();
    ReleaseShader();
    ReleaseRasterizerState();
    ReleaseFrameBuffer();
    ReleaseDeviceAndSwapChain();
}

void URenderer::ReleaseDeviceAndSwapChain() {
    if (DeviceContext) DeviceContext->Flush();
    if (SwapChain) SwapChain->Release();
    if (Device) Device->Release();
    if (DeviceContext) DeviceContext->Release();

    SwapChain = nullptr;
    Device = nullptr;
    DeviceContext = nullptr;
}

void URenderer::ReleaseFrameBuffer() {
    if (FrameBuffer) FrameBuffer->Release();
    if (FrameBufferRTV) FrameBufferRTV->Release();
    FrameBuffer = nullptr;
    FrameBufferRTV = nullptr;
}

void URenderer::ReleaseShader() {
    if (SimpleInputLayout) SimpleInputLayout->Release();
    if (SimplePixelShader) SimplePixelShader->Release();
    if (SimpleVertexShader) SimpleVertexShader->Release();
    SimpleInputLayout = nullptr;
    SimplePixelShader = nullptr;
    SimpleVertexShader = nullptr;
}

void URenderer::ReleaseRasterizerState() {
    if (RasterizerState) RasterizerState->Release();
    RasterizerState = nullptr;
}

void URenderer::createSamplerState()
{
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // 선형 필터링 (부드럽게)
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;    // UV가 1을 넘어가면 반복
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    Device->CreateSamplerState(&samplerDesc, &samplerState);
}

void URenderer::CreateBlendState() {
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    // PNG 투명도(알파) 혼합 공식 설정
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    Device->CreateBlendState(&blendDesc, &AlphaBlendState);
}



void URenderer::ReleaseVertexBuffer(ID3D11Buffer* vertexBuffer) {
    if (vertexBuffer) vertexBuffer->Release();
}

void URenderer::CreateIndexBuffer(ID3D11Buffer*& indexBuffer, UINT* indices, UINT count)
{
    D3D11_BUFFER_DESC indexBufferDesc = {};
    indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    indexBufferDesc.ByteWidth = sizeof(UINT) * count;
    indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    indexBufferDesc.CPUAccessFlags = 0;


    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = indices;

    Device->CreateBuffer(&indexBufferDesc, &initData, &indexBuffer);

}

void URenderer::ReleaseConstantBuffer() {
    if (ConstantBuffer) ConstantBuffer->Release();
    ConstantBuffer = nullptr;
}


void URenderer::indexRenderPrimitive(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, UINT numIndices)
{
    UINT offset = 0;

    // 1. 사용할 정점 버퍼 세팅
    DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &textureStride, &offset);

    // 2. 사용할 인덱스 버퍼 세팅 (unsigned int를 사용하므로 DXGI_FORMAT_R32_UINT 사용)
    DeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

    // 3. 그리기
    DeviceContext->DrawIndexed(numIndices, 0, 0);
}


void URenderer::RenderPrimitive(ID3D11Buffer* vertexBuffer, UINT vertexCount) {
    if (!vertexBuffer || !DeviceContext) return;

    UINT offset = 0;
    // 버퍼를 장치에 연결하고 그립니다.
    DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &textureStride, &offset);
    DeviceContext->Draw(vertexCount, 0);
}

void URenderer::textureRenderPrimitive(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, UINT numIndices, ID3D11ShaderResourceView* srv)
{
    UINT offset = 0;
    // 투명도가 있는 물체를 그리기 직전에 호출
    DeviceContext->OMSetBlendState(AlphaBlendState, nullptr, 0xFFFFFFFF);
    DeviceContext->IASetInputLayout(textureInputLayout);
    DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &textureStride, &offset);
    DeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
    DeviceContext->PSSetShaderResources(0, 1, &srv);
    DeviceContext->PSSetSamplers(0, 1, &samplerState);
    DeviceContext->DrawIndexed(numIndices, 0, 0);
}

