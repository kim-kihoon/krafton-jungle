#include <Core/PathManager.h>
#include <DirectXMath.h>
#include <Scene/AssetLoader.h>
#include <Scene/SceneManager.h>
#include <Scene/SceneSerializer.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <windows.h>

namespace Scene
{
namespace
{
constexpr float DEFAULT_HALF_EXTENT = 0.5f;

DirectX::XMMATRIX UnpackStoredMatrix(const Math::FPacked3x4Matrix& InPackedMatrix)
{
    return DirectX::XMMatrixSet(
        DirectX::XMVectorGetX(InPackedMatrix.Row0), DirectX::XMVectorGetX(InPackedMatrix.Row1),
        DirectX::XMVectorGetX(InPackedMatrix.Row2), 0.0f, DirectX::XMVectorGetY(InPackedMatrix.Row0),
        DirectX::XMVectorGetY(InPackedMatrix.Row1), DirectX::XMVectorGetY(InPackedMatrix.Row2), 0.0f,
        DirectX::XMVectorGetZ(InPackedMatrix.Row0), DirectX::XMVectorGetZ(InPackedMatrix.Row1),
        DirectX::XMVectorGetZ(InPackedMatrix.Row2), 0.0f, DirectX::XMVectorGetW(InPackedMatrix.Row0),
        DirectX::XMVectorGetW(InPackedMatrix.Row1), DirectX::XMVectorGetW(InPackedMatrix.Row2), 1.0f);
}

uint32_t NormalizeBaseMeshID(uint32_t InMeshID)
{
    if (InMeshID >= BILLBOARD_MESH_ID_OFFSET && InMeshID < BILLBOARD_MESH_ID_OFFSET + BASE_MESH_TYPE_COUNT)
    {
        InMeshID -= BILLBOARD_MESH_ID_OFFSET;
    }

    if (InMeshID >= TOTAL_MESH_RESOURCE_COUNT)
    {
        return 0u;
    }

    return InMeshID % BASE_MESH_TYPE_COUNT;
}
} // namespace

USceneManager::USceneManager()
{
}
USceneManager::~USceneManager()
{
}

void USceneManager::Initialize()
{
    SceneData = std::make_unique<FSceneDataSOA>();

    // 실제 씬 bounds를 읽은 뒤 BuildGrid()가 차원과 셀 크기를 다시 결정한다.
    Grid = std::make_unique<UUniformGrid>(1, 1, 1, 4.0f, SceneData.get());

    ResetScene();
}

void USceneManager::Update(float DeltaTime)
{
    // [Hot Path] 5만 개 가시성 카운트는 Culling 엔진이 직접 SceneStatistics.VisibleObjectCount를 업데이트하게 함.
}

void USceneManager::UpdateAllObjectBounds()
{
    if (!SceneData || !BoundsLookup)
        return;

    for (uint32_t Index = 0; Index < SceneData->TotalObjectCount; ++Index)
    {
        DirectX::XMMATRIX WorldMat = UnpackStoredMatrix(SceneData->WorldMatrices[Index]);
        DirectX::XMVECTOR Scale, Rot, Trans;
        DirectX::XMMatrixDecompose(&Scale, &Rot, &Trans, WorldMat);

        DirectX::XMStoreFloat4(&SceneData->RotationQuaternions[Index], DirectX::XMQuaternionNormalize(Rot));
        DirectX::XMStoreFloat3(&SceneData->Scale3D[Index], Scale);

        Math::FBox LocalAABB = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
        DirectX::XMFLOAT3 LocalCenter = {0.0f, 0.0f, 0.0f};
        float LocalRadius = 0.866f;

        // [중요] LOD 메쉬가 아닌 원본 BaseMeshID를 사용하여 바운딩 박스를 조회해야 함
        BoundsLookup(SceneData->BaseMeshIDs[Index], LocalAABB, LocalCenter, LocalRadius);

        float MaxScale =
            (std::max)({DirectX::XMVectorGetX(Scale), DirectX::XMVectorGetY(Scale), DirectX::XMVectorGetZ(Scale)});
        SceneData->Radius[Index] = LocalRadius * MaxScale;

        DirectX::XMVECTOR LocalCenterVec = DirectX::XMLoadFloat3(&LocalCenter);
        DirectX::XMVECTOR WorldCenterVec = DirectX::XMVector3TransformCoord(LocalCenterVec, WorldMat);
        SceneData->CenterX[Index] = DirectX::XMVectorGetX(WorldCenterVec);
        SceneData->CenterY[Index] = DirectX::XMVectorGetY(WorldCenterVec);
        SceneData->CenterZ[Index] = DirectX::XMVectorGetZ(WorldCenterVec);

        DirectX::XMVECTOR Corners[8];
        const auto& Min = LocalAABB.Min;
        const auto& Max = LocalAABB.Max;
        Corners[0] = DirectX::XMVectorSet(Min.x, Min.y, Min.z, 1.0f);
        Corners[1] = DirectX::XMVectorSet(Max.x, Min.y, Min.z, 1.0f);
        Corners[2] = DirectX::XMVectorSet(Min.x, Max.y, Min.z, 1.0f);
        Corners[3] = DirectX::XMVectorSet(Max.x, Max.y, Min.z, 1.0f);
        Corners[4] = DirectX::XMVectorSet(Min.x, Min.y, Max.z, 1.0f);
        Corners[5] = DirectX::XMVectorSet(Max.x, Min.y, Max.z, 1.0f);
        Corners[6] = DirectX::XMVectorSet(Min.x, Max.y, Max.z, 1.0f);
        Corners[7] = DirectX::XMVectorSet(Max.x, Max.y, Max.z, 1.0f);

        DirectX::XMVECTOR WorldMin = DirectX::XMVectorReplicate(FLT_MAX);
        DirectX::XMVECTOR WorldMax = DirectX::XMVectorReplicate(-FLT_MAX);

        for (int i = 0; i < 8; ++i)
        {
            DirectX::XMVECTOR TransformedCorner = DirectX::XMVector3TransformCoord(Corners[i], WorldMat);
            WorldMin = DirectX::XMVectorMin(WorldMin, TransformedCorner);
            WorldMax = DirectX::XMVectorMax(WorldMax, TransformedCorner);
        }

        SceneData->MinX[Index] = DirectX::XMVectorGetX(WorldMin);
        SceneData->MinY[Index] = DirectX::XMVectorGetY(WorldMin);
        SceneData->MinZ[Index] = DirectX::XMVectorGetZ(WorldMin);
        SceneData->MaxX[Index] = DirectX::XMVectorGetX(WorldMax);
        SceneData->MaxY[Index] = DirectX::XMVectorGetY(WorldMax);
        SceneData->MaxZ[Index] = DirectX::XMVectorGetZ(WorldMax);
    }
}

void USceneManager::BuildSceneBVH()
{
    if (SceneData)
        SceneBVH.Build(*SceneData);
}

Core::ESpatialStructure USceneManager::DetermineOptimalStructure() const
{
    if (!SceneData || SceneData->TotalObjectCount == 0)
        return Core::ESpatialStructure::SceneBVH;

    float SceneMinX = (std::numeric_limits<float>::max)();
    float SceneMinY = (std::numeric_limits<float>::max)();
    float SceneMinZ = (std::numeric_limits<float>::max)();
    float SceneMaxX = std::numeric_limits<float>::lowest();
    float SceneMaxY = std::numeric_limits<float>::lowest();
    float SceneMaxZ = std::numeric_limits<float>::lowest();

    float MaxObjectExtent = 0.0f;
    float TotalObjectVolume = 0.0f;

    for (uint32_t i = 0; i < SceneData->TotalObjectCount; ++i)
    {
        SceneMinX = (std::min)(SceneMinX, SceneData->MinX[i]);
        SceneMinY = (std::min)(SceneMinY, SceneData->MinY[i]);
        SceneMinZ = (std::min)(SceneMinZ, SceneData->MinZ[i]);
        SceneMaxX = (std::max)(SceneMaxX, SceneData->MaxX[i]);
        SceneMaxY = (std::max)(SceneMaxY, SceneData->MaxY[i]);
        SceneMaxZ = (std::max)(SceneMaxZ, SceneData->MaxZ[i]);

        float ExtentX = SceneData->MaxX[i] - SceneData->MinX[i];
        float ExtentY = SceneData->MaxY[i] - SceneData->MinY[i];
        float ExtentZ = SceneData->MaxZ[i] - SceneData->MinZ[i];
        MaxObjectExtent = (std::max)({MaxObjectExtent, ExtentX, ExtentY, ExtentZ});
        TotalObjectVolume += (ExtentX * ExtentY * ExtentZ);
    }

    float SceneSizeX = SceneMaxX - SceneMinX;
    float SceneSizeY = SceneMaxY - SceneMinY;
    float SceneSizeZ = SceneMaxZ - SceneMinZ;

    float MaxSceneExtent = (std::max)({SceneSizeX, SceneSizeY, SceneSizeZ});
    if (MaxObjectExtent > MaxSceneExtent * 0.1f)
    {
        return Core::ESpatialStructure::SceneBVH;
    }

    float SceneVolume = SceneSizeX * SceneSizeY * SceneSizeZ;
    float VolumePerObject = SceneVolume / static_cast<float>(SceneData->TotalObjectCount);
    float AverageObjectVolume = TotalObjectVolume / static_cast<float>(SceneData->TotalObjectCount);

    if (VolumePerObject > 30.0f)
    {
        return Core::ESpatialStructure::SceneBVH;
    }

    return Core::ESpatialStructure::UniformGrid;
}

void USceneManager::ResetScene()
{
    if (!SceneData)
        return;

    SceneData->TotalObjectCount = 0;
    SceneData->ResetRenderQueue();
    SceneData->LODLevels.fill(static_cast<uint8_t>(ELODLevel::LOD0));
    SceneData->IsVisible.fill(false);
    for (uint32_t Index = 0; Index < FSceneDataSOA::MAX_OBJECTS; ++Index)
    {
        SceneData->RotationQuaternions[Index] = {0.0f, 0.0f, 0.0f, 1.0f};
        SceneData->Scale3D[Index] = {1.0f, 1.0f, 1.0f};
        SceneData->ObjectIDs[Index] = Index;
    }
    NextObjectID = 1;
    ResetSelectionState();
    if (Grid)
        Grid->BuildGrid();
    BuildSceneBVH();
}

bool USceneManager::SpawnStaticMesh(const FSceneSpawnRequest& InRequest, bool bRebuildGrid)
{
    if (!SceneData || SceneData->TotalObjectCount >= FSceneDataSOA::MAX_OBJECTS)
        return false;

    const uint32_t ObjectIndex = SceneData->TotalObjectCount;
    DirectX::XMVECTOR Scale = {};
    DirectX::XMVECTOR Rot = {};
    DirectX::XMVECTOR Trans = {};
    DirectX::XMMatrixDecompose(&Scale, &Rot, &Trans, InRequest.WorldMatrix);

    SceneData->WorldMatrices[ObjectIndex].Store(InRequest.WorldMatrix);
    DirectX::XMStoreFloat4(&SceneData->RotationQuaternions[ObjectIndex], DirectX::XMQuaternionNormalize(Rot));
    DirectX::XMStoreFloat3(&SceneData->Scale3D[ObjectIndex], Scale);
    SceneData->ObjectIDs[ObjectIndex] = NextObjectID++;

    // 정점 기반 AABB, Sphere 계산
    Math::FBox LocalAABB = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    DirectX::XMFLOAT3 LocalCenter = {0.0f, 0.0f, 0.0f};
    float LocalRadius = 0.866f;

    if (BoundsLookup)
    {
        BoundsLookup(InRequest.MeshID, LocalAABB, LocalCenter, LocalRadius);
    }

    float MaxScale =
        (std::max)({DirectX::XMVectorGetX(Scale), DirectX::XMVectorGetY(Scale), DirectX::XMVectorGetZ(Scale)});
    SceneData->Radius[ObjectIndex] = LocalRadius * MaxScale;

    DirectX::XMVECTOR LocalCenterVec = DirectX::XMLoadFloat3(&LocalCenter);
    DirectX::XMVECTOR WorldCenterVec = DirectX::XMVector3TransformCoord(LocalCenterVec, InRequest.WorldMatrix);
    SceneData->CenterX[ObjectIndex] = DirectX::XMVectorGetX(WorldCenterVec);
    SceneData->CenterY[ObjectIndex] = DirectX::XMVectorGetY(WorldCenterVec);
    SceneData->CenterZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldCenterVec);

