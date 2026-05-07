#include "ShadowAtlasManager.h"

#include <algorithm>
#include <cmath>

TArray<uint8> FShadowAtlasManager::SpotCellOccupancy;
TArray<FSpotAtlasSlotDesc> FShadowAtlasManager::ActiveSpotSlots;
TArray<FDirectionalAtlasSlotDesc> FShadowAtlasManager::DirectionalCascadeSlots;
TArray<uint8> FShadowAtlasManager::PointAtlasCellOccupancy;
TArray<FPointAtlasSlotDesc> FShadowAtlasManager::ActivePointSlots;

bool FShadowAtlasManager::Initialize(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }
    
    // 이미 만들어진 atlas가 있으면 프레임 간 재사용합니다.
    if (SpotAtlasTexture && SpotAtlasDSV && SpotAtlasSRV)
    {
        return true;
    }

    // Depth를 DSV/SRV 둘 다에서 쓰기 위해 typeless texture를 만듭니다.
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = SpotAtlasResolution;
    TextureDesc.Height = SpotAtlasResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    
    if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, SpotAtlasTexture.GetAddressOf())))
    {
        return false;
    }
    
    D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
    DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
    DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    DSVDesc.Flags = 0;
    DSVDesc.Texture2D.MipSlice = 0;
    if (FAILED(Device->CreateDepthStencilView(SpotAtlasTexture.Get(), &DSVDesc, SpotAtlasDSV.GetAddressOf())))
    {
        Release();        
        return false;
    }
    
    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MostDetailedMip = 0;
    SRVDesc.Texture2D.MipLevels = 1;
    if (FAILED(Device->CreateShaderResourceView(SpotAtlasTexture.Get(), &SRVDesc, SpotAtlasSRV.GetAddressOf())))
    {
        Release();        
        return false;
    }

	// VSM
    D3D11_TEXTURE2D_DESC VSMTextureDesc = {};
    VSMTextureDesc.Width = SpotAtlasResolution;
    VSMTextureDesc.Height = SpotAtlasResolution;
    VSMTextureDesc.MipLevels = 1;
    VSMTextureDesc.ArraySize = 1;
    VSMTextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT; // R=depth G=depth²
    VSMTextureDesc.SampleDesc.Count = 1;
    VSMTextureDesc.SampleDesc.Quality = 0;
    VSMTextureDesc.Usage = D3D11_USAGE_DEFAULT;
    VSMTextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    VSMTextureDesc.CPUAccessFlags = 0;
    VSMTextureDesc.MiscFlags = 0;

    if (FAILED(Device->CreateTexture2D(&VSMTextureDesc, nullptr, SpotVSMAtlasTexture.GetAddressOf())))
    {
        Release();
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
    RTVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    RTVDesc.Texture2D.MipSlice = 0;

    if (FAILED(Device->CreateRenderTargetView(SpotVSMAtlasTexture.Get(), &RTVDesc, SpotVSMAtlasRTV.GetAddressOf())))
    {
        Release();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC VSMSRVDesc = {};
    VSMSRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    VSMSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    VSMSRVDesc.Texture2D.MostDetailedMip = 0;
    VSMSRVDesc.Texture2D.MipLevels = 1;

    if (FAILED(Device->CreateShaderResourceView(SpotVSMAtlasTexture.Get(), &VSMSRVDesc, SpotVSMAtlasSRV.GetAddressOf())))
    {
        Release();
        return false;
    }
    
    return true;
}

bool FShadowAtlasManager::InitializeDirectionalAtlas(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }
    
    if (DirectionalAtlasTexture && DirectionalAtlasDSV && DirectionalAtlasSRV)
    {
        return true;
    }
    
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = DirectionalAtlasResolution;
    TextureDesc.Height = DirectionalAtlasResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    
    if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, DirectionalAtlasTexture.GetAddressOf())))
    {
        return false;
    }
    
    D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
    DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
    DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    DSVDesc.Flags = 0;
    DSVDesc.Texture2D.MipSlice = 0;
    if (FAILED(Device->CreateDepthStencilView(DirectionalAtlasTexture.Get(), &DSVDesc, DirectionalAtlasDSV.GetAddressOf())))
    {
        DirectionalAtlasTexture.Reset();
        return false;
    }
    
    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MostDetailedMip = 0;
    SRVDesc.Texture2D.MipLevels = 1;
    if (FAILED(Device->CreateShaderResourceView(DirectionalAtlasTexture.Get(), &SRVDesc, DirectionalAtlasSRV.GetAddressOf())))
    {
        DirectionalAtlasDSV.Reset();
        DirectionalAtlasTexture.Reset();      
        return false;
    }

	// VSM
    D3D11_TEXTURE2D_DESC VSMTextureDesc = {};
    VSMTextureDesc.Width = DirectionalAtlasResolution;
    VSMTextureDesc.Height = DirectionalAtlasResolution;
    VSMTextureDesc.MipLevels = 1;
    VSMTextureDesc.ArraySize = 1;
    VSMTextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT; // R=depth G=depth²
    VSMTextureDesc.SampleDesc.Count = 1;
    VSMTextureDesc.SampleDesc.Quality = 0;
    VSMTextureDesc.Usage = D3D11_USAGE_DEFAULT;
    VSMTextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    VSMTextureDesc.CPUAccessFlags = 0;
    VSMTextureDesc.MiscFlags = 0;

    if (FAILED(Device->CreateTexture2D(&VSMTextureDesc, nullptr, DirectionalVSMAtlasTexture.GetAddressOf())))
    {
        Release();
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
    RTVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    RTVDesc.Texture2D.MipSlice = 0;

    if (FAILED(Device->CreateRenderTargetView(DirectionalVSMAtlasTexture.Get(), &RTVDesc, DirectionalVSMAtlasRTV.GetAddressOf())))
    {
        Release();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC VSMSRVDesc = {};
    VSMSRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    VSMSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    VSMSRVDesc.Texture2D.MostDetailedMip = 0;
    VSMSRVDesc.Texture2D.MipLevels = 1;

    if (FAILED(Device->CreateShaderResourceView(DirectionalVSMAtlasTexture.Get(), &VSMSRVDesc, DirectionalVSMAtlasSRV.GetAddressOf())))
    {
        Release();
        return false;
    }
    
    return true;
}

void FShadowAtlasManager::Release()
{
    SpotAtlasSRV.Reset();
    SpotAtlasDSV.Reset();
    SpotAtlasTexture.Reset();

	SpotVSMAtlasSRV.Reset();
    SpotVSMAtlasRTV.Reset();
    SpotVSMAtlasTexture.Reset();

    DirectionalAtlasTexture.Reset();
    DirectionalAtlasDSV.Reset();        
    DirectionalAtlasSRV.Reset();

    DirectionalVSMAtlasSRV.Reset();
    DirectionalVSMAtlasRTV.Reset();
    DirectionalVSMAtlasTexture.Reset();
    
    PointAtlasSRV.Reset();
    PointAtlasDSV.Reset();
    PointAtlasTexture.Reset();

    PointVSMAtlasSRV.Reset();
    PointVSMAtlasRTV.Reset();
    PointVSMAtlasTexture.Reset();
}

void FShadowAtlasManager::BeginSpotFrame()
{
    const uint32 CellCount = SpotAtlasCellsPerRow * SpotAtlasCellsPerRow;
    
    if (SpotCellOccupancy.size() != CellCount)
    {
        SpotCellOccupancy.resize(CellCount, 0u);
    }
    
    std::fill(SpotCellOccupancy.begin(), SpotCellOccupancy.end(), 0u);
    ActiveSpotSlots.clear();
}

uint32 FShadowAtlasManager::SnapSpotTileSize(float RequestedResolution)
{
    float Clamped = std::clamp(
        RequestedResolution,
        static_cast<float>(MinSpotTileResolution),
        static_cast<float>(MaxSpotTileResolution));
    
    uint32 Lower = MinSpotTileResolution;
    while ((Lower << 1u) <= MaxSpotTileResolution && static_cast<float>(Lower << 1u) <= Clamped)
    {
        Lower <<= 1u;
    }
    
    uint32 Upper = Lower;
    if (Upper < MaxSpotTileResolution)
    {
        Upper <<= 1u;
    }

    const float LowerDelta = std::fabs(Clamped - static_cast<float>(Lower));
    const float UpperDelta = std::fabs(static_cast<float>(Upper) - Clamped);

    // tie면 품질을 위해 큰 쪽을 선택합니다.
    return (UpperDelta <= LowerDelta) ? Upper : Lower;
}

bool FShadowAtlasManager::RequestSpotSlot(uint32 DesiredResolution, FSpotAtlasSlotDesc& OutSlot)
{
    const uint32 TileResolution = SanitizeSpotTileSize(DesiredResolution);
    return TryAllocateSpotSlot(TileResolution, OutSlot);
}

const TArray<FSpotAtlasSlotDesc>& FShadowAtlasManager::GetActiveSpotSlots()
{
    return ActiveSpotSlots;
}

void FShadowAtlasManager::UpdateSpotSlotDebugLightId(uint32 TileIndex, int32 DebugLightId)
{
    if (TileIndex >= ActiveSpotSlots.size())
    {
        return;
    }

    ActiveSpotSlots[TileIndex].DebugLightId = DebugLightId;
}

uint32 FShadowAtlasManager::SanitizeSpotTileSize(uint32 DesiredResolution)
{
    if (DesiredResolution < MinSpotTileResolution || DesiredResolution > MaxSpotTileResolution)
    {
        return SnapSpotTileSize(static_cast<float>(DesiredResolution));
    }

    // 허용 크기 집합이 아닌 값이 들어오면 가장 가까운 허용 PoT로 보정
    switch (DesiredResolution)
    {
    case 256:
    case 512:
    case 1024:
    case 2048:
        return DesiredResolution;
    default:
        return SnapSpotTileSize(static_cast<float>(DesiredResolution));
    }
}

bool FShadowAtlasManager::TryAllocateSpotSlot(uint32 TileResolution, FSpotAtlasSlotDesc& OutSlot)
{
    const uint32 CellSpan = TileResolution / SpotCellResolution;
    if (CellSpan == 0 || CellSpan > SpotAtlasCellsPerRow)
    {
        return false;
    }
    
    // 단순: first-fit allocator
    for (uint32 CellY=0; CellY + CellSpan <= SpotAtlasCellsPerRow; ++CellY)
    {
        for (uint32 CellX = 0; CellX + CellSpan <= SpotAtlasCellsPerRow; ++CellX)
        {
            // 해당 영역 사용 가능한지 파악
            if (!IsSpotRegionFree(CellX, CellY, CellSpan))
            {
                continue;
            }
            // 해당 영역 표시
            MarkSpotRegion(CellX, CellY, CellSpan, true);
            
            const uint32 TileIndex = static_cast<uint32>(ActiveSpotSlots.size());
            BuildSpotSlotDesc(CellX, CellY, TileResolution, TileIndex, OutSlot);
            ActiveSpotSlots.push_back(OutSlot);
            return true;
        }
    }
    return false;
}

bool FShadowAtlasManager::IsSpotRegionFree(uint32 CellX, uint32 CellY, uint32 CellSpan)
{
    for (uint32 Y = CellY; Y < CellY + CellSpan; ++Y)
    {
        for (uint32 X = CellX; X < CellX + CellSpan; ++X)
        {
            const uint32 Index = Y * SpotAtlasCellsPerRow + X;
            if (Index >= SpotCellOccupancy.size() || SpotCellOccupancy[Index] != 0u)
            {
                return false;
            }
        }
    }

    return true;
}

void FShadowAtlasManager::MarkSpotRegion(uint32 CellX, uint32 CellY, uint32 CellSpan, bool bOccupied)
{
    const uint8 OccupiedValue = bOccupied ? 1u : 0u;

    for (uint32 Y = CellY; Y < CellY + CellSpan; ++Y)
    {
        for (uint32 X = CellX; X < CellX + CellSpan; ++X)
        {
            const uint32 Index = Y * SpotAtlasCellsPerRow + X;
            if (Index < SpotCellOccupancy.size())
            {
                SpotCellOccupancy[Index] = OccupiedValue;
            }
        }
    }
}

void FShadowAtlasManager::BuildSpotSlotDesc(uint32 CellX, uint32 CellY, uint32 TileResolution, uint32 TileIndex, FSpotAtlasSlotDesc& OutSlot)
{
    OutSlot.TileIndex = TileIndex;
    OutSlot.DebugLightId = -1;
    OutSlot.X = CellX * SpotCellResolution;
    OutSlot.Y = CellY * SpotCellResolution;
    OutSlot.Width = TileResolution;
    OutSlot.Height = TileResolution;
    OutSlot.AtlasRect = FVector4(
        static_cast<float>(OutSlot.X) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Y) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Width) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Height) / static_cast<float>(SpotAtlasResolution));
}

