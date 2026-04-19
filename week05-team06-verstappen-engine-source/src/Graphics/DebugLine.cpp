#include "Graphics/DebugLine.h"
#include <d3dcompiler.h>
#include <cstring>
#include <DirectXMath.h>

namespace Graphics
{
    struct FDebugPerFrame
    {
        DirectX::XMFLOAT4X4 ViewProjection;
    };

    bool UDebugRenderer::Initialize(ID3D11Device* InDevice)
    {
        LineVertices.reserve(MAX_DEBUG_LINES * 2);
        const char* ShaderSrc = R"(
            cbuffer PerFrame : register(b0)
            {
                row_major float4x4 ViewProj;
            };

            struct VS_IN {
                float3 Pos : POSITION;
                float4 Color : COLOR;
            };

            struct PS_IN {
                float4 Pos : SV_POSITION;
                float4 Color : COLOR;
            };

            PS_IN VSMain(VS_IN i) {
                PS_IN o;
                o.Pos = mul(float4(i.Pos, 1.0f), ViewProj);
                o.Color = i.Color;
                return o;
            }

            float4 PSMain(PS_IN i) : SV_TARGET {
                return i.Color;
            }
        )";

        ComPtr<ID3DBlob> VS, PS, Err;
        if (FAILED(D3DCompile(ShaderSrc, std::strlen(ShaderSrc), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &VS, &Err))) return false;
        if (FAILED(D3DCompile(ShaderSrc, std::strlen(ShaderSrc), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &PS, &Err))) return false;

        if (FAILED(InDevice->CreateVertexShader(VS->GetBufferPointer(), VS->GetBufferSize(), nullptr, &VertexShader))) return false;
        if (FAILED(InDevice->CreatePixelShader(PS->GetBufferPointer(), PS->GetBufferSize(), nullptr, &PixelShader))) return false;

        D3D11_INPUT_ELEMENT_DESC Layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(InDevice->CreateInputLayout(Layout, 2, VS->GetBufferPointer(), VS->GetBufferSize(), &InputLayout))) return false;

        D3D11_BUFFER_DESC VbDesc = {};
        VbDesc.ByteWidth = sizeof(FDebugVertex) * MAX_DEBUG_LINES * 2;
        VbDesc.Usage = D3D11_USAGE_DYNAMIC;
        VbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        VbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(InDevice->CreateBuffer(&VbDesc, nullptr, &VertexBuffer))) return false;

        D3D11_BUFFER_DESC CbDesc = {};
        CbDesc.ByteWidth = sizeof(FDebugPerFrame);
        CbDesc.Usage = D3D11_USAGE_DYNAMIC;
        CbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        CbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(InDevice->CreateBuffer(&CbDesc, nullptr, &ConstantBuffer))) return false;

        return true;
    }

    void UDebugRenderer::AddLine(const DirectX::XMFLOAT3& Start, const DirectX::XMFLOAT3& End, const DirectX::XMFLOAT4& Color)
    {
        if (LineVertices.size() >= MAX_DEBUG_LINES * 2) return;
        LineVertices.push_back({ Start, Color });
        LineVertices.push_back({ End, Color });
    }

    void UDebugRenderer::AddBox(const Math::FBox& Box, const DirectX::XMFLOAT4& Color)
    {
        DirectX::XMFLOAT3 p0 = { Box.Min.x, Box.Min.y, Box.Min.z };
        DirectX::XMFLOAT3 p1 = { Box.Max.x, Box.Min.y, Box.Min.z };
        DirectX::XMFLOAT3 p2 = { Box.Max.x, Box.Max.y, Box.Min.z };
        DirectX::XMFLOAT3 p3 = { Box.Min.x, Box.Max.y, Box.Min.z };
        DirectX::XMFLOAT3 p4 = { Box.Min.x, Box.Min.y, Box.Max.z };
        DirectX::XMFLOAT3 p5 = { Box.Max.x, Box.Min.y, Box.Max.z };
        DirectX::XMFLOAT3 p6 = { Box.Max.x, Box.Max.y, Box.Max.z };
        DirectX::XMFLOAT3 p7 = { Box.Min.x, Box.Max.y, Box.Max.z };

        AddLine(p0, p1, Color); AddLine(p1, p2, Color); AddLine(p2, p3, Color); AddLine(p3, p0, Color);
        AddLine(p0, p4, Color); AddLine(p1, p5, Color); AddLine(p2, p6, Color); AddLine(p3, p7, Color);
        AddLine(p4, p5, Color); AddLine(p5, p6, Color); AddLine(p6, p7, Color); AddLine(p7, p4, Color);
    }

    void UDebugRenderer::AddSphere(const DirectX::XMFLOAT3& Center, float Radius, const DirectX::XMFLOAT4& Color, int Segments)
    {
        for (int i = 0; i < Segments; ++i)
        {
            float theta1 = (static_cast<float>(i) / Segments) * DirectX::XM_2PI;
            float theta2 = (static_cast<float>(i + 1) / Segments) * DirectX::XM_2PI;

            // X-Y plane circle
            AddLine(
                { Center.x + Radius * cosf(theta1), Center.y + Radius * sinf(theta1), Center.z },
                { Center.x + Radius * cosf(theta2), Center.y + Radius * sinf(theta2), Center.z },
                Color);

            // X-Z plane circle
            AddLine(
                { Center.x + Radius * cosf(theta1), Center.y, Center.z + Radius * sinf(theta1) },
                { Center.x + Radius * cosf(theta2), Center.y, Center.z + Radius * sinf(theta2) },
                Color);

            // Y-Z plane circle
            AddLine(
                { Center.x, Center.y + Radius * cosf(theta1), Center.z + Radius * sinf(theta1) },
                { Center.x, Center.y + Radius * cosf(theta2), Center.z + Radius * sinf(theta2) },
                Color);
        }
    }

    void UDebugRenderer::Render(ID3D11DeviceContext* InContext, const Math::FMatrix& InViewProjection)
    {
        if (LineVertices.empty()) return;

        D3D11_MAPPED_SUBRESOURCE CbMap;
        if (SUCCEEDED(InContext->Map(ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &CbMap)))
        {
            FDebugPerFrame CbData;
            DirectX::XMStoreFloat4x4(&CbData.ViewProjection, InViewProjection);
            std::memcpy(CbMap.pData, &CbData, sizeof(FDebugPerFrame));
            InContext->Unmap(ConstantBuffer.Get(), 0);
        }

        D3D11_MAPPED_SUBRESOURCE VbMap;
        if (SUCCEEDED(InContext->Map(VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &VbMap)))
        {
            std::memcpy(VbMap.pData, LineVertices.data(), sizeof(FDebugVertex) * LineVertices.size());
            InContext->Unmap(VertexBuffer.Get(), 0);
        }

        UINT Stride = sizeof(FDebugVertex);
        UINT Offset = 0;
        InContext->IASetVertexBuffers(0, 1, VertexBuffer.GetAddressOf(), &Stride, &Offset);
        InContext->IASetInputLayout(InputLayout.Get());

        InContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        InContext->VSSetShader(VertexShader.Get(), nullptr, 0);
        InContext->PSSetShader(PixelShader.Get(), nullptr, 0);
        InContext->VSSetConstantBuffers(0, 1, ConstantBuffer.GetAddressOf());

        InContext->Draw(static_cast<UINT>(LineVertices.size()), 0);

        LineVertices.clear();
    }
}