    DirectX::XMVECTOR Corners[8];
    const auto& Min = LocalAABB.Min;
    const auto& Max = LocalAABB.Max;
    Corners[0] = DirectX::XMVectorSet(Min.x, Min.y, Min.z, 1.0f);
    Corners[1] = DirectX::XMVectorSet(Max.x, Min.y, Min.z, 1.0f);
    Corners[2] = DirectX::XMVectorSet(Min.x, Max.y, Min.z, 1.0f);
    Corners[3] = DirectX::XMVectorSet(Max.x, Max.y, Min.z, 1.0f);
    Corners[4] = DirectX::XMVectorSet(Min.x, Min.y, Max.z, 1.0f);
    Corners[5] = DirectX::XMVectorSet(Max.x, Min.y, Max.z, 1.0f);
    Corners[6] = DirectX::XMVectorSet(Min.x, Max.y, Max.z, 1.0f);
    Corners[7] = DirectX::XMVectorSet(Max.x, Max.y, Max.z, 1.0f);

    DirectX::XMVECTOR WorldMin = DirectX::XMVectorReplicate(FLT_MAX);
    DirectX::XMVECTOR WorldMax = DirectX::XMVectorReplicate(-FLT_MAX);

    for (int i = 0; i < 8; ++i)
    {
        DirectX::XMVECTOR TransformedCorner = DirectX::XMVector3TransformCoord(Corners[i], InRequest.WorldMatrix);
        WorldMin = DirectX::XMVectorMin(WorldMin, TransformedCorner);
        WorldMax = DirectX::XMVectorMax(WorldMax, TransformedCorner);
    }