const TArray<FDirectionalAtlasSlotDesc>& FShadowAtlasManager::GetDirectionalCascadeSlots()
{
    if (!DirectionalCascadeSlots.empty())
    {
        return DirectionalCascadeSlots;
    }
    
    DirectionalCascadeSlots.reserve(DirectionalCascadeCount);
    
    for (uint32 CascadeIndex = 0; CascadeIndex < DirectionalCascadeCount; ++CascadeIndex)
    {
        const uint32 GridX = CascadeIndex % DirectionalAtlasGridDimension;
        const uint32 GridY = CascadeIndex / DirectionalAtlasGridDimension;
        
        FDirectionalAtlasSlotDesc Slot = {};
        Slot.CascadeIndex = CascadeIndex;
        Slot.X = GridX * DirectionalCascadeResolution;
        Slot.Y = GridY * DirectionalCascadeResolution;
        Slot.Width = DirectionalCascadeResolution;
        Slot.Height = DirectionalCascadeResolution;
        Slot.AtlasRect = FVector4(
            static_cast<float>(Slot.X) / static_cast<float>(DirectionalAtlasResolution),
            static_cast<float>(Slot.Y) / static_cast<float>(DirectionalAtlasResolution),
            static_cast<float>(Slot.Width) / static_cast<float>(DirectionalAtlasResolution),
            static_cast<float>(Slot.Height) / static_cast<float>(DirectionalAtlasResolution));
        DirectionalCascadeSlots.push_back(Slot);
    }
    
    return DirectionalCascadeSlots;
}

