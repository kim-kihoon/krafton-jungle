#pragma once
#include <Core/AppTypes.h>
#include <memory>
#include <vector>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <DirectXMath.h>

namespace Graphics
{
   struct FHUDVertex
   {
       DirectX::XMFLOAT3 Position;
       DirectX::XMFLOAT2 UV;
   };

    struct FHUDConstantBuffer
    {
        DirectX::XMFLOAT4X4 OrthoProjection;
    };

    class FHUD
    {
    public:
        FHUD();
        ~FHUD();

        bool Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);
        void Update(const Core::FFramePerformanceMetrics& InMetrics, int InScreenWidth, int InScreenHeight);
        void Render();

    private:
        void BatchText(const char* InText, float InX, float InY);

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;

        Microsoft::WRL::ComPtr<ID3D11Buffer> VertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> ConstantBuffer;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;

        // 2D UI 렌더링 전용 상태 객체들
        Microsoft::WRL::ComPtr<ID3D11BlendState> AlphaBlendState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DepthDisabledState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> SamplerState;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> FontTextureView;

        std::vector<FHUDVertex> BatchedVertices;
        int ScreenWidth = 0;
        int ScreenHeight = 0;
    };
}