    SceneData->MinX[ObjectIndex] = DirectX::XMVectorGetX(WorldMin);
    SceneData->MinY[ObjectIndex] = DirectX::XMVectorGetY(WorldMin);
    SceneData->MinZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldMin);
    SceneData->MaxX[ObjectIndex] = DirectX::XMVectorGetX(WorldMax);
    SceneData->MaxY[ObjectIndex] = DirectX::XMVectorGetY(WorldMax);
    SceneData->MaxZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldMax);

    SceneData->MeshIDs[ObjectIndex] = InRequest.MeshID;
    SceneData->BaseMeshIDs[ObjectIndex] = InRequest.MeshID;
    SceneData->MaterialIDs[ObjectIndex] = InRequest.MaterialID;
    SceneData->LODLevels[ObjectIndex] = static_cast<uint8_t>(ELODLevel::LOD0);
    SceneData->IsVisible[ObjectIndex] = true;

    SceneData->TotalObjectCount++;

    if (bRebuildGrid)
    {
        if (Grid)
            Grid->BuildGrid();
        BuildSceneBVH();
    }
    bNeedsSpatialRebuild = true;
    return true;
}

void USceneManager::SpawnStaticMeshGrid(const FSceneGridSpawnRequest& InRequest)
{
    for (uint32_t Z = 0; Z < InRequest.Depth; ++Z)
    {
        for (uint32_t Y = 0; Y < InRequest.Height; ++Y)
        {
            for (uint32_t X = 0; X < InRequest.Width; ++X)
            {
                FSceneSpawnRequest Req;
                Req.MeshID = InRequest.MeshID;
                Req.MaterialID = InRequest.MaterialID;

                float PosX = static_cast<float>(X) * InRequest.Spacing;
                float PosY = static_cast<float>(Y) * InRequest.Spacing;
                float PosZ = static_cast<float>(Z) * InRequest.Spacing;
                Req.WorldMatrix = DirectX::XMMatrixTranslation(PosX, PosY, PosZ);

                if (!SpawnStaticMesh(Req, false))
                    return;
            }
        }
    }
    if (Grid)
        Grid->BuildGrid();
    BuildSceneBVH();
}

