#include <Graphics/HUD.h>
#include <Core/PlatformTime.h>
#include <cstdio>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <Core/PathManager.h>

#include "UI/Panels/SControlPanel.h"

namespace Graphics
{
    FHUD::FHUD() {}
    FHUD::~FHUD() {}

    bool FHUD::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
    {
        Device = InDevice;
        Context = InContext;
        Core::FPlatformTime::InitTiming();

        const char* ShaderSrc = R"(
            cbuffer HUDBuffer : register(b0) {
                row_major float4x4 OrthoProj;
            };
            Texture2D FontTexture : register(t0);
            SamplerState FontSampler : register(s0);

            struct VS_IN {
                float3 Pos : POSITION;
                float2 UV : TEXCOORD0;
            };
            struct PS_IN {
                float4 Pos : SV_POSITION;   
                float2 UV : TEXCOORD0;
            };

            PS_IN VSMain(VS_IN i) {
                PS_IN o;
                // 직교 투영 행렬을 곱해 Screen Space 좌표로 변환
                o.Pos = mul(float4(i.Pos, 1.0f), OrthoProj);
                o.UV = i.UV;
                return o;
            }

            float4 PSMain(PS_IN i) : SV_TARGET {
            float4 TexColor = FontTexture.Sample(FontSampler, i.UV);
           
            return float4(TexColor.rgb, TexColor.r); 
        }
        )";

        Microsoft::WRL::ComPtr<ID3DBlob> VS, PS, Err;
        if (FAILED(D3DCompile(ShaderSrc, strlen(ShaderSrc), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &VS, &Err))) return false;
        if (FAILED(D3DCompile(ShaderSrc, strlen(ShaderSrc), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &PS, &Err))) return false;
        if (FAILED(Device->CreateVertexShader(VS->GetBufferPointer(), VS->GetBufferSize(), nullptr, &VertexShader))) return false;
        if (FAILED(Device->CreatePixelShader(PS->GetBufferPointer(), PS->GetBufferSize(), nullptr, &PixelShader))) return false;

        D3D11_INPUT_ELEMENT_DESC Layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (FAILED(Device->CreateInputLayout(Layout, 2, VS->GetBufferPointer(), VS->GetBufferSize(), &InputLayout))) return false;

