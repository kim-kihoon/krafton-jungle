cbuffer FBlurConstants : register(b10)
{
    uint BlurDirection; // 0 = Horizontal, 1 = Vertical
    uint TileBaseX;     // 픽셀 단위. blur clamp 영역의 좌상단
    uint TileBaseY;
    uint TileSize;      // 픽셀 단위. tile의 한 변 크기
};

Texture2D<float2> InputMap : register(t14);
RWTexture2D<float2> OutputMap : register(u0);

static const int KERNEL_RADIUS = 5;
static const float GaussWeights[11] =
{
    0.0093f, 0.0280f, 0.0654f, 0.1210f, 0.1762f,
    0.1994f,
    0.1762f, 0.1210f, 0.0654f, 0.0280f, 0.0093f
};

#define TILE_SIZE   8
#define CACHE_SIZE  (TILE_SIZE + KERNEL_RADIUS * 2) // 18

groupshared float2 Cache[TILE_SIZE][CACHE_SIZE];

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void mainCS(
    uint3 GroupID : SV_GroupID,
    uint3 GroupThreadID : SV_GroupThreadID,
    uint3 DispatchID : SV_DispatchThreadID)
{
    const int2 TileBase = int2((int)TileBaseX, (int)TileBaseY);
    const int TileMaxX = (int)TileBaseX + (int)TileSize - 1;
    const int TileMaxY = (int)TileBaseY + (int)TileSize - 1;

    // 그룹의 첫 픽셀 좌표 (atlas 절대 좌표)
    int2 TexelBase = TileBase + int2(GroupID.xy) * TILE_SIZE;

    if (BlurDirection == 0)
    {
        // Horizontal — 캐시 행은 GroupThreadID.y 기준, 열은 KERNEL_RADIUS 만큼 좌우 확장
        int CacheColsPerThread = (CACHE_SIZE + TILE_SIZE - 1) / TILE_SIZE; // 3

        for (int c = 0; c < CacheColsPerThread; ++c)
        {
            int CacheCol = (int) GroupThreadID.x * CacheColsPerThread + c;
            if (CacheCol >= CACHE_SIZE)
                break;

            int TexCol = clamp(TexelBase.x + CacheCol - KERNEL_RADIUS, (int)TileBaseX, TileMaxX);
            int TexRow = clamp(TexelBase.y + (int) GroupThreadID.y, (int)TileBaseY, TileMaxY);

            Cache[GroupThreadID.y][CacheCol] = InputMap.Load(int3(TexCol, TexRow, 0));
        }
    }
    else
    {
        /// Vertical — 캐시 열은 GroupThreadID.x 기준, 행은 KERNEL_RADIUS 만큼 상하 확장
        int CacheRowsPerThread = (CACHE_SIZE + TILE_SIZE - 1) / TILE_SIZE; // 3

        for (int r = 0; r < CacheRowsPerThread; ++r)
        {
            int CacheRow = (int) GroupThreadID.y * CacheRowsPerThread + r;
            if (CacheRow >= CACHE_SIZE)
                break;

            int TexRow = clamp(TexelBase.y + CacheRow - KERNEL_RADIUS, (int)TileBaseY, TileMaxY);
            int TexCol = clamp(TexelBase.x + (int) GroupThreadID.x, (int)TileBaseX, TileMaxX);

            Cache[GroupThreadID.x][CacheRow] = InputMap.Load(int3(TexCol, TexRow, 0));
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if ((int) DispatchID.x >= (int) TileSize || (int) DispatchID.y >= (int) TileSize)
        return;

    const int2 OutPos = TileBase + int2(DispatchID.xy);
    
    // 가우시안 합산
    float2 BlurResult = float2(0.0f, 0.0f);

    if (BlurDirection == 0)
    {
        int CacheCenter = (int) GroupThreadID.x + KERNEL_RADIUS;
        for (int k = -KERNEL_RADIUS; k <= KERNEL_RADIUS; ++k)
        {
            BlurResult += Cache[GroupThreadID.y][CacheCenter + k] * GaussWeights[k + KERNEL_RADIUS];
        }
    }
    else
    {
        int CacheCenter = (int) GroupThreadID.y + KERNEL_RADIUS;
        for (int k = -KERNEL_RADIUS; k <= KERNEL_RADIUS; ++k)
        {
            BlurResult += Cache[GroupThreadID.x][CacheCenter + k] * GaussWeights[k + KERNEL_RADIUS];
        }
    }

    OutputMap[OutPos] = BlurResult;
}