bool USceneManager::EnsureObjectCount(uint32_t InObjectCount)
{
    if (!SceneData || InObjectCount > FSceneDataSOA::MAX_OBJECTS)
    {
        return false;
    }

    SceneData->TotalObjectCount = InObjectCount;
    return true;
}

// Camera 직렬화 저장용
struct FVerstappenSceneHeader
{
    static const uint32_t MAGIC = 0x5653434E; // 'VSCN'
    uint32_t Magic = MAGIC;
    uint32_t ObjectCount = 0;

    // 카메라 정보 (FCameraState 기반)
    DirectX::XMFLOAT3 CameraPosition;
    float Pitch;
    float Yaw;
    float FOV;
    float Near;
    float Far;
};

bool USceneManager::SaveSceneBinary(const std::wstring& InFilePath, const Graphics::FCameraState* InCameraState) const
{
    if (!SceneData || InFilePath.empty())
        return false;

    std::wstring FinalPath = InFilePath;
    if (std::filesystem::path(InFilePath).is_relative())
        FinalPath = Core::FPathManager::GetProjectRoot() + InFilePath;

    HANDLE hFile = CreateFileW(FinalPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD BytesWritten = 0;
    const uint32_t Count = SceneData->TotalObjectCount;

    // 1. 헤더 작성
    FVerstappenSceneHeader Header;
    Header.ObjectCount = Count;

    if (InCameraState)
    {
        Header.CameraPosition = InCameraState->Position;
        Header.Pitch = DirectX::XMConvertToDegrees(InCameraState->PitchRadians);
        Header.Yaw = DirectX::XMConvertToDegrees(InCameraState->YawRadians);
        Header.FOV = InCameraState->FOVDegrees;
        Header.Near = InCameraState->NearClip;
        Header.Far = InCameraState->FarClip;
    }
    WriteFile(hFile, &Header, sizeof(Header), &BytesWritten, NULL);

    // 2. 데이터 작성 람다
    auto FastWrite = [&](const void* pData, size_t Size)
    { WriteFile(hFile, pData, static_cast<DWORD>(Size), &BytesWritten, NULL); };

    if (Count > 0)
    {
        // 기존 SOA 데이터들 (순서 유지)
        FastWrite(SceneData->MinX.data(), sizeof(float) * Count);
        FastWrite(SceneData->MinY.data(), sizeof(float) * Count);
        FastWrite(SceneData->MinZ.data(), sizeof(float) * Count);
        FastWrite(SceneData->MaxX.data(), sizeof(float) * Count);
        FastWrite(SceneData->MaxY.data(), sizeof(float) * Count);
        FastWrite(SceneData->MaxZ.data(), sizeof(float) * Count);
        FastWrite(SceneData->CenterX.data(), sizeof(float) * Count);
        FastWrite(SceneData->CenterY.data(), sizeof(float) * Count);
        FastWrite(SceneData->CenterZ.data(), sizeof(float) * Count);
        FastWrite(SceneData->Radius.data(), sizeof(float) * Count);
        FastWrite(SceneData->WorldMatrices.data(), sizeof(FPacked3x4Matrix) * Count);
        FastWrite(SceneData->RotationQuaternions.data(), sizeof(DirectX::XMFLOAT4) * Count);
        FastWrite(SceneData->Scale3D.data(), sizeof(DirectX::XMFLOAT3) * Count);
        FastWrite(SceneData->BaseMeshIDs.data(), sizeof(uint32_t) * Count);
        FastWrite(SceneData->MaterialIDs.data(), sizeof(uint32_t) * Count);
    }

    CloseHandle(hFile);
    return true;
}

bool USceneManager::LoadSceneBinary(const std::wstring& InFilePath, Graphics::FCameraState* OutCameraState)
{
    if (!SceneData || InFilePath.empty())
        return false;

    std::wstring FinalPath = InFilePath;
    if (std::filesystem::path(InFilePath).is_relative())
        FinalPath = Core::FPathManager::GetProjectRoot() + InFilePath;

    HANDLE hFile =
        CreateFileW(FinalPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD BytesRead = 0;
        FVerstappenSceneHeader Header;
        if (ReadFile(hFile, &Header, sizeof(Header), &BytesRead, NULL) && Header.Magic == FVerstappenSceneHeader::MAGIC)
        {
            // 1. 카메라 정보 즉시 복원
            if (OutCameraState)
            {
                OutCameraState->Position = Header.CameraPosition;
                OutCameraState->PitchRadians = Header.Pitch;
                OutCameraState->YawRadians = Header.Yaw;
                OutCameraState->FOVDegrees = Header.FOV;
                OutCameraState->NearClip = Header.Near;
                OutCameraState->FarClip = Header.Far;
            }

            const uint32_t Count = Header.ObjectCount;
            if (Count <= FSceneDataSOA::MAX_OBJECTS)
            {
                ResetScene();
                SceneData->TotalObjectCount = Count;
                auto FastRead = [&](void* pData, size_t Size)
                { ReadFile(hFile, pData, static_cast<DWORD>(Size), &BytesRead, NULL); };

                if (Count > 0)
                {
                    // 데이터 읽기 순서 (Save와 동일하게 유지)
                    FastRead(SceneData->MinX.data(), sizeof(float) * Count);
                    FastRead(SceneData->MinY.data(), sizeof(float) * Count);
                    FastRead(SceneData->MinZ.data(), sizeof(float) * Count);
                    FastRead(SceneData->MaxX.data(), sizeof(float) * Count);
                    FastRead(SceneData->MaxY.data(), sizeof(float) * Count);
                    FastRead(SceneData->MaxZ.data(), sizeof(float) * Count);
                    FastRead(SceneData->CenterX.data(), sizeof(float) * Count);
                    FastRead(SceneData->CenterY.data(), sizeof(float) * Count);
                    FastRead(SceneData->CenterZ.data(), sizeof(float) * Count);
                    FastRead(SceneData->Radius.data(), sizeof(float) * Count);
                    FastRead(SceneData->WorldMatrices.data(), sizeof(FPacked3x4Matrix) * Count);
                    FastRead(SceneData->RotationQuaternions.data(), sizeof(DirectX::XMFLOAT4) * Count);
                    FastRead(SceneData->Scale3D.data(), sizeof(DirectX::XMFLOAT3) * Count);
                    FastRead(SceneData->BaseMeshIDs.data(), sizeof(uint32_t) * Count);
                    FastRead(SceneData->MaterialIDs.data(), sizeof(uint32_t) * Count);

                    for (uint32_t i = 0; i < Count; ++i)
                    {
                        SceneData->MeshIDs[i] = SceneData->BaseMeshIDs[i];
                        SceneData->LODLevels[i] = static_cast<uint8_t>(ELODLevel::LOD0);
                        SceneData->ObjectIDs[i] = NextObjectID++;
                        SceneData->IsVisible[i] = true;
                    }
                }
                CloseHandle(hFile);
                if (Grid)
                    Grid->BuildGrid();
                BuildSceneBVH();
                return true;
            }
        }
        CloseHandle(hFile);
    }

    // 2. 바이너리 실패 시 JSON 폴백 (카메라 정보 전달)
    if (FAssetLoader::LoadDefaultScene(*this, OutCameraState, FinalPath))
    {
        RebuildCentersAndRadii();
        if (Grid)
            Grid->BuildGrid();
        BuildSceneBVH();
        return true;
    }

    return false;
}

bool USceneManager::AddObject(const Math::FBox& InBounds, const Math::FMatrix& InWorldMatrix, uint32_t InMeshID,
                              uint32_t InMaterialID)
{
    if (!SceneData || SceneData->TotalObjectCount >= FSceneDataSOA::MAX_OBJECTS)
        return false;

    const uint32_t ObjectIndex = SceneData->TotalObjectCount;

    DirectX::XMVECTOR Scale = {};
    DirectX::XMVECTOR Rotation = {};
    DirectX::XMVECTOR Translation = {};
    DirectX::XMMatrixDecompose(&Scale, &Rotation, &Translation, InWorldMatrix);

    SceneData->WorldMatrices[ObjectIndex].Store(InWorldMatrix);
    DirectX::XMStoreFloat4(&SceneData->RotationQuaternions[ObjectIndex], DirectX::XMQuaternionNormalize(Rotation));
    DirectX::XMStoreFloat3(&SceneData->Scale3D[ObjectIndex], Scale);
    SceneData->ObjectIDs[ObjectIndex] = NextObjectID++;

    // 정점 기반 AABB, Sphere 계산
    Math::FBox LocalAABB = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    DirectX::XMFLOAT3 LocalCenter = {0.0f, 0.0f, 0.0f};
    float LocalRadius = 0.866f;

    if (BoundsLookup)
    {
        BoundsLookup(InMeshID, LocalAABB, LocalCenter, LocalRadius);
    }

    float MaxScale =
        (std::max)({DirectX::XMVectorGetX(Scale), DirectX::XMVectorGetY(Scale), DirectX::XMVectorGetZ(Scale)});
    SceneData->Radius[ObjectIndex] = LocalRadius * MaxScale;

    DirectX::XMVECTOR LocalCenterVec = DirectX::XMLoadFloat3(&LocalCenter);
    DirectX::XMVECTOR WorldCenterVec = DirectX::XMVector3TransformCoord(LocalCenterVec, InWorldMatrix);
    SceneData->CenterX[ObjectIndex] = DirectX::XMVectorGetX(WorldCenterVec);
    SceneData->CenterY[ObjectIndex] = DirectX::XMVectorGetY(WorldCenterVec);
    SceneData->CenterZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldCenterVec);

    DirectX::XMVECTOR Corners[8];
    const auto& Min = LocalAABB.Min;
    const auto& Max = LocalAABB.Max;
    Corners[0] = DirectX::XMVectorSet(Min.x, Min.y, Min.z, 1.0f);
    Corners[1] = DirectX::XMVectorSet(Max.x, Min.y, Min.z, 1.0f);
    Corners[2] = DirectX::XMVectorSet(Min.x, Max.y, Min.z, 1.0f);
    Corners[3] = DirectX::XMVectorSet(Max.x, Max.y, Min.z, 1.0f);
    Corners[4] = DirectX::XMVectorSet(Min.x, Min.y, Max.z, 1.0f);
    Corners[5] = DirectX::XMVectorSet(Max.x, Min.y, Max.z, 1.0f);
    Corners[6] = DirectX::XMVectorSet(Min.x, Max.y, Max.z, 1.0f);
    Corners[7] = DirectX::XMVectorSet(Max.x, Max.y, Max.z, 1.0f);

    DirectX::XMVECTOR WorldMin = DirectX::XMVectorReplicate(FLT_MAX);
    DirectX::XMVECTOR WorldMax = DirectX::XMVectorReplicate(-FLT_MAX);

    for (int i = 0; i < 8; ++i)
    {
        DirectX::XMVECTOR TransformedCorner = DirectX::XMVector3TransformCoord(Corners[i], InWorldMatrix);
        WorldMin = DirectX::XMVectorMin(WorldMin, TransformedCorner);
        WorldMax = DirectX::XMVectorMax(WorldMax, TransformedCorner);
    }

    SceneData->MinX[ObjectIndex] = DirectX::XMVectorGetX(WorldMin);
    SceneData->MinY[ObjectIndex] = DirectX::XMVectorGetY(WorldMin);
    SceneData->MinZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldMin);
    SceneData->MaxX[ObjectIndex] = DirectX::XMVectorGetX(WorldMax);
    SceneData->MaxY[ObjectIndex] = DirectX::XMVectorGetY(WorldMax);
    SceneData->MaxZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldMax);

    SceneData->MeshIDs[ObjectIndex] = InMeshID;
    SceneData->BaseMeshIDs[ObjectIndex] = InMeshID;
    SceneData->MaterialIDs[ObjectIndex] = InMaterialID;
    SceneData->IsVisible[ObjectIndex] = true;

    SceneData->TotalObjectCount++;
    if (Grid)
        Grid->BuildGrid();
    BuildSceneBVH();
    bNeedsSpatialRebuild = true;
    return true;
}

