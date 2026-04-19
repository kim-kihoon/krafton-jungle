#pragma once
#include <Math/MathTypes.h>
#include <DirectXMath.h>
#include <vector>
#include <d3d11_1.h>
#include <wrl/client.h>

#include "Renderer.h"

using Microsoft::WRL::ComPtr;
namespace Graphics
{
    struct FDebugVertex
    {
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT4 Color;
    };

    class UDebugRenderer
    {
    public:
        UDebugRenderer() = default;
        ~UDebugRenderer() = default;

        bool Initialize(ID3D11Device* InDevice);

        void AddLine(const DirectX::XMFLOAT3& Start, const DirectX::XMFLOAT3& End, const DirectX::XMFLOAT4& Color = { 1.0f, 0.0f, 0.0f, 1.0f });
        void AddBox(const Math::FBox& Box, const DirectX::XMFLOAT4& Color = { 0.0f, 1.0f, 0.0f, 1.0f });
        void AddSphere(const DirectX::XMFLOAT3& Center, float Radius, const DirectX::XMFLOAT4& Color = { 0.0f, 0.0f, 1.0f, 1.0f }, int Segments = 12);

        void Render(ID3D11DeviceContext* InContext, const Math::FMatrix& InViewProjection);

    private:
        static constexpr uint32_t MAX_DEBUG_LINES = 500000;
        std::vector<FDebugVertex> LineVertices;

        ComPtr<ID3D11Buffer> VertexBuffer;
        ComPtr<ID3D11Buffer> ConstantBuffer; // ViewProj 전용 독립 버퍼
        ComPtr<ID3D11VertexShader> VertexShader;
        ComPtr<ID3D11PixelShader> PixelShader;
        ComPtr<ID3D11InputLayout> InputLayout;
    };
}
