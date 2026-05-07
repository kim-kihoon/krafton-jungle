#pragma once
#include "RenderPass.h"
#include "Render/Common/ComPtr.h"

struct FLightCullingOutputs
{
    ID3D11ShaderResourceView* LightBufferSRV = nullptr;
    ID3D11ShaderResourceView* TileLightCountSRV = nullptr;
    ID3D11ShaderResourceView* TileLightIndexSRV = nullptr;
    uint32 TileCountX = 0;
    uint32 TileCountY = 0;
    uint32 TileSize = 0;
    uint32 MaxLightsPerTile = 0;
    uint32 LightCount = 0;
};

struct FLightCullingDebugStats
{
    uint32 LightCount        = 0;
    uint32 TileCountX        = 0;
    uint32 TileCountY        = 0;
    uint32 TileCount         = 0;
    uint32 NonZeroTileCount  = 0;
    uint32 MaxLightsInTile   = 0;
    float  AvgLightsPerTile  = 0.0f;
};

class FLightCullingPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;
    static const FLightCullingOutputs& GetOutputs();
    static const FLightCullingDebugStats& GetDebugStats();

private:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

    bool EnsureComputeShader(ID3D11Device* Device);
    bool EnsureInputLightBuffer(ID3D11Device* Device, uint32 RequiredLightCount);
    bool EnsureTileBuffers(ID3D11Device* Device, uint32 RequiredTileCount);
    bool EnsureConstantBuffer(ID3D11Device* Device);
    void EmitDebugStats(const FRenderPassContext* Context, uint32 TileCountX, uint32 TileCountY);

private:
    TComPtr<ID3D11ComputeShader> ComputeShader;
    TComPtr<ID3D11Buffer> LightBuffer;
    TComPtr<ID3D11ShaderResourceView> LightBufferSRV;
    TComPtr<ID3D11Buffer> TileLightCountBuffer;
    TComPtr<ID3D11Buffer> TileLightCountReadbackBuffer;
    TComPtr<ID3D11UnorderedAccessView> TileLightCountUAV;
    TComPtr<ID3D11ShaderResourceView> TileLightCountSRV;
    TComPtr<ID3D11Buffer> TileLightIndexBuffer;
    TComPtr<ID3D11UnorderedAccessView> TileLightIndexUAV;
    TComPtr<ID3D11ShaderResourceView> TileLightIndexSRV;
    TComPtr<ID3D11Buffer> CullingConstantBuffer;

    uint32 LightBufferCapacity = 0;
    uint32 TileBufferCapacity = 0;

    const uint32 MaxLocalLightNum = 512;
};