bool USceneManager::AddObjectPacked(const Math::FBox& InBounds, const Math::FPacked3x4Matrix& InWorldMatrix,
                                    uint32_t InMeshID, uint32_t InMaterialID)
{
    if (!SceneData || SceneData->TotalObjectCount >= FSceneDataSOA::MAX_OBJECTS)
        return false;

    const uint32_t ObjectIndex = SceneData->TotalObjectCount;

    DirectX::XMMATRIX WorldMat = UnpackStoredMatrix(InWorldMatrix);
    DirectX::XMVECTOR Scale = {};
    DirectX::XMVECTOR Rotation = {};
    DirectX::XMVECTOR Translation = {};
    DirectX::XMMatrixDecompose(&Scale, &Rotation, &Translation, WorldMat);

    SceneData->WorldMatrices[ObjectIndex] = InWorldMatrix;
    DirectX::XMStoreFloat4(&SceneData->RotationQuaternions[ObjectIndex], DirectX::XMQuaternionNormalize(Rotation));
    DirectX::XMStoreFloat3(&SceneData->Scale3D[ObjectIndex], Scale);
    SceneData->ObjectIDs[ObjectIndex] = NextObjectID++;

    // 정점 기반 AABB, Sphere 계산
    Math::FBox LocalAABB = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    DirectX::XMFLOAT3 LocalCenter = {0.0f, 0.0f, 0.0f};
    float LocalRadius = 0.866f;

    if (BoundsLookup)
    {
        BoundsLookup(InMeshID, LocalAABB, LocalCenter, LocalRadius);
    }

    float MaxScale =
        (std::max)({DirectX::XMVectorGetX(Scale), DirectX::XMVectorGetY(Scale), DirectX::XMVectorGetZ(Scale)});
    SceneData->Radius[ObjectIndex] = LocalRadius * MaxScale;

    DirectX::XMVECTOR LocalCenterVec = DirectX::XMLoadFloat3(&LocalCenter);
    DirectX::XMVECTOR WorldCenterVec = DirectX::XMVector3TransformCoord(LocalCenterVec, WorldMat);
    SceneData->CenterX[ObjectIndex] = DirectX::XMVectorGetX(WorldCenterVec);
    SceneData->CenterY[ObjectIndex] = DirectX::XMVectorGetY(WorldCenterVec);
    SceneData->CenterZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldCenterVec);

    DirectX::XMVECTOR Corners[8];
    const auto& Min = LocalAABB.Min;
    const auto& Max = LocalAABB.Max;
    Corners[0] = DirectX::XMVectorSet(Min.x, Min.y, Min.z, 1.0f);
    Corners[1] = DirectX::XMVectorSet(Max.x, Min.y, Min.z, 1.0f);
    Corners[2] = DirectX::XMVectorSet(Min.x, Max.y, Min.z, 1.0f);
    Corners[3] = DirectX::XMVectorSet(Max.x, Max.y, Min.z, 1.0f);
    Corners[4] = DirectX::XMVectorSet(Min.x, Min.y, Max.z, 1.0f);
    Corners[5] = DirectX::XMVectorSet(Max.x, Min.y, Max.z, 1.0f);
    Corners[6] = DirectX::XMVectorSet(Min.x, Max.y, Max.z, 1.0f);
    Corners[7] = DirectX::XMVectorSet(Max.x, Max.y, Max.z, 1.0f);

    DirectX::XMVECTOR WorldMin = DirectX::XMVectorReplicate(FLT_MAX);
    DirectX::XMVECTOR WorldMax = DirectX::XMVectorReplicate(-FLT_MAX);

    for (int i = 0; i < 8; ++i)
    {
        DirectX::XMVECTOR TransformedCorner = DirectX::XMVector3TransformCoord(Corners[i], WorldMat);
        WorldMin = DirectX::XMVectorMin(WorldMin, TransformedCorner);
        WorldMax = DirectX::XMVectorMax(WorldMax, TransformedCorner);
    }

    SceneData->MinX[ObjectIndex] = DirectX::XMVectorGetX(WorldMin);
    SceneData->MinY[ObjectIndex] = DirectX::XMVectorGetY(WorldMin);
    SceneData->MinZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldMin);
    SceneData->MaxX[ObjectIndex] = DirectX::XMVectorGetX(WorldMax);
    SceneData->MaxY[ObjectIndex] = DirectX::XMVectorGetY(WorldMax);
    SceneData->MaxZ[ObjectIndex] = DirectX::XMVectorGetZ(WorldMax);

    SceneData->MeshIDs[ObjectIndex] = InMeshID;
    SceneData->BaseMeshIDs[ObjectIndex] = InMeshID;
    SceneData->MaterialIDs[ObjectIndex] = InMaterialID;
    SceneData->IsVisible[ObjectIndex] = true;

    SceneData->TotalObjectCount++;
    if (Grid)
        Grid->BuildGrid();
    BuildSceneBVH();
    bNeedsSpatialRebuild = true;
    return true;
}

