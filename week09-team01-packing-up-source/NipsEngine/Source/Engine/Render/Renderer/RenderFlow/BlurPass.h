#pragma once

#include "RenderPass.h"

struct FShadowBlurConstants
{
    uint32 BlurDirection; // 0 = Horizontal, 1 = Vertical
    // uint32 Pad0, Pad1, Pad2;
    uint32 TileBaseX; // 픽셀 단위. blur 클램프 좌상단
    uint32 TileBaseY;
    uint32 TileSize; // 픽셀 단위. tile의 한 변 크기
};

class FBlurPass : public FBaseRenderPass
{
public:
	bool Initialize() override;
    bool Release() override;

	bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    bool EnsureComputeShader(ID3D11Device* Device);
    bool EnsureConstantBuffer(ID3D11Device* Device);
    bool EnsureSpotShadowBlurResources(ID3D11Device* Device);
    bool EnsureDirectionalShadowBlurResources(ID3D11Device* Device);
    bool EnsurePointShadowBlurResources(ID3D11Device* Device);

	void DrawBlurCommand(const FRenderPassContext* Context, 
	                     uint32 TileBaseX, uint32 TileBaseY, uint32 TileSize,
                         ID3D11ShaderResourceView* ShadowBlurTempSRV,
                         ID3D11UnorderedAccessView* ShadowBlurTempUAV,
                         ID3D11ShaderResourceView* ShadowBlurFinalSRV,
                         ID3D11UnorderedAccessView* ShadowBlurFinalUAV);

    void UpdateConstantBuffer(ID3D11DeviceContext* DeviceContext, uint32 BlurDirection,
                                uint32 TileBaseX, uint32 TileBaseY, uint32 TileSize);

private:
    uint32 DirectionalShadowResolution = 4096;

    uint32 MaxSpotShadowCount = 8;
    uint32 SpotShadowResolution = 4096;
    uint32 PointShadowResolution = 4096;

	TComPtr<ID3D11ComputeShader> ComputeShader;
    TComPtr<ID3D11Buffer> ConstantBuffer;

	TComPtr<ID3D11ShaderResourceView> ShadowVSMInputSRV;

	// ------------- Spot ---------------------
	// Horizontal Blur 중간 결과
	TComPtr<ID3D11Texture2D> ShadowBlurTempTexture;
    TComPtr<ID3D11ShaderResourceView> ShadowBlurTempSRV;
    TComPtr<ID3D11UnorderedAccessView> ShadowBlurTempUAV;

    // Vertical Blur 최종 결과
    TComPtr<ID3D11Texture2D> ShadowBlurFinalTexture;
    TComPtr<ID3D11ShaderResourceView> ShadowBlurFinalSRV;
    TComPtr<ID3D11UnorderedAccessView> ShadowBlurFinalUAV;

	// ------------- Directional --------------

	// Horizontal Blur 중간 결과
    TComPtr<ID3D11Texture2D> DirectionalShadowBlurTempTexture;
    TComPtr<ID3D11ShaderResourceView> DirectionalShadowBlurTempSRV;
    TComPtr<ID3D11UnorderedAccessView> DirectionalShadowBlurTempUAV;

    // Vertical Blur 최종 결과
    TComPtr<ID3D11Texture2D> DirectionalShadowBlurFinalTexture;
    TComPtr<ID3D11ShaderResourceView> DirectionalShadowBlurFinalSRV;
    TComPtr<ID3D11UnorderedAccessView> DirectionalShadowBlurFinalUAV;

    // ------------- Point --------------------
    TComPtr<ID3D11Texture2D> PointShadowBlurTempTexture;
    TComPtr<ID3D11ShaderResourceView> PointShadowBlurTempSRV;
    TComPtr<ID3D11UnorderedAccessView> PointShadowBlurTempUAV;

    TComPtr<ID3D11Texture2D> PointShadowBlurFinalTexture;
    TComPtr<ID3D11ShaderResourceView> PointShadowBlurFinalSRV;
    TComPtr<ID3D11UnorderedAccessView> PointShadowBlurFinalUAV;
};
