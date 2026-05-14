#pragma once

#include "Core/CoreMinimal.h"
#include "Render/Common/ComPtr.h"
#include "Render/Common/RenderTypes.h"

struct FSpotAtlasSlotDesc
{
    uint32 TileIndex = 0;
    int32 DebugLightId = -1;
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Width = 0;
    uint32 Height = 0;
    
    // xy = normalized UV offset, zw = normalized UV scale
    FVector4 AtlasRect = FVector4(0.0f, 0.0f, 1.0f, 1.0f);
};

struct FDirectionalAtlasSlotDesc
{
    uint32 CascadeIndex = 0;
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Width = 0;
    uint32 Height = 0;
    
    FVector4 AtlasRect = FVector4(0.0f, 0.0f, 1.0f, 1.0f);
};

struct FPointAtlasSlotDesc
{
    uint32 CubeIndex = 0;
    uint32 BaseCellX = 0;
    uint32 BaseCellY = 0;
    uint32 TileResolution = 0;
    FVector4 FaceAtlasRects[6] =
    {
        FVector4(0.0f, 0.0f, 1.0f, 1.0f),
        FVector4(0.0f, 0.0f, 1.0f, 1.0f),
        FVector4(0.0f, 0.0f, 1.0f, 1.0f),
        FVector4(0.0f, 0.0f, 1.0f, 1.0f),
        FVector4(0.0f, 0.0f, 1.0f, 1.0f),
        FVector4(0.0f, 0.0f, 1.0f, 1.0f)
    };
};

class FShadowAtlasManager
{
public:
    static constexpr uint32 SpotAtlasResolution = 4096;
    
    // allocator의 기본 분할 단위
    // 4096 -> 256 -> 16 x 16 셀 그리드
    static constexpr uint32 SpotCellResolution = 256;
    static constexpr uint32 SpotAtlasCellsPerRow = SpotAtlasResolution / SpotCellResolution;
    
    // 하용 타일 크기
    static constexpr uint32 MinSpotTileResolution = 256;
    static constexpr uint32 MaxSpotTileResolution = 2048;
    
    // 이론상 최대 shadow 수
    static constexpr uint32 MaxSpotShadowCount = SpotAtlasCellsPerRow * SpotAtlasCellsPerRow;
    
    static constexpr uint32 DirectionalCascadeResolution = 2048;
    static constexpr uint32 DirectionalAtlasGridDimension = 2;
    static constexpr uint32 DirectionalAtlasResolution = DirectionalCascadeResolution * DirectionalAtlasGridDimension;
    static constexpr uint32 DirectionalCascadeCount = 4;
    
    static constexpr uint32 PointCubeResolution = 512;
    static constexpr uint32 MaxPointShadowCount = 8;
    static constexpr uint32 PointCubeFaceCount = 6;

    static constexpr uint32 PointAtlasResolution = 4096;
    static constexpr uint32 PointAtlasCellResolution = 256;
    static constexpr uint32 PointAtlasCellsPerRow = PointAtlasResolution / PointAtlasCellResolution;
    static constexpr uint32 MinPointTileResolution = 256;
    static constexpr uint32 MaxPointTileResolution = 1024;
    
public:
    // Spot shadow atlas 텍스처/DSV/SRV를 생성합니다.
    bool Initialize(ID3D11Device* Device);
    void Release();
    
    // 프레임 시작 시 dynamic allocation 상태를 초기화
    // 아직 cache X -> 전체 reset
    static void BeginSpotFrame();
    
    // LightComponent의 ShadowResolutionScale 결과를
    // 2048 / 1024 / 512 / 256 중 하나로 정규화
    static uint32 SnapSpotTileSize(float RequestedResolution);
    
    // 원하는 크기의 spot shadow tule을 atlas 안에서 찾음
    // 성공하면 OutSlot에 atlas 위치/크기 채워서 반환
    static bool RequestSpotSlot(uint32 DesiredResolution, FSpotAtlasSlotDesc& OutSlot);
    
    // Debug overlay가 현재 프레임의 실제 할당 결과를 볼 수 있도록 함
    static const TArray<FSpotAtlasSlotDesc>& GetActiveSpotSlots();
    static void UpdateSpotSlotDebugLightId(uint32 TileIndex, int32 DebugLightId);
    
    ID3D11DepthStencilView* GetSpotAtlasDSV() const { return SpotAtlasDSV.Get(); }
    ID3D11ShaderResourceView* GetSpotAtlasSRV() const { return SpotAtlasSRV.Get(); }
    ID3D11RenderTargetView* GetSpotVSMAtlasRTV() const { return SpotVSMAtlasRTV.Get(); }
    ID3D11ShaderResourceView* GetSpotVSMAtlasSRV() const { return SpotVSMAtlasSRV.Get(); }
    
    bool InitializeDirectionalAtlas(ID3D11Device* Device);
    ID3D11DepthStencilView* GetDirectionalAtlasDSV() const { return DirectionalAtlasDSV.Get(); }
    ID3D11ShaderResourceView* GetDirectionalAtlasSRV() const { return DirectionalAtlasSRV.Get(); }
    ID3D11RenderTargetView* GetDirectionalVSMAtlasRTV() const { return DirectionalVSMAtlasRTV.Get(); }
    ID3D11ShaderResourceView* GetDirectionalVSMAtlasSRV() const { return DirectionalVSMAtlasSRV.Get(); }
    