bool USceneManager::SelectObject(uint32_t InObjectIndex, bool bAdditive)
{
    if (!SceneData || InObjectIndex >= SceneData->TotalObjectCount)
        return false;

    if (!bAdditive)
    {
        ResetSelectionState();
    }

    if (!SelectionMask[InObjectIndex])
    {
        SelectionMask[InObjectIndex] = 1;
        SelectionData.SelectedObjectIndices.push_back(InObjectIndex);
    }

    SelectionData.ObjectIndex = InObjectIndex;
    RefreshSelectionMetadata();
    return true;
}

bool USceneManager::ToggleObjectSelection(uint32_t InObjectIndex)
{
    if (!SceneData || InObjectIndex >= SceneData->TotalObjectCount)
    {
        return false;
    }

    if (SelectionMask[InObjectIndex])
    {
        SelectionMask[InObjectIndex] = 0;
        auto It = std::find(SelectionData.SelectedObjectIndices.begin(), SelectionData.SelectedObjectIndices.end(),
                            InObjectIndex);
        if (It != SelectionData.SelectedObjectIndices.end())
        {
            SelectionData.SelectedObjectIndices.erase(It);
        }
    }
    else
    {
        SelectionMask[InObjectIndex] = 1;
        SelectionData.SelectedObjectIndices.push_back(InObjectIndex);
        SelectionData.ObjectIndex = InObjectIndex;
    }

    RefreshSelectionMetadata();
    return true;
}