// ----------------------------------------
// - Point Atlas --------------------------
// ----------------------------------------
void FShadowAtlasManager::BeginPointFrame()
{
    const uint32 CellCount = PointAtlasCellsPerRow * PointAtlasCellsPerRow;
    
    if (PointAtlasCellOccupancy.size() != CellCount)
    {
        PointAtlasCellOccupancy.resize(CellCount, 0u);
    }

    std::fill(PointAtlasCellOccupancy.begin(), PointAtlasCellOccupancy.end(), 0u);
    ActivePointSlots.clear();
}

uint32 FShadowAtlasManager::SnapPointTileSize(float RequestedResolution)
{
    float Clamped = std::clamp(RequestedResolution, static_cast<float>(MinPointTileResolution), static_cast<float>(MaxPointTileResolution));
    
    uint32 Lower = MinPointTileResolution; 
    while ((Lower<<1u) <= MaxPointTileResolution && static_cast<float>(Lower << 1u) <= Clamped)
    {
        Lower <<= 1u;
    }
    
    uint32 Upper = Lower;
    if (Upper < MaxPointTileResolution)
    {
        Upper <<= 1u;
    }
    
    const float LowerDelta = std::fabs(Clamped - static_cast<float>(Lower));
    const float UpperDelta = std::fabs(static_cast<float>(Upper) - Clamped);
    
    return (UpperDelta <= LowerDelta) ? Upper : Lower;
}