    static const TArray<FDirectionalAtlasSlotDesc>& GetDirectionalCascadeSlots();

    bool InitializePointAtlas(ID3D11Device* Device);
    static void BeginPointFrame();
    static uint32 SnapPointTileSize(float RequestedResolution);
    static bool RequestPointAtlasSlot(uint32 DesiredResolution, FPointAtlasSlotDesc& OutSlot);
    static const TArray<FPointAtlasSlotDesc>& GetActivePointSlots() { return ActivePointSlots; }
    ID3D11DepthStencilView* GetPointAtlasDSV() const { return PointAtlasDSV.Get(); }
    ID3D11ShaderResourceView* GetPointAtlasSRV() const { return PointAtlasSRV.Get(); }
    ID3D11RenderTargetView* GetPointVSMAtlasRTV() const { return PointVSMAtlasRTV.Get(); }
    ID3D11ShaderResourceView* GetPointVSMAtlasSRV() const { return PointVSMAtlasSRV.Get(); }

private:
    // allocator가 실제로 처리 가능한 PoT 타일 크기로 보정합니다.
    // 반환값은 256 / 512 / 1024 / 2048 중 하나입니다.
    static uint32 SanitizeSpotTileSize(uint32 DesiredResolution);
    static bool TryAllocateSpotSlot(uint32 TileResolution, FSpotAtlasSlotDesc& OutSlot);
    static bool IsSpotRegionFree(uint32 CellX, uint32 CellY, uint32 CellSpan);
    static void MarkSpotRegion(uint32 CellX, uint32 CellY, uint32 CellSpan, bool bOccupied);
    static void BuildSpotSlotDesc(uint32 CellX, uint32 CellY, uint32 TileResolution, uint32 TileIndex, FSpotAtlasSlotDesc& OutSlot);
  
    static uint32 SanitizePointTileSize(uint32 DesiredResolution);
    static bool TryAllocatePointFaceTiles(uint32 TileResolution, FPointAtlasSlotDesc& OutSlot);
    static bool TryAllocatePointFaceTile(uint32 CellSpan,uint32& OutCellX, uint32& OutCellY);
    static bool IsPointRegionFree(uint32 CellX, uint32 CellY, uint32 CellSpan);
    static void MarkPointRegion(uint32 CellX, uint32 CellY, uint32 CellSpan, bool bOccupied);
    static void BuildPointSlotDesc(const uint32 FaceCellX[PointCubeFaceCount], const uint32 FaceCellY[PointCubeFaceCount], 
       uint32 TileResolution, uint32 CubeIndex, FPointAtlasSlotDesc& OutSlot);
    
private:
    // --- Spot Atlas --- 
    TComPtr<ID3D11Texture2D> SpotAtlasTexture;
    TComPtr<ID3D11DepthStencilView> SpotAtlasDSV;
    TComPtr<ID3D11ShaderResourceView> SpotAtlasSRV;

	TComPtr<ID3D11Texture2D> SpotVSMAtlasTexture;
    TComPtr<ID3D11RenderTargetView> SpotVSMAtlasRTV;
    TComPtr<ID3D11ShaderResourceView> SpotVSMAtlasSRV;

    // 16x16 cell occupancy map
    // 각 셀은 256x256 영역을 의미함
    static TArray<uint8> SpotCellOccupancy;
    static TArray<FSpotAtlasSlotDesc> ActiveSpotSlots;
    
    // --- Directional Atlas ---
    TComPtr<ID3D11Texture2D> DirectionalAtlasTexture;
    TComPtr<ID3D11DepthStencilView> DirectionalAtlasDSV;
    TComPtr<ID3D11ShaderResourceView> DirectionalAtlasSRV;

	TComPtr<ID3D11Texture2D> DirectionalVSMAtlasTexture;
    TComPtr<ID3D11RenderTargetView> DirectionalVSMAtlasRTV;
    TComPtr<ID3D11ShaderResourceView> DirectionalVSMAtlasSRV;
    
    static TArray<FDirectionalAtlasSlotDesc> DirectionalCascadeSlots;
    
    // --- Point Atlas ---
    // TComPtr<ID3D11Texture2D> PointCubeArrayTexture;
    // TArray<TComPtr<ID3D11DepthStencilView>> PointCubeFaceDSVs;
    // TComPtr<ID3D11ShaderResourceView> PointCubeArraySRV;
    TComPtr<ID3D11Texture2D> PointAtlasTexture;
    TComPtr<ID3D11DepthStencilView> PointAtlasDSV;
    TComPtr<ID3D11ShaderResourceView> PointAtlasSRV;

    TComPtr<ID3D11Texture2D> PointVSMAtlasTexture;
    TComPtr<ID3D11RenderTargetView> PointVSMAtlasRTV;
    TComPtr<ID3D11ShaderResourceView> PointVSMAtlasSRV;

    static TArray<uint8> PointAtlasCellOccupancy;
    static TArray<FPointAtlasSlotDesc> ActivePointSlots;
};