void USceneManager::ClearSelection()
{
    ResetSelectionState();
}

void USceneManager::FlushSpatialBuilds()
{
    if (bNeedsSpatialRebuild)
    {
        RebuildCentersAndRadii();

        if (Grid)
            Grid->BuildGrid();
        BuildSceneBVH();

        bNeedsSpatialRebuild = false;
    }
}


bool USceneManager::IsObjectSelected(uint32_t InObjectIndex) const
{
    return InObjectIndex < SelectionMask.size() && SelectionMask[InObjectIndex] != 0;
}

bool USceneManager::DestroySelectedObjects()
{
    if (!SceneData || SelectionData.SelectedObjectIndices.empty())
    {
        return false;
    }

    auto RemoveSelectionIndex = [&](uint32_t InObjectIndex)
    {
        auto It = std::find(SelectionData.SelectedObjectIndices.begin(), SelectionData.SelectedObjectIndices.end(),
                            InObjectIndex);
        if (It != SelectionData.SelectedObjectIndices.end())
        {
            SelectionData.SelectedObjectIndices.erase(It);
        }
    };

    auto ReplaceSelectionIndex = [&](uint32_t InOldIndex, uint32_t InNewIndex)
    {
        for (uint32_t& SelectedIndex : SelectionData.SelectedObjectIndices)
        {
            if (SelectedIndex == InOldIndex)
            {
                SelectedIndex = InNewIndex;
            }
        }
    };

    auto SwapObjectField = []<typename T, size_t N>(std::array<T, N>& InArray, uint32_t InA, uint32_t InB)
    {
        if (InA != InB)
        {
            std::swap(InArray[InA], InArray[InB]);
        }
    };

    while (!SelectionData.SelectedObjectIndices.empty() && SceneData->TotalObjectCount > 0)
    {
        const uint32_t RemoveIndex = SelectionData.SelectedObjectIndices.back();
        SelectionData.SelectedObjectIndices.pop_back();

        if (RemoveIndex >= SceneData->TotalObjectCount)
            continue;

        const uint32_t LastIndex = SceneData->TotalObjectCount - 1;
        if (SelectionData.ObjectIndex == RemoveIndex)
        {
            SelectionData.ObjectIndex = 0;
        }

        SelectionMask[RemoveIndex] = 0;
        RemoveSelectionIndex(RemoveIndex);

        if (RemoveIndex != LastIndex)
        {
            const bool bLastWasSelected = SelectionMask[LastIndex] != 0;

            SwapObjectField(SceneData->MinX, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->MinY, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->MinZ, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->MaxX, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->MaxY, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->MaxZ, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->CenterX, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->CenterY, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->CenterZ, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->Radius, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->WorldMatrices, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->RotationQuaternions, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->Scale3D, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->ObjectIDs, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->MeshIDs, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->BaseMeshIDs, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->MaterialIDs, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->LODLevels, RemoveIndex, LastIndex);
            SwapObjectField(SceneData->IsVisible, RemoveIndex, LastIndex);
            SwapObjectField(SelectionMask, RemoveIndex, LastIndex);

            if (SelectionData.ObjectIndex == LastIndex)
            {
                SelectionData.ObjectIndex = RemoveIndex;
            }

            if (bLastWasSelected)
            {
                ReplaceSelectionIndex(LastIndex, RemoveIndex);
            }
        }

        SelectionMask[LastIndex] = 0;
        SceneData->IsVisible[LastIndex] = false;
        --SceneData->TotalObjectCount;
    }

    SelectionData.SelectedObjectIndices.erase(
        std::remove_if(SelectionData.SelectedObjectIndices.begin(), SelectionData.SelectedObjectIndices.end(),
                       [&](uint32_t InIndex) { return InIndex >= SceneData->TotalObjectCount; }),
        SelectionData.SelectedObjectIndices.end());

    RefreshSelectionMetadata();
    if (Grid)
        Grid->BuildGrid();
    BuildSceneBVH();
    return true;
}