        D3D11_BUFFER_DESC VBDesc = {};
        VBDesc.ByteWidth = sizeof(FHUDVertex) * 4000;
        VBDesc.Usage = D3D11_USAGE_DYNAMIC; // 매 프레임 CPU가 업데이트해야 함
        VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        VBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(Device->CreateBuffer(&VBDesc, nullptr, &VertexBuffer))) return false;

        D3D11_BUFFER_DESC CBDesc = { sizeof(FHUDConstantBuffer), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(Device->CreateBuffer(&CBDesc, nullptr, &ConstantBuffer))) return false;

        D3D11_BLEND_DESC BlendDesc = {};
        BlendDesc.RenderTarget[0].BlendEnable = TRUE;
        BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(Device->CreateBlendState(&BlendDesc, &AlphaBlendState))) return false;

        D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
        DepthDesc.DepthEnable = FALSE;
        DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &DepthDisabledState))) return false;

        D3D11_SAMPLER_DESC SamplerDesc = {};
        SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        SamplerDesc.AddressU = SamplerDesc.AddressV = SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(Device->CreateSamplerState(&SamplerDesc, &SamplerState))) return false;

        // 폰트 텍스처 로드
        Microsoft::WRL::ComPtr<IWICImagingFactory> ImagingFactory;
        if (SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ImagingFactory))))
        {
            std::wstring FontFilePath = Core::FPathManager::GetDataPath() + L"Font/Font.png";
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> Decoder;
            HRESULT hr = ImagingFactory->CreateDecoderFromFilename(FontFilePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &Decoder);
            if (SUCCEEDED(hr))
            {
                Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> Frame;
                Decoder->GetFrame(0, &Frame);

                Microsoft::WRL::ComPtr<IWICFormatConverter> Converter;
                ImagingFactory->CreateFormatConverter(&Converter);
                Converter->Initialize(Frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);

                UINT Width, Height;
                Converter->GetSize(&Width, &Height);

                std::vector<uint8_t> Pixels(Width * Height * 4);
                Converter->CopyPixels(nullptr, Width * 4, (UINT)Pixels.size(), Pixels.data());

                D3D11_TEXTURE2D_DESC TexDesc = {};
                TexDesc.Width = Width;
                TexDesc.Height = Height;
                TexDesc.MipLevels = 1;
                TexDesc.ArraySize = 1;
                TexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                TexDesc.SampleDesc.Count = 1;
                TexDesc.Usage = D3D11_USAGE_DEFAULT;
                TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                D3D11_SUBRESOURCE_DATA InitData = { Pixels.data(), Width * 4, 0 };
                Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
                if (SUCCEEDED(Device->CreateTexture2D(&TexDesc, &InitData, &Texture)))
                {
                    Device->CreateShaderResourceView(Texture.Get(), nullptr, &FontTextureView);
                }
            }
        }

        return FontTextureView != nullptr;
    }

    void FHUD::Update(const Core::FFramePerformanceMetrics& InMetrics, int InScreenWidth, int InScreenHeight)
    {
        ScreenWidth = InScreenWidth;
        ScreenHeight = InScreenHeight;
        BatchedVertices.clear();

        static float UpdateTimer = 0.1f;
        static float UpdateTimer2 = 0.05f;
        static char Buffer6[256] = "FPS: ...";
        static char Buffer7[256] = "FPS: ...";

        char Buffer[256];
        char Buffer2[256];
        char Buffer3[256];
        char Buffer4[256];
        char Buffer5[256];

        float DeltaTime = (InMetrics.FramesPerSecond > 0.0f) ? (1.0f / InMetrics.FramesPerSecond) : 0.016f;
        UpdateTimer += DeltaTime;
        UpdateTimer2 += DeltaTime;

        if (UpdateTimer >= 0.05f)
        {
            float FrameMS = (InMetrics.FramesPerSecond > 0.0f) ? (1000.0f / InMetrics.FramesPerSecond) : 0.0f;
            std::snprintf(Buffer6, sizeof(Buffer6), "FPS: %.3f(%.3fms)", InMetrics.FramesPerSecond, FrameMS);
            UpdateTimer = 0.0f;
        }
        static char Buffer8[256] = "";
        static char Buffer9[256] = "";

        if (UpdateTimer2 >= 0.5f)
        {
            float FrameMS = (InMetrics.FramesPerSecond > 0.0f) ? (1000.0f / InMetrics.FramesPerSecond) : 0.0f;
            std::snprintf(Buffer7, sizeof(Buffer7), "FPS: %.3f(%.3fms)", InMetrics.FramesPerSecond, FrameMS);

            std::snprintf(Buffer8, sizeof(Buffer8), "Split=%.2f  Prepass=%.2f  HiZ=%.2f  Cull=%.2f  Draw=%.2f",
                          InMetrics.SplitTime, InMetrics.PrepassTime, InMetrics.HiZTime, InMetrics.CullTime,
                          InMetrics.DrawTime);

            UpdateTimer2 = 0.0f;
        }

        double LastPickingMS = Core::FPlatformTime::ToMilliseconds(InMetrics.LastPickingCycles);
        double TotalPickingMS = Core::FPlatformTime::ToMilliseconds(InMetrics.TotalPickingCycles);

        double GLastMS = Core::FPlatformTime::ToMilliseconds(InMetrics.GridLastPickingCycles);
        double GTotalMS = Core::FPlatformTime::ToMilliseconds(InMetrics.GridTotalPickingCycles);
        double GAvgMS =
            (InMetrics.GridTotalPickCount > 0) ? (GTotalMS / static_cast<double>(InMetrics.GridTotalPickCount)) : 0.0;

        double BLastMS = Core::FPlatformTime::ToMilliseconds(InMetrics.BVHLastPickingCycles);
        double BTotalMS = Core::FPlatformTime::ToMilliseconds(InMetrics.BVHTotalPickingCycles);
        double BAvgMS =
            (InMetrics.BVHTotalPickCount > 0) ? (BTotalMS / static_cast<double>(InMetrics.BVHTotalPickCount)) : 0.0;

        const char* StructureName = (InMetrics.CurrentStructure == Core::ESpatialStructure::UniformGrid) ? "Grid" : "BVH";

        std::snprintf(Buffer9, sizeof(Buffer9), "DrawCount: %u  PrevVisible=%u  PrevInvisible=%u  Total=%u",
                      InMetrics.DrawCount, InMetrics.PrevVisible, InMetrics.PrevInvisible, InMetrics.TotalObjectsCount);
        std::snprintf(Buffer, sizeof(Buffer), "[%s Mode] Picking Time %.4fms, RenderObjects: %u", StructureName, LastPickingMS,
                      InMetrics.RenderedObjectCount);
        std::snprintf(Buffer4, sizeof(Buffer4), "Grid Pick -> Last:%.4fms Avg:%.4fms Total:%.2fms (Cnt:%llu)", GLastMS,
                      GAvgMS, GTotalMS, (unsigned long long)InMetrics.GridTotalPickCount);
        std::snprintf(Buffer5, sizeof(Buffer5), "BVH  Pick -> Last:%.4fms Avg:%.4fms Total:%.2fms (Cnt:%llu)", BLastMS,
                      BAvgMS, BTotalMS, (unsigned long long)InMetrics.BVHTotalPickCount);
        std::snprintf(Buffer2, sizeof(Buffer2), "BVH Tests  -> Nodes: %u, Objects: %u", InMetrics.BVHNodeTestCount,
                      InMetrics.ObjectAABBTestCount);
        std::snprintf(Buffer3, sizeof(Buffer3), "Grid Tests -> Cells: %u, Objects: %u", InMetrics.GridCellTestCount,
                      InMetrics.GridObjectAABBTestCount);

        BatchText(Buffer6, 10.0f, 10.0f);
        BatchText(Buffer7, 10.0f, 30.0f); 
        BatchText(Buffer, 10.0f, 50.0f);  
        BatchText(Buffer4, 10.0f, 75.0f);
        BatchText(Buffer5, 10.0f, 100.0f);
        BatchText(Buffer2, 10.0f, 125.0f);
        BatchText(Buffer3, 10.0f, 150.0f);
        BatchText(Buffer8, 10.0f, 175.0f);
        BatchText(Buffer9, 10.0f, 200.0f);
    }

    void FHUD::Render()
    {
        if (BatchedVertices.empty() || !FontTextureView) return;

        D3D11_MAPPED_SUBRESOURCE MappedVB = {};
        if (SUCCEEDED(Context->Map(VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedVB)))
        {
            memcpy(MappedVB.pData, BatchedVertices.data(), sizeof(FHUDVertex) * BatchedVertices.size());
            Context->Unmap(VertexBuffer.Get(), 0);
        }

        D3D11_MAPPED_SUBRESOURCE MappedCB = {};
        if (SUCCEEDED(Context->Map(ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedCB)))
        {
            FHUDConstantBuffer CB;
            DirectX::XMMATRIX Ortho = DirectX::XMMatrixOrthographicOffCenterLH(0.0f, (float)ScreenWidth, (float)ScreenHeight, 0.0f, 0.0f, 1.0f);
            DirectX::XMStoreFloat4x4(&CB.OrthoProjection, Ortho);
            memcpy(MappedCB.pData, &CB, sizeof(FHUDConstantBuffer));
            Context->Unmap(ConstantBuffer.Get(), 0);
        }

        UINT Stride = sizeof(FHUDVertex);
        UINT Offset = 0;
        Context->IASetInputLayout(InputLayout.Get());
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->IASetVertexBuffers(0, 1, VertexBuffer.GetAddressOf(), &Stride, &Offset);

        Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Context->VSSetConstantBuffers(0, 1, ConstantBuffer.GetAddressOf());

        Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Context->PSSetShaderResources(0, 1, FontTextureView.GetAddressOf());
        Context->PSSetSamplers(0, 1, SamplerState.GetAddressOf());

        Context->OMSetBlendState(AlphaBlendState.Get(), nullptr, 0xFFFFFFFF);
        Context->OMSetDepthStencilState(DepthDisabledState.Get(), 0);

        Context->Draw((UINT)BatchedVertices.size(), 0);

        Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        Context->OMSetDepthStencilState(nullptr, 0);
    }

    void FHUD::BatchText(const char* InText, float InX, float InY)
    {
        float CursorX = InX;
        float CursorY = InY;

        const float CharRenderSize = 32.0f;
        const float CursorAdvanceX = CharRenderSize * 0.6f;
        const float UVStep = 1.0f / 16.0f;

        for (int i = 0; InText[i] != '\0'; i++)
        {
            unsigned char Character = (unsigned char)InText[i];

            int Column = Character % 16;
            int Row = Character / 16;

            float U = Column * UVStep;
            float V = Row * UVStep;

            // 정점 위치를 계산할 때는 CharRenderSize를 사용
            FHUDVertex V1 = { { CursorX, CursorY, 0.0f }, { U, V } };
            FHUDVertex V2 = { { CursorX + CharRenderSize, CursorY, 0.0f }, { U + UVStep, V } };
            FHUDVertex V3 = { { CursorX, CursorY + CharRenderSize, 0.0f }, { U, V + UVStep } };
            FHUDVertex V4 = { { CursorX + CharRenderSize, CursorY + CharRenderSize, 0.0f }, { U + UVStep, V + UVStep } };

            BatchedVertices.push_back(V1); BatchedVertices.push_back(V2); BatchedVertices.push_back(V3);
            BatchedVertices.push_back(V3); BatchedVertices.push_back(V2); BatchedVertices.push_back(V4);

            CursorX += CursorAdvanceX;
        }
    }
}
