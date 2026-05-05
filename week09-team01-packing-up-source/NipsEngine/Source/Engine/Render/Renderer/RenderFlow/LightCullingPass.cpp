#include "LightCullingPass.h"
#include "Core/Paths.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Scene/RenderCommand.h"
#include <cstring>
#include "Core/Logger.h"
#include <cmath>
#include <algorithm>

namespace
{
    constexpr uint32 LightCullingTileSize = 16;
    constexpr uint32 MaxLightsPerTile = 512;
    constexpr uint32 ThreadGroupSizeX = 8;
    constexpr uint32 ThreadGroupSizeY = 8;
    constexpr uint32 DebugLogIntervalFrames = 30;

    struct FLightCullingConstants
    {
        FMatrix View;
        FMatrix Projection;
        uint32 LightCount = 0;
        uint32 TileCountX = 0;
        uint32 TileCountY = 0;
        uint32 TileSize = 0;
        float ViewportWidth = 0.0f;
        float ViewportHeight = 0.0f;
        uint32 IsOrthographic = 0;
        float Padding = 0.0f;
    };

	struct FLightCullingLight
    {
        FVector WorldPos = FVector::ZeroVector;
        float Radius = 0.0f;
        FVector Color = FVector::ZeroVector;
        float Intensity = 0.0f;
        float RadiusFalloff = 1.0f;
        uint32 Type = 0; // 0=Point, 1=Spot
        float SpotInnerCos = 1.0f;
        float SpotOuterCos = 0.0f;
        FVector Direction = FVector::ZeroVector;
        uint32 bCastShadows = 0u;

        int ShadowMapIndex = -1;
        float ShadowBias = 0.f;
        float Padding0 = 0.f;
        float Padding1 = 0.f;
    };

    static_assert(sizeof(FLightCullingLight) == 80, "FLightCullingLight layout must match FVisibleLightData in HLSL.");

    uint32 CeilDivide(uint32 Numerator, uint32 Denominator)
    {
        return (Numerator + Denominator - 1u) / Denominator;
    }

    void UnbindVisibleLightSRVs(ID3D11DeviceContext* DeviceContext)
    {
        if (DeviceContext == nullptr)
        {
            return;
        }

        ID3D11ShaderResourceView* NullSRVs[3] = { nullptr, nullptr, nullptr };
        DeviceContext->VSSetShaderResources(8, 3, NullSRVs);
        DeviceContext->PSSetShaderResources(8, 3, NullSRVs);
    }

    FLightCullingOutputs GLightCullingOutputs = {};
    FLightCullingDebugStats GDebugStats = {};
}

bool FLightCullingPass::Initialize()
{
    return true;
}

bool FLightCullingPass::Release()
{
    ComputeShader.Reset();
    LightBuffer.Reset();
    LightBufferSRV.Reset();
    TileLightCountBuffer.Reset();
    TileLightCountReadbackBuffer.Reset();
    TileLightCountUAV.Reset();
    TileLightCountSRV.Reset();
    TileLightIndexBuffer.Reset();
    TileLightIndexUAV.Reset();
    TileLightIndexSRV.Reset();
    CullingConstantBuffer.Reset();
    LightBufferCapacity = 0;
    TileBufferCapacity = 0;
    GLightCullingOutputs = {};
    return true;
}

const FLightCullingOutputs& FLightCullingPass::GetOutputs()
{
    return GLightCullingOutputs;
}

const FLightCullingDebugStats& FLightCullingPass::GetDebugStats()
{
    return GDebugStats;
}

bool FLightCullingPass::Begin(const FRenderPassContext* Context)
{
    if (!Context || !Context->Device || !Context->DeviceContext || !Context->RenderBus || !Context->RenderTargets)
    {
        return false;
    }

    OutSRV = PrevPassSRV;
    OutRTV = PrevPassRTV;
    return true;
}