FSceneStatistics USceneManager::GetSceneStatistics() const
{
    FSceneStatistics Stats;
    if (SceneData)
    {
        Stats.TotalObjectCount = SceneData->TotalObjectCount;
        Stats.VisibleObjectCount = SceneData->RenderCount;
    }
    return Stats;
}

void USceneManager::RefreshSelectionMetadata()
{
    SelectionData.SelectionCount = static_cast<uint32_t>(SelectionData.SelectedObjectIndices.size());
    SelectionData.bHasSelection = SelectionData.SelectionCount > 0;

    if (!SelectionData.bHasSelection || !SceneData)
    {
        SelectionData.MeshID = 0;
        SelectionData.MaterialID = 0;
        SelectionData.ObjectIndex = 0;
        return;
    }

    if (!IsObjectSelected(SelectionData.ObjectIndex))
    {
        SelectionData.ObjectIndex = SelectionData.SelectedObjectIndices.back();
    }

    SelectionData.MeshID = SceneData->BaseMeshIDs[SelectionData.ObjectIndex];
    SelectionData.MaterialID = SceneData->MaterialIDs[SelectionData.ObjectIndex];
}

void USceneManager::ResetSelectionState()
{
    SelectionData = {};
    SelectionMask.fill(0);
}

void USceneManager::RebuildCentersAndRadii()
{
    if (!SceneData)
        return;

    UpdateAllObjectBounds();
}
} // namespace Scene