bool FShadowAtlasManager::RequestPointAtlasSlot(uint32 DesiredResolution, FPointAtlasSlotDesc& OutSlot)
{
    if (ActivePointSlots.size() >= MaxPointShadowCount)
    {
        return false;
    }
    
    const uint32 TileResolution = SanitizePointTileSize(DesiredResolution);
    return TryAllocatePointFaceTiles(TileResolution, OutSlot);
}

uint32 FShadowAtlasManager::SanitizePointTileSize(uint32 DesiredResolution)
{
    if (DesiredResolution < MinPointTileResolution || DesiredResolution > MaxPointTileResolution)
    {
        return SnapPointTileSize(static_cast<float>(DesiredResolution));
    }
    
    switch (DesiredResolution)
    {
    case 256:
    case 512:
    case 1024:
        return DesiredResolution;
    default:
        return SnapPointTileSize(static_cast<float>(DesiredResolution));
    }
}

bool FShadowAtlasManager::TryAllocatePointFaceTiles(uint32 TileResolution, FPointAtlasSlotDesc& OutSlot)
{
    if (TileResolution == 0 || TileResolution % PointAtlasCellResolution != 0)
    {
        return false;
    }
    
    const uint32 CellSpan = TileResolution / PointAtlasCellResolution;
    if (CellSpan == 0 || CellSpan > PointAtlasCellsPerRow)
    {
        return false;
    }
    
    uint32 FaceCellX[PointCubeFaceCount] = {};
    uint32 FaceCellY[PointCubeFaceCount] = {};
    uint32 AllocatedFaceCount  = 0;
    
    for (uint32 FaceIndex = 0; FaceIndex < PointCubeFaceCount; ++FaceIndex)
    {
        uint32 CellX = 0;
        uint32 CellY = 0;
        
        if (!TryAllocatePointFaceTile(CellSpan, CellX, CellY))
        {
            for (uint32 RollbackIndex = 0; RollbackIndex < AllocatedFaceCount; ++RollbackIndex)
            {
                MarkPointRegion(FaceCellX[RollbackIndex], FaceCellY[RollbackIndex], CellSpan, false);
            }
            return false;
        }
        MarkPointRegion(CellX, CellY, CellSpan, true);
        
        FaceCellX[FaceIndex] = CellX;
        FaceCellY[FaceIndex] = CellY;
        ++AllocatedFaceCount;
    }
    
    const uint32 CubeIndex = static_cast<uint32>(ActivePointSlots.size());
    BuildPointSlotDesc(FaceCellX, FaceCellY, TileResolution, CubeIndex, OutSlot);
    ActivePointSlots.push_back(OutSlot);
    return true;
    
}