bool FLightCullingPass::DrawCommand(const FRenderPassContext* Context)
{
    GLightCullingOutputs = {};

    if (!EnsureComputeShader(Context->Device) || !EnsureConstantBuffer(Context->Device))
    {
        return false;
    }

    const float Width = Context->RenderTargets->Width;
    const float Height = Context->RenderTargets->Height;
    if (Width <= 0.0f || Height <= 0.0f)
    {
        return true;
    }

    const uint32 TileCountX = CeilDivide(static_cast<uint32>(Width), LightCullingTileSize);
    const uint32 TileCountY = CeilDivide(static_cast<uint32>(Height), LightCullingTileSize);
    const uint32 TileCount = TileCountX * TileCountY;
    if (TileCount == 0)
    {
        return true;
    }

    if (!EnsureTileBuffers(Context->Device, TileCount))
    {
        return false;
    }
	
	TArray<FLightCullingLight> CullingLights;
    const TArray<FRenderLight>& SceneLights = Context->RenderBus->GetLights();
    const FVector& CameraPos = Context->RenderBus->GetCameraPosition();

    // 거리 포함한 임시 구조체로 max-heap 구성 (가장 먼 것이 top)
    using FLightWithDist = TPair<float, FLightCullingLight>;
    TArray<FLightWithDist> Heap;
    Heap.reserve(MaxLocalLightNum + 1);

    auto HeapCmp = [](const FLightWithDist& A, const FLightWithDist& B)
    {
        return A.first < B.first; // max-heap: 거리 큰 게 top
    };

    for (const FRenderLight& Light : SceneLights)
    {
        if (Light.Type != (uint32)ELightType::LightType_Point &&
            Light.Type != (uint32)ELightType::LightType_Spot)
            continue;

        FLightCullingLight CullingLight = {};
        CullingLight.WorldPos = Light.Position;
        CullingLight.Radius = Light.Radius;
        CullingLight.Color = Light.Color;
        CullingLight.Intensity = Light.Intensity;
        CullingLight.RadiusFalloff = Light.FalloffExponent;
        CullingLight.Type = Light.Type;
        CullingLight.SpotInnerCos = Light.SpotInnerCos;
        CullingLight.SpotOuterCos = Light.SpotOuterCos;
        CullingLight.Direction = Light.Direction;
        CullingLight.bCastShadows = Light.bCastShadows;
        CullingLight.ShadowMapIndex = Light.ShadowMapIndex;
        CullingLight.ShadowBias = Light.ShadowBias;

        float Dist = FVector::DistSquared(CameraPos, Light.Position);

        Heap.push_back({ Dist, CullingLight });
        std::push_heap(Heap.begin(), Heap.end(), HeapCmp);

        // MaxLocalLightNum 초과 시 가장 먼 것(top) 제거
        if (Heap.size() > MaxLocalLightNum)
        {
            std::pop_heap(Heap.begin(), Heap.end(), HeapCmp);
            Heap.pop_back();
        }
    }

    CullingLights.reserve(MaxLocalLightNum + 1);
	for (FLightWithDist& Entry : Heap)
	{
        CullingLights.push_back(std::move(Entry.second));
	}

    const uint32 LightCount = static_cast<uint32>(CullingLights.size());

    if (LightCount > 0)
    {
        if (!EnsureInputLightBuffer(Context->Device, LightCount))
        {
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE MappedLightBuffer = {};
        if (FAILED(Context->DeviceContext->Map(LightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedLightBuffer)))
        {
            return false;
        }
        std::memcpy(MappedLightBuffer.pData, CullingLights.data(), sizeof(FLightCullingLight) * LightCount);
        Context->DeviceContext->Unmap(LightBuffer.Get(), 0);
    }

    FLightCullingConstants Constants = {};
    Constants.View = Context->RenderBus->GetView();
    Constants.Projection = Context->RenderBus->GetProj();
    Constants.LightCount = LightCount;
    Constants.TileCountX = TileCountX;
    Constants.TileCountY = TileCountY;
    Constants.TileSize = LightCullingTileSize;
    Constants.ViewportWidth = Width;
    Constants.ViewportHeight = Height;
    Constants.IsOrthographic = Context->RenderBus->IsOrthographic() ? 1 : 0;

    D3D11_MAPPED_SUBRESOURCE MappedCB = {};
    if (FAILED(Context->DeviceContext->Map(CullingConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedCB)))
    {
        return false;
    }
    std::memcpy(MappedCB.pData, &Constants, sizeof(Constants));
    Context->DeviceContext->Unmap(CullingConstantBuffer.Get(), 0);

    ID3D11ComputeShader* CS = ComputeShader.Get();
    Context->DeviceContext->CSSetShader(CS, nullptr, 0);

    ID3D11Buffer* CBuffers[] = { CullingConstantBuffer.Get() };
    Context->DeviceContext->CSSetConstantBuffers(0, 1, CBuffers);

    ID3D11ShaderResourceView* SRVs[] = { LightCount > 0 ? LightBufferSRV.Get() : nullptr };
    Context->DeviceContext->CSSetShaderResources(0, 1, SRVs);

    UINT ClearValues[4] = { 0u, 0u, 0u, 0u };
    Context->DeviceContext->ClearUnorderedAccessViewUint(TileLightCountUAV.Get(), ClearValues);
    Context->DeviceContext->ClearUnorderedAccessViewUint(TileLightIndexUAV.Get(), ClearValues);

    UnbindVisibleLightSRVs(Context->DeviceContext);

    ID3D11UnorderedAccessView* UAVs[] = { TileLightCountUAV.Get(), TileLightIndexUAV.Get() };
    Context->DeviceContext->CSSetUnorderedAccessViews(0, 2, UAVs, nullptr);

    Context->DeviceContext->Dispatch(TileCountX, TileCountY, 1);

    GLightCullingOutputs.LightBufferSRV = (LightCount > 0) ? LightBufferSRV.Get() : nullptr;
    GLightCullingOutputs.TileLightCountSRV = TileLightCountSRV.Get();
    GLightCullingOutputs.TileLightIndexSRV = TileLightIndexSRV.Get();
    GLightCullingOutputs.TileCountX = TileCountX;
    GLightCullingOutputs.TileCountY = TileCountY;
    GLightCullingOutputs.TileSize = LightCullingTileSize;
    GLightCullingOutputs.MaxLightsPerTile = MaxLightsPerTile;
    GLightCullingOutputs.LightCount = LightCount;

    // TODO: 디버그용
     EmitDebugStats(Context, TileCountX, TileCountY);

    return true;
}

bool FLightCullingPass::End(const FRenderPassContext* Context)
{
    ID3D11ShaderResourceView* NullSRVs[] = { nullptr };
    Context->DeviceContext->CSSetShaderResources(0, 1, NullSRVs);

    ID3D11UnorderedAccessView* NullUAVs[] = { nullptr, nullptr };
    Context->DeviceContext->CSSetUnorderedAccessViews(0, 2, NullUAVs, nullptr);

    ID3D11Buffer* NullCBs[] = { nullptr };
    Context->DeviceContext->CSSetConstantBuffers(0, 1, NullCBs);
    Context->DeviceContext->CSSetShader(nullptr, nullptr, 0);
    return true;
}

bool FLightCullingPass::EnsureComputeShader(ID3D11Device* Device)
{
    if (ComputeShader)
    {
        return true;
    }

    TComPtr<ID3DBlob> CSBlob;
    TComPtr<ID3DBlob> ErrorBlob;
    const HRESULT CompileResult = D3DCompileFromFile(
        FPaths::ToWide("Shaders/Multipass/LightCullingCS.hlsl").c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "mainCS",
        "cs_5_0",
        0,
        0,
        CSBlob.GetAddressOf(),
        ErrorBlob.GetAddressOf());

    if (FAILED(CompileResult))
    {
        if (ErrorBlob)
        {
            UE_LOG("LightCulling CS Compile Error: %s", static_cast<const char*>(ErrorBlob->GetBufferPointer()));
        }
        else
        {
            UE_LOG("Failed to compile LightCullingCS.hlsl");
        }
        return false;
    }

    const HRESULT CreateResult =
        Device->CreateComputeShader(CSBlob->GetBufferPointer(), CSBlob->GetBufferSize(), nullptr, ComputeShader.GetAddressOf());
    if (FAILED(CreateResult))
    {
        UE_LOG("Failed to create LightCulling compute shader");
        return false;
    }

    return true;
}

bool FLightCullingPass::EnsureInputLightBuffer(ID3D11Device* Device, uint32 RequiredLightCount)
{
    if (RequiredLightCount <= LightBufferCapacity && LightBuffer && LightBufferSRV)
    {
        return true;
    }

    const uint32 NewCapacity = RequiredLightCount;

    D3D11_BUFFER_DESC BufferDesc = {};
    BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    BufferDesc.ByteWidth = static_cast<uint32>(sizeof(FLightCullingLight) * NewCapacity);
    BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    BufferDesc.StructureByteStride = sizeof(FLightCullingLight);

    TComPtr<ID3D11Buffer> NewBuffer;
    if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, NewBuffer.GetAddressOf())))
    {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    SRVDesc.Buffer.FirstElement = 0;
    SRVDesc.Buffer.NumElements = NewCapacity;

    TComPtr<ID3D11ShaderResourceView> NewSRV;
    if (FAILED(Device->CreateShaderResourceView(NewBuffer.Get(), &SRVDesc, NewSRV.GetAddressOf())))
    {
        return false;
    }

    LightBuffer = std::move(NewBuffer);
    LightBufferSRV = std::move(NewSRV);
    LightBufferCapacity = NewCapacity;
    return true;
}