bool FShadowAtlasManager::TryAllocatePointFaceTile(uint32 CellSpan, uint32& OutCellX, uint32& OutCellY)
{
    for (uint32 CellY = 0; CellY + CellSpan <= PointAtlasCellsPerRow; ++CellY)
    {
        for (uint32 CellX = 0; CellX + CellSpan <= PointAtlasCellsPerRow; ++CellX)
        {
            if (!IsPointRegionFree(CellX, CellY, CellSpan))
            {
                continue;
            }

            OutCellX = CellX;
            OutCellY = CellY;
            return true;
        }
    }

    return false;
}


bool FShadowAtlasManager::IsPointRegionFree(uint32 CellX, uint32 CellY, uint32 CellSpan)
{
    for (uint32 Y = CellY; Y < CellY + CellSpan; ++Y)
    {
        for (uint32 X = CellX; X < CellX + CellSpan; ++X)
        {
            const uint32 Index = Y * PointAtlasCellsPerRow + X;
            if (Index >= PointAtlasCellOccupancy.size() || PointAtlasCellOccupancy[Index] != 0u)
            {
                return false;
            }
        }
    }

    return true;
}

void FShadowAtlasManager::MarkPointRegion(uint32 CellX, uint32 CellY, uint32 CellSpan, bool bOccupied)
{
    const uint8 OccupiedValue = bOccupied ? 1u : 0u;

    for (uint32 Y = CellY; Y < CellY + CellSpan; ++Y)
    {
        for (uint32 X = CellX; X < CellX + CellSpan; ++X)
        {
            const uint32 Index = Y * PointAtlasCellsPerRow + X;
            if (Index < PointAtlasCellOccupancy.size())
            {
                PointAtlasCellOccupancy[Index] = OccupiedValue;
            }
        }
    }
}

void FShadowAtlasManager::BuildPointSlotDesc(const uint32 FaceCellX[PointCubeFaceCount], const uint32 FaceCellY[PointCubeFaceCount],
    uint32 TileResolution, uint32 CubeIndex,  FPointAtlasSlotDesc& OutSlot)
{
    OutSlot = {};
    OutSlot.CubeIndex = CubeIndex;
    OutSlot.BaseCellX = FaceCellX[0];
    OutSlot.BaseCellY = FaceCellY[0];
    OutSlot.TileResolution = TileResolution;

    const float Size = static_cast<float>(TileResolution);
    const float AtlasSize = static_cast<float>(PointAtlasResolution);

    for (uint32 FaceIndex = 0; FaceIndex < PointCubeFaceCount; ++FaceIndex)
    {
        const float X = static_cast<float>(FaceCellX[FaceIndex] * PointAtlasCellResolution);
        const float Y = static_cast<float>(FaceCellY[FaceIndex] * PointAtlasCellResolution);

        OutSlot.FaceAtlasRects[FaceIndex] = FVector4(
            X / AtlasSize,
            Y / AtlasSize,
            Size / AtlasSize,
            Size / AtlasSize);
    }
}

bool FShadowAtlasManager::InitializePointAtlas(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }

    if (PointAtlasTexture && PointAtlasDSV && PointAtlasSRV && PointVSMAtlasTexture && PointVSMAtlasRTV && PointVSMAtlasSRV)
    {
        return true;
    }
    
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = PointAtlasResolution;
    TextureDesc.Height = PointAtlasResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    
    if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, PointAtlasTexture.GetAddressOf())))
    {
        return false;
    }
    
    D3D11_DEPTH_STENCIL_VIEW_DESC DepthStencilViewDesc = {};
    DepthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    DepthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    DepthStencilViewDesc.Texture2D.MipSlice = 0;
    
    if (FAILED(Device->CreateDepthStencilView(PointAtlasTexture.Get(), &DepthStencilViewDesc, PointAtlasDSV.GetAddressOf())))
    {
        Release();
        return false;
    }
        
    D3D11_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc = {};
    ShaderResourceViewDesc.Format = DXGI_FORMAT_R32_FLOAT;
    ShaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    ShaderResourceViewDesc.Texture2D.MipLevels = 1;
    
    if (FAILED(Device->CreateShaderResourceView(PointAtlasTexture.Get(), &ShaderResourceViewDesc, PointAtlasSRV.GetAddressOf())))
    {
        Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC VSMTextureDesc = {};
    VSMTextureDesc.Width = PointAtlasResolution;
    VSMTextureDesc.Height = PointAtlasResolution;
    VSMTextureDesc.MipLevels = 1;
    VSMTextureDesc.ArraySize = 1;
    VSMTextureDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    VSMTextureDesc.SampleDesc.Count = 1;
    VSMTextureDesc.SampleDesc.Quality = 0;
    VSMTextureDesc.Usage = D3D11_USAGE_DEFAULT;
    VSMTextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    if (FAILED(Device->CreateTexture2D(&VSMTextureDesc, nullptr, PointVSMAtlasTexture.GetAddressOf())))
    {
        Release();
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC RTVDesc = {};
    RTVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    RTVDesc.Texture2D.MipSlice = 0;

    if (FAILED(Device->CreateRenderTargetView(PointVSMAtlasTexture.Get(), &RTVDesc, PointVSMAtlasRTV.GetAddressOf())))
    {
        Release();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC VSMSRVDesc = {};
    VSMSRVDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    VSMSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    VSMSRVDesc.Texture2D.MostDetailedMip = 0;
    VSMSRVDesc.Texture2D.MipLevels = 1;

    if (FAILED(Device->CreateShaderResourceView(PointVSMAtlasTexture.Get(), &VSMSRVDesc, PointVSMAtlasSRV.GetAddressOf())))
    {
        Release();
        return false;
    }

    return true;
}