bool FLightCullingPass::EnsureTileBuffers(ID3D11Device* Device, uint32 RequiredTileCount)
{
    if (RequiredTileCount <= TileBufferCapacity && TileLightCountBuffer && TileLightCountUAV && TileLightIndexBuffer && TileLightIndexUAV)
    {
        return true;
    }

    const uint32 NewTileCount = RequiredTileCount;
    const uint32 TileLightIndexCount = NewTileCount * MaxLightsPerTile;

    D3D11_BUFFER_DESC CountBufferDesc = {};
    CountBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    CountBufferDesc.ByteWidth = static_cast<uint32>(sizeof(uint32) * NewTileCount);
    CountBufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    CountBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    CountBufferDesc.StructureByteStride = sizeof(uint32);

    TComPtr<ID3D11Buffer> NewCountBuffer;
    if (FAILED(Device->CreateBuffer(&CountBufferDesc, nullptr, NewCountBuffer.GetAddressOf())))
    {
        return false;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC CountUAVDesc = {};
    CountUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
    CountUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    CountUAVDesc.Buffer.FirstElement = 0;
    CountUAVDesc.Buffer.NumElements = NewTileCount;

    TComPtr<ID3D11UnorderedAccessView> NewCountUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewCountBuffer.Get(), &CountUAVDesc, NewCountUAV.GetAddressOf())))
    {
        return false;
    }

    D3D11_BUFFER_DESC CountReadbackDesc = {};
    CountReadbackDesc.Usage = D3D11_USAGE_STAGING;
    CountReadbackDesc.ByteWidth = static_cast<uint32>(sizeof(uint32) * NewTileCount);
    CountReadbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CountReadbackDesc.StructureByteStride = sizeof(uint32);

    TComPtr<ID3D11Buffer> NewCountReadbackBuffer;
    if (FAILED(Device->CreateBuffer(&CountReadbackDesc, nullptr, NewCountReadbackBuffer.GetAddressOf())))
    {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC CountSRVDesc = {};
    CountSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    CountSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    CountSRVDesc.Buffer.FirstElement = 0;
    CountSRVDesc.Buffer.NumElements = NewTileCount;

    TComPtr<ID3D11ShaderResourceView> NewCountSRV;
    if (FAILED(Device->CreateShaderResourceView(NewCountBuffer.Get(), &CountSRVDesc, NewCountSRV.GetAddressOf())))
    {
        return false;
    }

    D3D11_BUFFER_DESC IndexBufferDesc = {};
    IndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    IndexBufferDesc.ByteWidth = static_cast<uint32>(sizeof(uint32) * TileLightIndexCount);
    IndexBufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    IndexBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    IndexBufferDesc.StructureByteStride = sizeof(uint32);

    TComPtr<ID3D11Buffer> NewIndexBuffer;
    if (FAILED(Device->CreateBuffer(&IndexBufferDesc, nullptr, NewIndexBuffer.GetAddressOf())))
    {
        return false;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC IndexUAVDesc = {};
    IndexUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
    IndexUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    IndexUAVDesc.Buffer.FirstElement = 0;
    IndexUAVDesc.Buffer.NumElements = TileLightIndexCount;

    TComPtr<ID3D11UnorderedAccessView> NewIndexUAV;
    if (FAILED(Device->CreateUnorderedAccessView(NewIndexBuffer.Get(), &IndexUAVDesc, NewIndexUAV.GetAddressOf())))
    {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC IndexSRVDesc = {};
    IndexSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    IndexSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    IndexSRVDesc.Buffer.FirstElement = 0;
    IndexSRVDesc.Buffer.NumElements = TileLightIndexCount;

    TComPtr<ID3D11ShaderResourceView> NewIndexSRV;
    if (FAILED(Device->CreateShaderResourceView(NewIndexBuffer.Get(), &IndexSRVDesc, NewIndexSRV.GetAddressOf())))
    {
        return false;
    }

    TileLightCountBuffer = std::move(NewCountBuffer);
    TileLightCountReadbackBuffer = std::move(NewCountReadbackBuffer);
    TileLightCountUAV = std::move(NewCountUAV);
    TileLightCountSRV = std::move(NewCountSRV);
    TileLightIndexBuffer = std::move(NewIndexBuffer);
    TileLightIndexUAV = std::move(NewIndexUAV);
    TileLightIndexSRV = std::move(NewIndexSRV);
    TileBufferCapacity = NewTileCount;
    return true;
}

bool FLightCullingPass::EnsureConstantBuffer(ID3D11Device* Device)
{
    if (CullingConstantBuffer)
    {
        return true;
    }

    D3D11_BUFFER_DESC CBDesc = {};
    CBDesc.ByteWidth = sizeof(FLightCullingConstants);
    CBDesc.Usage = D3D11_USAGE_DYNAMIC;
    CBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    CBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    return SUCCEEDED(Device->CreateBuffer(&CBDesc, nullptr, CullingConstantBuffer.GetAddressOf()));
}

void FLightCullingPass::EmitDebugStats(const FRenderPassContext* Context, uint32 TileCountX, uint32 TileCountY)
{
    static uint64 FrameCounter = 0;
    ++FrameCounter;

    if ((FrameCounter % DebugLogIntervalFrames) != 0)
    {
        return;
    }

    if (!Context || !Context->DeviceContext || !TileLightCountBuffer || !TileLightCountReadbackBuffer)
    {
        return;
    }

    const uint32 TileCount = TileCountX * TileCountY;
    if (TileCount == 0)
    {
        return;
    }

    Context->DeviceContext->CopyResource(TileLightCountReadbackBuffer.Get(), TileLightCountBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(Context->DeviceContext->Map(TileLightCountReadbackBuffer.Get(), 0, D3D11_MAP_READ, 0, &Mapped)))
    {
        return;
    }

    const uint32* Counts = static_cast<const uint32*>(Mapped.pData);
    uint64 TotalVisibleLights = 0;
    uint32 MaxVisibleLightsInTile = 0;
    uint32 NonZeroTileCount = 0;

    for (uint32 TileIndex = 0; TileIndex < TileCount; ++TileIndex)
    {
        const uint32 Count = Counts[TileIndex];
        TotalVisibleLights += Count;
        if (Count > MaxVisibleLightsInTile) MaxVisibleLightsInTile = Count;
        if (Count > 0) ++NonZeroTileCount;
    }

    Context->DeviceContext->Unmap(TileLightCountReadbackBuffer.Get(), 0);

    GDebugStats.LightCount       = GLightCullingOutputs.LightCount;
    GDebugStats.TileCountX       = TileCountX;
    GDebugStats.TileCountY       = TileCountY;
    GDebugStats.TileCount        = TileCount;
    GDebugStats.NonZeroTileCount = NonZeroTileCount;
    GDebugStats.MaxLightsInTile  = MaxVisibleLightsInTile;
    GDebugStats.AvgLightsPerTile = (TileCount > 0)
        ? static_cast<float>(TotalVisibleLights) / static_cast<float>(TileCount)
        : 0.0f;
}
