#include <Core/PathManager.h>
#include <DirectXMath.h>
#include <Graphics/Renderer.h>
#include <Math/MathTypes.h>
#include <Scene/SceneData.h>
#include <Scene/SceneManager.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>
#include <wincodec.h>

namespace Graphics
{
namespace
{
constexpr uint32_t MESH_CACHE_MAGIC = 0x48444F4Cu;
constexpr uint32_t MESH_CACHE_VERSION = 1;
constexpr float LOD1_SIMPLIFY_RATIO = 0.5f;
constexpr float LOD2_SIMPLIFY_RATIO = 0.2f;

struct FPerFrameConstants
{
    DirectX::XMFLOAT4X4 ViewProj;
    DirectX::XMFLOAT4 CameraRight;
    DirectX::XMFLOAT4 CameraUp;
    DirectX::XMFLOAT4 CameraPos;
};

struct FPerObjectConstants
{
    DirectX::XMFLOAT4 Row0;
    DirectX::XMFLOAT4 Row1;
    DirectX::XMFLOAT4 Row2;
    DirectX::XMFLOAT4 Padding;
};

struct FMaterialConstants
{
    DirectX::XMFLOAT4 BaseColor;
};

struct FObjVertexKey
{
    int PositionIndex = -1;
    int TexCoordIndex = -1;
    int NormalIndex = -1;
    int MaterialIndex = -1;

    bool operator==(const FObjVertexKey& InOther) const
    {
        return PositionIndex == InOther.PositionIndex && TexCoordIndex == InOther.TexCoordIndex &&
               NormalIndex == InOther.NormalIndex && MaterialIndex == InOther.MaterialIndex;
    }
};

struct FObjVertexKeyHasher
{
    size_t operator()(const FObjVertexKey& k) const noexcept
    {
        size_t h = std::hash<int>{}(k.PositionIndex);
        h ^= std::hash<int>{}(k.TexCoordIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.NormalIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.MaterialIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct FMeshCacheHeader
{
    uint32_t Magic = MESH_CACHE_MAGIC;
    uint32_t Version = MESH_CACHE_VERSION;
    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;
    int64_t SourceWriteTime = 0;
    uint64_t SourceFileSize = 0;
};

struct FClusterVertex
{
    double PositionX = 0.0;
    double PositionY = 0.0;
    double PositionZ = 0.0;
    double NormalX = 0.0;
    double NormalY = 0.0;
    double NormalZ = 0.0;
    double TexCoordX = 0.0;
    double TexCoordY = 0.0;
    uint32_t SampleCount = 0;
};

std::wstring GetMeshCachePath(const std::wstring& SourcePath, Scene::ELODLevel LODLevel)
{
    std::filesystem::path CacheDirectory = std::filesystem::path(Core::FPathManager::GetBinPath()) / L"MeshCache";
    std::error_code EC;
    std::filesystem::create_directories(CacheDirectory, EC);

    std::filesystem::path SourceFileName(SourcePath);
    std::wstring Suffix = (LODLevel == Scene::ELODLevel::LOD1) ? L".lod1.meshbin" : L".lod2.meshbin";
    return (CacheDirectory / (SourceFileName.stem().wstring() + Suffix)).wstring();
}

bool ComputeSourceSignature(const std::wstring& SourcePath, int64_t& OutWriteTime, uint64_t& OutFileSize)
{
    const std::filesystem::path SourceFilePath(SourcePath);
    std::error_code EC;

    const auto WriteTime = std::filesystem::last_write_time(SourceFilePath, EC);
    if (EC)
        return false;
    OutWriteTime = WriteTime.time_since_epoch().count();

    OutFileSize = std::filesystem::file_size(SourceFilePath, EC);
    return !EC;
}

void UpdateMeshBounds(URenderer::FMeshResource& Resource)
{
    if (Resource.SourceVertices.empty())
    {
        Resource.LocalAABB.Min = {0.0f, 0.0f, 0.0f};
        Resource.LocalAABB.Max = {0.0f, 0.0f, 0.0f};
        Resource.LocalRadius = 0.0f;
        Resource.LocalCenter = {0.0f, 0.0f, 0.0f};
        return;
    }

    float MinX = Resource.SourceVertices[0].Position.x;
    float MinY = Resource.SourceVertices[0].Position.y;
    float MinZ = Resource.SourceVertices[0].Position.z;
    float MaxX = MinX;
    float MaxY = MinY;
    float MaxZ = MinZ;

    for (const auto& Vertex : Resource.SourceVertices)
    {
        MinX = (std::min)(MinX, Vertex.Position.x);
        MinY = (std::min)(MinY, Vertex.Position.y);
        MinZ = (std::min)(MinZ, Vertex.Position.z);
        MaxX = (std::max)(MaxX, Vertex.Position.x);
        MaxY = (std::max)(MaxY, Vertex.Position.y);
        MaxZ = (std::max)(MaxZ, Vertex.Position.z);
    }

    Resource.LocalAABB.Min = {MinX, MinY, MinZ};
    Resource.LocalAABB.Max = {MaxX, MaxY, MaxZ};
    Resource.LocalCenter = {(MinX + MaxX) * 0.5f, (MinY + MaxY) * 0.5f, (MinZ + MaxZ) * 0.5f};
    const float SizeX = MaxX - MinX;
    const float SizeY = MaxY - MinY;
    const float SizeZ = MaxZ - MinZ;
    Resource.LocalRadius = (std::max)({SizeX, SizeY, SizeZ}) * 0.5f;
}

bool UploadMeshResource(ID3D11Device* Device, URenderer::FMeshResource& Resource)
{
    if (Resource.SourceVertices.empty() || Resource.SourceIndices.empty())
        return false;

    UpdateMeshBounds(Resource);

    D3D11_BUFFER_DESC VBDesc = {static_cast<UINT>(Resource.SourceVertices.size() * sizeof(URenderer::FMeshVertex)),
                                D3D11_USAGE_DEFAULT,
                                D3D11_BIND_VERTEX_BUFFER,
                                0,
                                0,
                                0};
    D3D11_SUBRESOURCE_DATA VBData = {Resource.SourceVertices.data(), 0, 0};
    if (FAILED(Device->CreateBuffer(&VBDesc, &VBData, &Resource.VertexBuffer)))
        return false;

    D3D11_BUFFER_DESC IBDesc = {static_cast<UINT>(Resource.SourceIndices.size() * sizeof(uint32_t)),
                                D3D11_USAGE_DEFAULT,
                                D3D11_BIND_INDEX_BUFFER,
                                0,
                                0,
                                0};
    D3D11_SUBRESOURCE_DATA IBData = {Resource.SourceIndices.data(), 0, 0};
    if (FAILED(Device->CreateBuffer(&IBDesc, &IBData, &Resource.IndexBuffer)))
        return false;

    Resource.IndexCount = static_cast<uint32_t>(Resource.SourceIndices.size());
    Resource.BuildBVH();
    return true;
}

bool TryLoadSimplifiedMeshCache(const std::wstring& SourcePath, const std::wstring& CachePath,
                                std::vector<URenderer::FMeshVertex>& OutVertices, std::vector<uint32_t>& OutIndices)
{
    int64_t SourceWriteTime = 0;
    uint64_t SourceFileSize = 0;
    if (!ComputeSourceSignature(SourcePath, SourceWriteTime, SourceFileSize))
        return false;

    std::ifstream File(CachePath, std::ios::binary);
    if (!File)
        return false;

    FMeshCacheHeader Header = {};
    File.read(reinterpret_cast<char*>(&Header), sizeof(Header));
    if (!File || Header.Magic != MESH_CACHE_MAGIC || Header.Version != MESH_CACHE_VERSION ||
        Header.SourceWriteTime != SourceWriteTime || Header.SourceFileSize != SourceFileSize ||
        Header.VertexCount == 0 || Header.IndexCount == 0 || (Header.IndexCount % 3) != 0)
    {
        return false;
    }

    OutVertices.resize(Header.VertexCount);
    OutIndices.resize(Header.IndexCount);
    File.read(reinterpret_cast<char*>(OutVertices.data()), sizeof(URenderer::FMeshVertex) * Header.VertexCount);
    File.read(reinterpret_cast<char*>(OutIndices.data()), sizeof(uint32_t) * Header.IndexCount);
    return File.good();
}

bool SaveSimplifiedMeshCache(const std::wstring& SourcePath, const std::wstring& CachePath,
                             const std::vector<URenderer::FMeshVertex>& Vertices, const std::vector<uint32_t>& Indices)
{
    int64_t SourceWriteTime = 0;
    uint64_t SourceFileSize = 0;
    if (!ComputeSourceSignature(SourcePath, SourceWriteTime, SourceFileSize))
        return false;

    std::ofstream File(CachePath, std::ios::binary);
    if (!File)
        return false;

    FMeshCacheHeader Header = {};
    Header.VertexCount = static_cast<uint32_t>(Vertices.size());
    Header.IndexCount = static_cast<uint32_t>(Indices.size());
    Header.SourceWriteTime = SourceWriteTime;
    Header.SourceFileSize = SourceFileSize;

    File.write(reinterpret_cast<const char*>(&Header), sizeof(Header));
    File.write(reinterpret_cast<const char*>(Vertices.data()), sizeof(URenderer::FMeshVertex) * Vertices.size());
    File.write(reinterpret_cast<const char*>(Indices.data()), sizeof(uint32_t) * Indices.size());
    return File.good();
}

bool BuildSimplifiedMeshData(const URenderer::FMeshResource& SourceResource, float TargetRatio,
                             std::vector<URenderer::FMeshVertex>& OutVertices, std::vector<uint32_t>& OutIndices)
{
    if (SourceResource.SourceVertices.empty() || SourceResource.SourceIndices.size() < 3)
        return false;

    const auto& SourceVertices = SourceResource.SourceVertices;
    const auto& SourceIndices = SourceResource.SourceIndices;
    const size_t TargetVertexCount =
        (std::max)(static_cast<size_t>(4),
                   static_cast<size_t>(std::llround(static_cast<double>(SourceVertices.size()) * TargetRatio)));

    float MinX = SourceVertices[0].Position.x;
    float MinY = SourceVertices[0].Position.y;
    float MinZ = SourceVertices[0].Position.z;
    float MaxX = MinX;
    float MaxY = MinY;
    float MaxZ = MinZ;

    for (const auto& Vertex : SourceVertices)
    {
        MinX = (std::min)(MinX, Vertex.Position.x);
        MinY = (std::min)(MinY, Vertex.Position.y);
        MinZ = (std::min)(MinZ, Vertex.Position.z);
        MaxX = (std::max)(MaxX, Vertex.Position.x);
        MaxY = (std::max)(MaxY, Vertex.Position.y);
        MaxZ = (std::max)(MaxZ, Vertex.Position.z);
    }

    const float ExtentX = (std::max)(MaxX - MinX, 0.0001f);
    const float ExtentY = (std::max)(MaxY - MinY, 0.0001f);
    const float ExtentZ = (std::max)(MaxZ - MinZ, 0.0001f);
    const float MaxExtent = (std::max)(ExtentX, (std::max)(ExtentY, ExtentZ));
    const uint32_t BaseResolution =
        (std::max)(1u, static_cast<uint32_t>(std::round(std::cbrt(static_cast<double>(TargetVertexCount)))));
    const uint32_t GridX = (std::max)(1u, static_cast<uint32_t>(std::round((ExtentX / MaxExtent) * BaseResolution)));
    const uint32_t GridY = (std::max)(1u, static_cast<uint32_t>(std::round((ExtentY / MaxExtent) * BaseResolution)));
    const uint32_t GridZ = (std::max)(1u, static_cast<uint32_t>(std::round((ExtentZ / MaxExtent) * BaseResolution)));

    std::unordered_map<uint64_t, uint32_t> ClusterMap;
    std::vector<FClusterVertex> Clusters;
    std::vector<uint32_t> VertexRemap(SourceVertices.size(), 0);
    Clusters.reserve(TargetVertexCount);
    ClusterMap.reserve(TargetVertexCount);

    const auto QuantizeAxis = [](float Value, float MinValue, float Extent, uint32_t GridCount) -> uint32_t
    {
        if (GridCount <= 1)
            return 0;
        const float Normalized = std::clamp((Value - MinValue) / Extent, 0.0f, 0.999999f);
        return static_cast<uint32_t>(Normalized * static_cast<float>(GridCount));
    };

    for (size_t VertexIndex = 0; VertexIndex < SourceVertices.size(); ++VertexIndex)
    {
        const auto& Vertex = SourceVertices[VertexIndex];
        const uint32_t QX = QuantizeAxis(Vertex.Position.x, MinX, ExtentX, GridX);
        const uint32_t QY = QuantizeAxis(Vertex.Position.y, MinY, ExtentY, GridY);
        const uint32_t QZ = QuantizeAxis(Vertex.Position.z, MinZ, ExtentZ, GridZ);
        const uint64_t Key =
            (static_cast<uint64_t>(QX) << 42) | (static_cast<uint64_t>(QY) << 21) | static_cast<uint64_t>(QZ);

        auto It = ClusterMap.find(Key);
        uint32_t ClusterIndex = 0;
        if (It == ClusterMap.end())
        {
            ClusterIndex = static_cast<uint32_t>(Clusters.size());
            ClusterMap.emplace(Key, ClusterIndex);
            Clusters.emplace_back();
        }
        else
        {
            ClusterIndex = It->second;
        }

        auto& Cluster = Clusters[ClusterIndex];
        Cluster.PositionX += Vertex.Position.x;
        Cluster.PositionY += Vertex.Position.y;
        Cluster.PositionZ += Vertex.Position.z;
        Cluster.NormalX += Vertex.Normal.x;
        Cluster.NormalY += Vertex.Normal.y;
        Cluster.NormalZ += Vertex.Normal.z;
        Cluster.TexCoordX += Vertex.TexCoord.x;
        Cluster.TexCoordY += Vertex.TexCoord.y;
        Cluster.SampleCount += 1;
        VertexRemap[VertexIndex] = ClusterIndex;
    }

    OutVertices.clear();
    OutIndices.clear();
    OutVertices.resize(Clusters.size());

    for (size_t ClusterIndex = 0; ClusterIndex < Clusters.size(); ++ClusterIndex)
    {
        const auto& Cluster = Clusters[ClusterIndex];
        auto& Vertex = OutVertices[ClusterIndex];
        const float InvCount = 1.0f / static_cast<float>((std::max)(Cluster.SampleCount, 1u));

        Vertex.Position = {static_cast<float>(Cluster.PositionX * InvCount),
                           static_cast<float>(Cluster.PositionY * InvCount),
                           static_cast<float>(Cluster.PositionZ * InvCount)};

        DirectX::XMVECTOR Normal =
            DirectX::XMVectorSet(static_cast<float>(Cluster.NormalX), static_cast<float>(Cluster.NormalY),
                                 static_cast<float>(Cluster.NormalZ), 0.0f);
        Normal = DirectX::XMVector3Normalize(Normal);
        DirectX::XMStoreFloat3(&Vertex.Normal, Normal);

        Vertex.TexCoord = {static_cast<float>(Cluster.TexCoordX * InvCount),
                           static_cast<float>(Cluster.TexCoordY * InvCount)};
    }

    OutIndices.reserve(SourceIndices.size());
    for (size_t TriangleIndex = 0; TriangleIndex + 2 < SourceIndices.size(); TriangleIndex += 3)
    {
        const uint32_t A = VertexRemap[SourceIndices[TriangleIndex]];
        const uint32_t B = VertexRemap[SourceIndices[TriangleIndex + 1]];
        const uint32_t C = VertexRemap[SourceIndices[TriangleIndex + 2]];
        if (A == B || B == C || A == C)
            continue;

        OutIndices.push_back(A);
        OutIndices.push_back(B);
        OutIndices.push_back(C);
    }

    if (OutVertices.size() < 4 || OutIndices.size() < 3)
    {
        OutVertices = SourceVertices;
        OutIndices = SourceIndices;
    }

    return true;
}

bool ParseObjFaceIndices(const std::string& tok, int& p, int& t, int& n)
{
    p = t = n = -1;
    size_t s1 = tok.find('/');
    if (s1 == std::string::npos)
    {
        p = std::stoi(tok) - 1;
        return p >= 0;
    }
    p = std::stoi(tok.substr(0, s1)) - 1;
    size_t s2 = tok.find('/', s1 + 1);
    if (s2 == std::string::npos)
    {
        if (s1 + 1 < tok.size())
            t = std::stoi(tok.substr(s1 + 1)) - 1;
        return p >= 0;
    }
    if (s2 > s1 + 1)
        t = std::stoi(tok.substr(s1 + 1, s2 - s1 - 1)) - 1;
    if (s2 + 1 < tok.size())
        n = std::stoi(tok.substr(s2 + 1)) - 1;
    return p >= 0;
}

// .mtl 파일에서 모든 머티리얼과 텍스처 경로를 추출하는 헬퍼 함수
bool LoadMtlData(const std::wstring& mtlPath, std::unordered_map<std::string, std::wstring>& outMtlTextures)
{
    std::ifstream f{mtlPath};
    if (!f)
        return false;

    std::string line, currentMtl = "";
    while (std::getline(f, line))
    {
        std::istringstream ls(line);
        std::string pre;
        ls >> pre;
        if (pre == "newmtl")
            ls >> currentMtl;
        else if (pre == "map_Kd" && !currentMtl.empty())
        {
            std::string texName;
            ls >> texName;
            
            wchar_t wTexName[512] = {};
            MultiByteToWideChar(CP_ACP, 0, texName.c_str(), -1, wTexName, 512);
            outMtlTextures[currentMtl] = wTexName;
        }
    }
    return !outMtlTextures.empty();
}

bool LoadObjMeshData(const std::wstring& path, std::vector<URenderer::FMeshVertex>& verts,
                     std::vector<uint32_t>& indices, std::wstring& outAtlasInfo)
{
    std::filesystem::path MeshFilePath(path);
    std::wstring MeshDir = MeshFilePath.parent_path().wstring() + L"/";
    std::ifstream f{MeshFilePath};
    if (!f)
        return false;

    // 1. 머티리얼 및 아틀라스 정보 준비
    std::string line, mtlFileName;
    std::unordered_map<std::string, std::wstring> mtlTextures;
    std::vector<std::wstring> uniqueTextures;
    std::unordered_map<std::string, int> mtlToAtlasIdx;

    // .obj를 한 번 훑어서 mtl 파일명 찾기
    while (std::getline(f, line))
    {
        if (line.substr(0, 7) == "mtllib ")
        {
            mtlFileName = line.substr(7);
            break;
        }
    }
    f.clear();
    f.seekg(0);

    if (!mtlFileName.empty())
    {
        wchar_t wMtlName[512] = {};
        MultiByteToWideChar(CP_ACP, 0, mtlFileName.c_str(), -1, wMtlName, 512);
        LoadMtlData(MeshDir + wMtlName, mtlTextures);
        for (auto& pair : mtlTextures)
        {
            auto it = std::find(uniqueTextures.begin(), uniqueTextures.end(), pair.second);
            int idx = (it == uniqueTextures.end()) ? (int)uniqueTextures.size()
                                                   : (int)std::distance(uniqueTextures.begin(), it);
            if (it == uniqueTextures.end())
                uniqueTextures.push_back(pair.second);
            mtlToAtlasIdx[pair.first] = idx;
        }
    }

    // 아틀라스 그리드 계산 (예: 4개 텍스처 -> 2x2 그리드)
    int texCount = (int)uniqueTextures.size();
    int gridSize = (int)std::ceil(std::sqrt((float)texCount));
    if (gridSize < 1)
        gridSize = 1;

    // 2. 정점 파싱 및 UV Remapping
    std::vector<DirectX::XMFLOAT3> P, N;
    std::vector<DirectX::XMFLOAT2> T;
    std::unordered_map<FObjVertexKey, uint32_t, FObjVertexKeyHasher> vm;
    int currentAtlasIdx = 0;

    while (std::getline(f, line))
    {
        if (line.empty())
            continue;
        std::istringstream ls(line);
        std::string pre;
        ls >> pre;

        if (pre == "usemtl")
        {
            std::string mname;
            ls >> mname;
            if (mtlToAtlasIdx.count(mname))
                currentAtlasIdx = mtlToAtlasIdx[mname];
        }
        else if (pre == "v")
        {
            DirectX::XMFLOAT3 v;
            ls >> v.x >> v.y >> v.z;
            P.push_back(v);
        }
        else if (pre == "vt")
        {
            DirectX::XMFLOAT2 v;
            ls >> v.x >> v.y;
            v.y = 1.0f - v.y;
            T.push_back(v);
        }
        else if (pre == "vn")
        {
            DirectX::XMFLOAT3 v;
            ls >> v.x >> v.y >> v.z;
            N.push_back(v);
        }
        else if (pre == "f")
        {
            std::string t;
            std::vector<std::string> toks;
            while (ls >> t)
                toks.push_back(t);
            for (size_t tri = 1; tri + 1 < toks.size(); ++tri)
            {
                const std::array<std::string, 3> tt = {toks[0], toks[tri], toks[tri + 1]};
                for (const std::string& ft : tt)
                {
                    FObjVertexKey k = {};
                    if (!ParseObjFaceIndices(ft, k.PositionIndex, k.TexCoordIndex, k.NormalIndex))
                        continue;
                    k.MaterialIndex = currentAtlasIdx; // 머티리얼별로 정점 고유성 보장

                    auto it = vm.find(k);
                    if (it != vm.end())
                    {
                        indices.push_back(it->second);
                        continue;
                    }

                    URenderer::FMeshVertex v = {};
                    v.Position = P[k.PositionIndex];
                    v.Normal = (k.NormalIndex >= 0) ? N[k.NormalIndex] : DirectX::XMFLOAT3{0, 0, 1};

                    if (k.TexCoordIndex >= 0)
                    {
                        float u = T[k.TexCoordIndex].x;
                        float v_orig = T[k.TexCoordIndex].y;

                        // Clamp 대신 Wrap(반복)을 사용하여 0.0~1.0 사이로 맞춤
                        u = u - std::floor(u);
                        v_orig = v_orig - std::floor(v_orig);

                        int row = currentAtlasIdx / gridSize;
                        int col = currentAtlasIdx % gridSize;

                        // 아틀라스 내 해당 칸으로 좌표 변환 (DirectX V축 반전 고려)
                        v.TexCoord.x = (u + (float)col) / (float)gridSize;
                        v.TexCoord.y = (v_orig + (float)row) / (float)gridSize;
                    }

                    uint32_t vi = (uint32_t)verts.size();
                    verts.push_back(v);
                    indices.push_back(vi);
                    vm.emplace(k, vi);
                }
            }
        }
    }

    // 아틀라스에 사용된 텍스처 목록을 전달
    outAtlasInfo = L"";
    for (const auto& path : uniqueTextures)
        outAtlasInfo += MeshDir + path + L"|";
    return !verts.empty();
}

// 여러 텍스처를 하나로 합쳐서 GPU에 올리는 함수
bool LoadTextureAtlas(ID3D11Device* dev, const std::wstring& atlasInfo,
                      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv)
{
    std::vector<std::wstring> paths;
    std::wstring item;
    std::wstringstream ss(atlasInfo);
    while (std::getline(ss, item, L'|'))
        if (!item.empty())
            paths.push_back(item);

    if (paths.empty())
        return false;
    int gridSize = (int)std::ceil(std::sqrt((float)paths.size()));

    // 첫 번째 텍스처 기준으로 사이즈 결정 (Verstappen 방식: 1024x1024 고정 가정 혹은 첫 장 기준)
    UINT baseW = 1024, baseH = 1024;
    std::vector<uint32_t> atlasPixels(baseW * gridSize * baseH * gridSize, 0);

    Microsoft::WRL::ComPtr<IWICImagingFactory> fact;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fact));

    for (int i = 0; i < (int)paths.size(); ++i)
    {
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> dec;
        if (FAILED(fact->CreateDecoderFromFilename(paths[i].c_str(), nullptr, GENERIC_READ,
                                                   WICDecodeMetadataCacheOnLoad, &dec)))
            continue;

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        dec->GetFrame(0, &frame);
        Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
        fact->CreateFormatConverter(&conv);
        conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f,
                         WICBitmapPaletteTypeCustom);

        Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
        fact->CreateBitmapScaler(&scaler);
        scaler->Initialize(conv.Get(), baseW, baseH, WICBitmapInterpolationModeFant);

        std::vector<uint32_t> temp(baseW * baseH);
        // conv 대신 scaler에서 픽셀을 뽑아옵니다.
        scaler->CopyPixels(nullptr, baseW * 4, baseW * baseH * 4, (BYTE*)temp.data());

        // 아틀라스 버퍼에 복사 (이제 사이즈 불일치가 없으므로 min 연산이 필요 없습니다)
        int startRow = (i / gridSize) * baseH;
        int startCol = (i % gridSize) * baseW;

        for (UINT y = 0; y < baseH; ++y)
        {
            // memcpy로 한 줄씩 고속 복사
            memcpy(&atlasPixels[(startRow + y) * (baseW * gridSize) + startCol], &temp[y * baseW], baseW * 4);
        }
    }

    D3D11_TEXTURE2D_DESC td = {
        baseW * gridSize,    baseH * gridSize,           1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0},
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_SUBRESOURCE_DATA id = {atlasPixels.data(), baseW * gridSize * 4, 0};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    dev->CreateTexture2D(&td, &id, &tex);
    return SUCCEEDED(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv));
}

bool CreateSolidTexture(ID3D11Device* InDevice, const DirectX::XMFLOAT4& InColor,
                        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& OutTextureView)
{
    const uint8_t PixelData[4] = {static_cast<uint8_t>(InColor.x * 255.0f), static_cast<uint8_t>(InColor.y * 255.0f),
                                  static_cast<uint8_t>(InColor.z * 255.0f), static_cast<uint8_t>(InColor.w * 255.0f)};

    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = 1;
    TextureDesc.Height = 1;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA InitialData = {};
    InitialData.pSysMem = PixelData;
    InitialData.SysMemPitch = sizeof(PixelData);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
    if (FAILED(InDevice->CreateTexture2D(&TextureDesc, &InitialData, &Texture)))
        return false;
    return SUCCEEDED(InDevice->CreateShaderResourceView(Texture.Get(), nullptr, &OutTextureView));
}

bool LoadMeshResource(ID3D11Device* dev, const std::wstring& path, URenderer::FMeshResource& res)
{
    std::wstring atlasInfo;
    if (!LoadObjMeshData(path, res.SourceVertices, res.SourceIndices, atlasInfo))
        return false;
    if (!UploadMeshResource(dev, res))
        return false;

    // 1. 아틀라스 정보가 있으면 아틀라스 생성
    if (!atlasInfo.empty() && atlasInfo.find(L"|") != std::wstring::npos && atlasInfo.length() > 5)
    {
        return LoadTextureAtlas(dev, atlasInfo, res.DiffuseTextureView);
    }

    // 2. 아틀라스가 없으면 기존의 단일 텍스처/사과 폴백 로직 실행
    std::wstring texPath;
    const std::wstring MeshBase = Core::FPathManager::GetMeshPath();
    texPath = (path.find(L"bitten") != std::wstring::npos)
                  ? MeshBase + L"Bitten_Apple_tgyociqpa_Mid_2K_BaseColor.jpg"
                  : MeshBase + L"Freshly_Bitten_Apple_tgzpdhlpa_Mid_2K_BaseColor.jpg";

    // 만약 피카츄처럼 .mtl에서 단일 텍스처라도 찾았다면 atlasInfo에 첫 번째 경로가 들어있을 것
    if (!atlasInfo.empty())
    {
        std::wstring firstPath = atlasInfo.substr(0, atlasInfo.find(L"|"));
        if (std::filesystem::exists(firstPath))
            texPath = firstPath;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> fact;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fact));
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> dec;
    if (SUCCEEDED(fact->CreateDecoderFromFilename(texPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                  &dec)))
    {
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        dec->GetFrame(0, &frame);
        Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
        fact->CreateFormatConverter(&conv);
        conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0f,
                         WICBitmapPaletteTypeCustom);
        UINT w, h;
        conv->GetSize(&w, &h);
        std::vector<uint8_t> pix(w * h * 4);
        conv->CopyPixels(nullptr, w * 4, (UINT)pix.size(), pix.data());
        D3D11_TEXTURE2D_DESC td = {
            w, h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        D3D11_SUBRESOURCE_DATA id = {pix.data(), w * 4, 0};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        dev->CreateTexture2D(&td, &id, &tex);
        return SUCCEEDED(dev->CreateShaderResourceView(tex.Get(), nullptr, &res.DiffuseTextureView));
    }

    return true;
}

bool LoadSimplifiedMeshResource(ID3D11Device* Device, const std::wstring& SourcePath,
                                const URenderer::FMeshResource& SourceResource, Scene::ELODLevel LODLevel,
                                float TargetRatio, URenderer::FMeshResource& OutResource)
{
    OutResource = {};
    OutResource.DiffuseTexturePath = SourceResource.DiffuseTexturePath;
    OutResource.DiffuseTextureView = SourceResource.DiffuseTextureView;

    const std::wstring CachePath = GetMeshCachePath(SourcePath, LODLevel);
    if (!TryLoadSimplifiedMeshCache(SourcePath, CachePath, OutResource.SourceVertices, OutResource.SourceIndices))
    {
        if (!BuildSimplifiedMeshData(SourceResource, TargetRatio, OutResource.SourceVertices,
                                     OutResource.SourceIndices))
        {
            return false;
        }

        SaveSimplifiedMeshCache(SourcePath, CachePath, OutResource.SourceVertices, OutResource.SourceIndices);
    }

    return UploadMeshResource(Device, OutResource);
}
} // anonymous namespace

void URenderer::FMeshResource::BuildBVH()
{
    MeshBVH.Nodes.clear();
    MeshBVH.TriangleIndices.clear();

    uint32_t TriangleCount = (uint32_t)SourceIndices.size() / 3;
    if (TriangleCount == 0)
        return;

    MeshBVH.TriangleIndices.reserve(TriangleCount);
    for (uint32_t i = 0; i < TriangleCount; ++i)
        MeshBVH.TriangleIndices.push_back(i);

    // Pre-allocate nodes to avoid reallocations during build
    MeshBVH.Nodes.reserve(TriangleCount * 2);
    MeshBVH.Nodes.emplace_back();

    struct BuildState
    {
        uint32_t NodeIndex;
        uint32_t TriStart;
        uint32_t TriCount;
    };
    std::vector<BuildState> Stack;
    Stack.push_back({0, 0, TriangleCount});

    while (!Stack.empty())
    {
        BuildState State = Stack.back();
        Stack.pop_back();

        // Calculate bounds
        Math::FBox Bounds;
        for (uint32_t i = 0; i < State.TriCount; ++i)
        {
            uint32_t TriIdx = MeshBVH.TriangleIndices[State.TriStart + i];
            Bounds.Expand(SourceVertices[SourceIndices[TriIdx * 3]].Position);
            Bounds.Expand(SourceVertices[SourceIndices[TriIdx * 3 + 1]].Position);
            Bounds.Expand(SourceVertices[SourceIndices[TriIdx * 3 + 2]].Position);
        }
        MeshBVH.Nodes[State.NodeIndex].Bounds = Bounds;

        if (State.TriCount <= 4)
        {
            MeshBVH.Nodes[State.NodeIndex].TriangleIndex = State.TriStart;
            MeshBVH.Nodes[State.NodeIndex].TriangleCount = State.TriCount;
            continue;
        }

        // Split
        float SizeX = Bounds.Max.x - Bounds.Min.x;
        float SizeY = Bounds.Max.y - Bounds.Min.y;
        float SizeZ = Bounds.Max.z - Bounds.Min.z;
        int Axis = (SizeX > SizeY && SizeX > SizeZ) ? 0 : (SizeY > SizeZ ? 1 : 2);
        float SplitPos =
            0.5f * (reinterpret_cast<float*>(&Bounds.Min)[Axis] + reinterpret_cast<float*>(&Bounds.Max)[Axis]);

        uint32_t i = State.TriStart;
        uint32_t j = State.TriStart + State.TriCount - 1;
        while (i <= j)
        {
            uint32_t TriIdx = MeshBVH.TriangleIndices[i];
            const auto& V0 = SourceVertices[SourceIndices[TriIdx * 3]].Position;
            const auto& V1 = SourceVertices[SourceIndices[TriIdx * 3 + 1]].Position;
            const auto& V2 = SourceVertices[SourceIndices[TriIdx * 3 + 2]].Position;
            float Centroid = (reinterpret_cast<const float*>(&V0)[Axis] + reinterpret_cast<const float*>(&V1)[Axis] +
                              reinterpret_cast<const float*>(&V2)[Axis]) /
                             3.0f;

            if (Centroid < SplitPos)
                i++;
            else
            {
                std::swap(MeshBVH.TriangleIndices[i], MeshBVH.TriangleIndices[j]);
                j--;
            }
        }

        uint32_t LeftCount = i - State.TriStart;
        if (LeftCount == 0 || LeftCount == State.TriCount)
            LeftCount = State.TriCount / 2;

        uint32_t LeftIdx = (uint32_t)MeshBVH.Nodes.size();
        MeshBVH.Nodes.emplace_back();
        uint32_t RightIdx = (uint32_t)MeshBVH.Nodes.size();
        MeshBVH.Nodes.emplace_back();

        MeshBVH.Nodes[State.NodeIndex].LeftChild = LeftIdx;
        MeshBVH.Nodes[State.NodeIndex].RightChild = RightIdx;

        Stack.push_back({RightIdx, State.TriStart + LeftCount, State.TriCount - LeftCount});
        Stack.push_back({LeftIdx, State.TriStart, LeftCount});
    }
}

bool URenderer::FMeshResource::Raycast(const Math::FRay& LocalRay, float& OutT) const
{
    if (MeshBVH.Nodes.empty())
        return false;

    float NearestT = OutT > 0.0f ? OutT : FLT_MAX;
    bool bHit = false;

    // Use a fixed-size local stack to avoid heap allocation
    uint32_t Stack[64];
    uint32_t StackPtr = 0;

    float RootT;
    if (!LocalRay.Intersects(MeshBVH.Nodes[0].Bounds, RootT))
        return false;
    if (RootT > NearestT)
        return false;

    Stack[StackPtr++] = 0;

    while (StackPtr > 0)
    {
        uint32_t NodeIdx = Stack[--StackPtr];
        const FBVHNode& Node = MeshBVH.Nodes[NodeIdx];

        if (Node.IsLeaf())
        {
            for (uint32_t i = 0; i < Node.TriangleCount; ++i)
            {
                uint32_t TriIdx = MeshBVH.TriangleIndices[Node.TriangleIndex + i];
                DirectX::XMVECTOR V0 = DirectX::XMLoadFloat3(&SourceVertices[SourceIndices[TriIdx * 3]].Position);
                DirectX::XMVECTOR V1 = DirectX::XMLoadFloat3(&SourceVertices[SourceIndices[TriIdx * 3 + 1]].Position);
                DirectX::XMVECTOR V2 = DirectX::XMLoadFloat3(&SourceVertices[SourceIndices[TriIdx * 3 + 2]].Position);

                DirectX::XMVECTOR LocalOrigin = DirectX::XMLoadFloat3(&LocalRay.Origin);
                DirectX::XMVECTOR LocalDir = DirectX::XMLoadFloat3(&LocalRay.Direction);

                DirectX::XMVECTOR Edge1 = DirectX::XMVectorSubtract(V1, V0);
                DirectX::XMVECTOR Edge2 = DirectX::XMVectorSubtract(V2, V0);
                DirectX::XMVECTOR H = DirectX::XMVector3Cross(LocalDir, Edge2);

                float A = DirectX::XMVectorGetX(DirectX::XMVector3Dot(Edge1, H));
                if (A < 0.00001f)
                    continue;

                float F = 1.0f / A;
                DirectX::XMVECTOR S = DirectX::XMVectorSubtract(LocalOrigin, V0);
                float U = F * DirectX::XMVectorGetX(DirectX::XMVector3Dot(S, H));
                if (U < 0.0f || U > 1.0f)
                    continue;

                DirectX::XMVECTOR Q = DirectX::XMVector3Cross(S, Edge1);
                float V = F * DirectX::XMVectorGetX(DirectX::XMVector3Dot(LocalDir, Q));
                if (V < 0.0f || U + V > 1.0f)
                    continue;

                float T = F * DirectX::XMVectorGetX(DirectX::XMVector3Dot(Edge2, Q));
                if (T > 0.00001f && T < NearestT)
                {
                    NearestT = T;
                    bHit = true;
                }
            }
        }
        else
        {
            // Front-to-back traversal optimization: Check child distances and push closer one last
            float tL, tR;
            bool hitL = LocalRay.Intersects(MeshBVH.Nodes[Node.LeftChild].Bounds, tL) && tL < NearestT;
            bool hitR = LocalRay.Intersects(MeshBVH.Nodes[Node.RightChild].Bounds, tR) && tR < NearestT;

            if (hitL && hitR)
            {
                if (tL < tR)
                {
                    Stack[StackPtr++] = Node.RightChild;
                    Stack[StackPtr++] = Node.LeftChild;
                }
                else
                {
                    Stack[StackPtr++] = Node.LeftChild;
                    Stack[StackPtr++] = Node.RightChild;
                }
            }
            else if (hitL)
                Stack[StackPtr++] = Node.LeftChild;
            else if (hitR)
                Stack[StackPtr++] = Node.RightChild;
        }
    }

    if (bHit)
        OutT = NearestT;
    return bHit;
}

// ============================================================================
URenderer::URenderer() = default;
URenderer::~URenderer() = default;

// ============================================================================
bool URenderer::Initialize(HWND InWindowHandle, int InWidth, int InHeight)
{
    ViewportWidth = static_cast<uint32_t>(InWidth);
    ViewportHeight = static_cast<uint32_t>(InHeight);
    SceneViewportX = 0;
    SceneViewportY = 0;
    SceneViewportWidth = ViewportWidth;
    SceneViewportHeight = ViewportHeight;

    DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
    SwapChainDesc.BufferCount = 3; // Triple Buffering
    SwapChainDesc.BufferDesc.Width = InWidth;
    SwapChainDesc.BufferDesc.Height = InHeight;
    SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SwapChainDesc.BufferDesc.RefreshRate = {0, 1}; // Uncapped
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.OutputWindow = InWindowHandle;
    SwapChainDesc.SampleDesc = {1, 0};
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    SwapChainDesc.Windowed = TRUE;

    const D3D_FEATURE_LEVEL FeatureLevels[] = {D3D_FEATURE_LEVEL_11_0};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, FeatureLevels, 1,
                                             D3D11_SDK_VERSION, &SwapChainDesc, &SwapChain, &Device, nullptr,
                                             &Context)))
        return false;

    Context.As(&Context1);

    // ── RTV ──────────────────────────────────────────────────────────────────
    {
        ComPtr<ID3D11Texture2D> BackBuffer;
        if (FAILED(SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer))))
            return false;
        if (FAILED(Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &MainRenderTargetView)))
            return false;
    }

    // ── Depth Buffer (R32_TYPELESS — DSV + SRV 동시 사용) ────────────────────
    {
        ComPtr<ID3D11Texture2D> DepthBuffer;
        D3D11_TEXTURE2D_DESC DepthDesc = {};
        DepthDesc.Width = InWidth;
        DepthDesc.Height = InHeight;
        DepthDesc.MipLevels = 1;
        DepthDesc.ArraySize = 1;
        DepthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        DepthDesc.SampleDesc = {1, 0};
        DepthDesc.Usage = D3D11_USAGE_DEFAULT;
        DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(Device->CreateTexture2D(&DepthDesc, nullptr, &DepthBuffer)))
            return false;

        // DSV : D32_FLOAT
        D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
        DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
        DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        DSVDesc.Texture2D.MipSlice = 0;
        if (FAILED(Device->CreateDepthStencilView(DepthBuffer.Get(), &DSVDesc, &DepthStencilView)))
            return false;

        // SRV : R32_FLOAT → CS에서 직접 읽기 가능
        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MostDetailedMip = 0;
        SRVDesc.Texture2D.MipLevels = 1;
        if (FAILED(Device->CreateShaderResourceView(DepthBuffer.Get(), &SRVDesc, &DepthCopySRV)))
            return false;
    }

    // ── HUD ──────────────────────────────────────────────────────────────────
    HUD = std::make_unique<FHUD>();
    if (!HUD->Initialize(Device.Get(), Context.Get()))
        return false;

    // DebugRenderer 초기화

    DebugRenderer = std::make_unique<UDebugRenderer>();
    if (!DebugRenderer->Initialize(Device.Get()))
        return false;

    if (!CreateDefaultResources())
        return false;

    if (!EnsureSceneViewportResources(ViewportWidth, ViewportHeight))
        return false;

    return true;
}

// ============================================================================
void URenderer::RenderHUD()
{
    if (HUD)
    {
        HUD->Update(CurrentMetrics, ViewportWidth, ViewportHeight);
        HUD->Render();
    }
}

void URenderer::SetSceneViewportRect(int X, int Y, int Width, int Height)
{
    SceneViewportX = X;
    SceneViewportY = Y;
    SceneViewportWidth = static_cast<uint32_t>((std::max)(Width, 1));
    SceneViewportHeight = static_cast<uint32_t>((std::max)(Height, 1));
}

void URenderer::BindMainRenderTarget()
{
    Context->OMSetRenderTargets(1, MainRenderTargetView.GetAddressOf(), nullptr);
    D3D11_VIEWPORT Viewport = {0.0f, 0.0f, static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight),
                               0.0f, 1.0f};
    Context->RSSetViewports(1, &Viewport);
}

bool URenderer::EnsureSceneViewportResources(uint32_t Width, uint32_t Height)
{
    Width = (std::max)(Width, 1u);
    Height = (std::max)(Height, 1u);

    if (SceneRenderTargetView && SceneViewportSRV && DepthStencilView && DepthCopySRV &&
        SceneRenderTargetWidth == Width && SceneRenderTargetHeight == Height)
    {
        return true;
    }

    Context->OMSetRenderTargets(0, nullptr, nullptr);
    SceneRenderTargetView.Reset();
    SceneViewportSRV.Reset();
    DepthStencilView.Reset();
    DepthCopySRV.Reset();

    {
        ComPtr<ID3D11Texture2D> SceneColorTexture;
        D3D11_TEXTURE2D_DESC ColorDesc = {};
        ColorDesc.Width = Width;
        ColorDesc.Height = Height;
        ColorDesc.MipLevels = 1;
        ColorDesc.ArraySize = 1;
        ColorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ColorDesc.SampleDesc = {1, 0};
        ColorDesc.Usage = D3D11_USAGE_DEFAULT;
        ColorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(Device->CreateTexture2D(&ColorDesc, nullptr, &SceneColorTexture)))
            return false;
        if (FAILED(Device->CreateRenderTargetView(SceneColorTexture.Get(), nullptr, &SceneRenderTargetView)))
            return false;
        if (FAILED(Device->CreateShaderResourceView(SceneColorTexture.Get(), nullptr, &SceneViewportSRV)))
            return false;
    }

    {
        ComPtr<ID3D11Texture2D> DepthBuffer;
        D3D11_TEXTURE2D_DESC DepthDesc = {};
        DepthDesc.Width = Width;
        DepthDesc.Height = Height;
        DepthDesc.MipLevels = 1;
        DepthDesc.ArraySize = 1;
        DepthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        DepthDesc.SampleDesc = {1, 0};
        DepthDesc.Usage = D3D11_USAGE_DEFAULT;
        DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(Device->CreateTexture2D(&DepthDesc, nullptr, &DepthBuffer)))
            return false;

        D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
        DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
        DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        DSVDesc.Texture2D.MipSlice = 0;
        if (FAILED(Device->CreateDepthStencilView(DepthBuffer.Get(), &DSVDesc, &DepthStencilView)))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        SRVDesc.Texture2D.MostDetailedMip = 0;
        SRVDesc.Texture2D.MipLevels = 1;
        if (FAILED(Device->CreateShaderResourceView(DepthBuffer.Get(), &SRVDesc, &DepthCopySRV)))
            return false;
    }

    HiZTexture.Reset();
    HiZSRV.Reset();
    HiZMipUAVs.clear();
    HiZMipSRVs.clear();
    BoundsBuffer.Reset();
    BoundsSRV.Reset();
    VisibilityBuffer.Reset();
    VisibilityUAV.Reset();
    VisibilityStagingBuffers[0].Reset();
    VisibilityStagingBuffers[1].Reset();
    CullParamBuffer.Reset();
    HiZBuildParamBuffer.Reset();
    PointClampSamplerState.Reset();
    HiZWidth = 0;
    HiZHeight = 0;
    HiZMipCount = 0;
    StagingReadIndex = 0;
    StagingWriteIndex = 1;
    bFirstFrame = true;
    bHasPrevFrame = false;
    InitHiZResources(Width, Height);

    SceneRenderTargetWidth = Width;
    SceneRenderTargetHeight = Height;
    return true;
}

// ============================================================================
void URenderer::Resize(int Width, int Height)
{
    if (Width == 0 || Height == 0 || !SwapChain)
        return;

    ViewportWidth = static_cast<uint32_t>(Width);
    ViewportHeight = static_cast<uint32_t>(Height);
    SceneViewportX = 0;
    SceneViewportY = 0;
    SceneViewportWidth = ViewportWidth;
    SceneViewportHeight = ViewportHeight;

    Context->OMSetRenderTargets(0, nullptr, nullptr);
    MainRenderTargetView.Reset();
    SceneRenderTargetView.Reset();
    SceneViewportSRV.Reset();
    DepthStencilView.Reset();
    DepthCopySRV.Reset();
    SceneRenderTargetWidth = 0;
    SceneRenderTargetHeight = 0;

    if (FAILED(SwapChain->ResizeBuffers(0, ViewportWidth, ViewportHeight, DXGI_FORMAT_UNKNOWN,
                                        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)))
        return;

    {
        ComPtr<ID3D11Texture2D> BackBuffer;
        if (SUCCEEDED(SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer))))
            Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &MainRenderTargetView);
    }

    if (!EnsureSceneViewportResources(SceneViewportWidth, SceneViewportHeight))
        return;
}

// ============================================================================
const URenderer::FMeshResource* URenderer::GetMeshResource(uint32_t MeshID) const
{
    if (MeshID < TOTAL_MESH_RESOURCE_COUNT)
    {
        MeshID %= BASE_MESH_TYPES;
    }

    if (MeshID >= BILLBOARD_MESH_ID_OFFSET && MeshID < BILLBOARD_MESH_ID_OFFSET + BASE_MESH_TYPES)
    {
        MeshID -= BILLBOARD_MESH_ID_OFFSET;
    }

    if (MeshID < BASE_MESH_TYPES)
        return &MeshResources[MeshID];
    return nullptr;
}

// ============================================================================
uint32_t URenderer::GetOrLoadMesh(const std::wstring& FileName)
{
    // 1. 이미 로드된 메쉬인지 확인 (Flyweight Check)
    auto It = MeshPathMap.find(FileName);
    if (It != MeshPathMap.end())
    {
        return It->second;
    }

    // 2. 신규 로드 시 슬롯 제한 확인 (BASE_MESH_TYPES 준수)
    if (NextBaseMeshID >= BASE_MESH_TYPES)
    {
        return 0xFFFFFFFF;
    }

    uint32_t BaseMeshID = NextBaseMeshID++;
    const std::wstring& WPath = FileName;

    // 3. LOD0 로드 및 버퍼 생성
    FMeshResource& LOD0 = MeshResources[Scene::EncodeRenderMeshID(BaseMeshID, Scene::ELODLevel::LOD0)];
    if (!LoadMeshResource(Device.Get(), WPath, LOD0))
    {
        NextBaseMeshID--; // 실패 시 롤백
        return 0xFFFFFFFF;
    }

    if (!LOD0.DiffuseTextureView)
        LOD0.DiffuseTextureView = DefaultWhiteTextureView;

    // 4. LOD1, LOD2 자동 생성 및 캐싱
    FMeshResource& LOD1 = MeshResources[Scene::EncodeRenderMeshID(BaseMeshID, Scene::ELODLevel::LOD1)];
    FMeshResource& LOD2 = MeshResources[Scene::EncodeRenderMeshID(BaseMeshID, Scene::ELODLevel::LOD2)];

    LoadSimplifiedMeshResource(Device.Get(), WPath, LOD0, Scene::ELODLevel::LOD1, LOD1_SIMPLIFY_RATIO, LOD1);
    LoadSimplifiedMeshResource(Device.Get(), WPath, LOD0, Scene::ELODLevel::LOD2, LOD2_SIMPLIFY_RATIO, LOD2);

    if (!LOD1.DiffuseTextureView)
        LOD1.DiffuseTextureView = LOD0.DiffuseTextureView;
    if (!LOD2.DiffuseTextureView)
        LOD2.DiffuseTextureView = LOD0.DiffuseTextureView;

    // 5. 임포스터 베이킹
    BakeImpostor(BaseMeshID);

    MeshPathMap[FileName] = BaseMeshID;
    return BaseMeshID;
}

// ============================================================================
bool URenderer::CreateDefaultResources()
{
    // ── Bake용 임시 cbuffer (BakeImpostor 전용) ───────────────────────────────
    {
        D3D11_BUFFER_DESC bfd = {
            sizeof(FPerFrameConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        Device->CreateBuffer(&bfd, nullptr, &BakePerFrameBuffer);
        D3D11_BUFFER_DESC bod = {4096, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        Device->CreateBuffer(&bod, nullptr, &BakePerObjectBuffer);
    }

    // ── Main Shader ───────────────────────────────────────────────────────────
    const char* ShaderSrc = R"(
            cbuffer PerFrame : register(b0)
            {
                row_major float4x4 ViewProj;
            };

            cbuffer PerObject : register(b1)
            {
                float4 Row0;
                float4 Row1;
                float4 Row2;
                float4 Padding;
            };

            cbuffer MaterialData : register(b2)
            {
                float4 BaseColor;
            };

            Texture2D DiffuseTexture : register(t0);
            SamplerState DiffuseSampler : register(s0);

            struct VS_IN
            {
                float3 Pos      : POSITION;
                float3 Norm     : NORMAL;
                float2 TexCoord : TEXCOORD0;
            };

            struct PS_IN
            {
                float4 Pos          : SV_POSITION;
                float2 TexCoord     : TEXCOORD0;
            };

            PS_IN VSMain(VS_IN i)
            {
                PS_IN o;
                float4 LocalPos  = float4(i.Pos, 1.0f);
                float3 WorldPos  = float3(dot(LocalPos, Row0), dot(LocalPos, Row1), dot(LocalPos, Row2));
                o.Pos            = mul(float4(WorldPos, 1.0f), ViewProj);
                o.TexCoord       = i.TexCoord;
                return o;
            }

            float4 PSMain(PS_IN i) : SV_TARGET
            {
                return DiffuseTexture.Sample(DiffuseSampler, i.TexCoord) * BaseColor;
            }
        )";

    ComPtr<ID3DBlob> VS, PS, Err;
    if (FAILED(D3DCompile(ShaderSrc, std::strlen(ShaderSrc), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &VS,
                          &Err)))
        return false;
    if (FAILED(D3DCompile(ShaderSrc, std::strlen(ShaderSrc), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &PS,
                          &Err)))
        return false;
    if (FAILED(Device->CreateVertexShader(VS->GetBufferPointer(), VS->GetBufferSize(), nullptr, &VertexShader)))
        return false;
    if (FAILED(Device->CreatePixelShader(PS->GetBufferPointer(), PS->GetBufferSize(), nullptr, &PixelShader)))
        return false;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        // 0바이트부터 12바이트(float 3개)를 차지
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},

        // 12바이트 지점부터 Normal(float 3개) 시작
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},

        // 24바이트(12+12) 지점부터 TexCoord(float 2개) 시작
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    if (FAILED(Device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)), VS->GetBufferPointer(),
                                         VS->GetBufferSize(), &InputLayout)))
        return false;

    // ── Constant Buffers ──────────────────────────────────────────────────────
    {
        D3D11_BUFFER_DESC d = {
            sizeof(FPerFrameConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        if (FAILED(Device->CreateBuffer(&d, nullptr, &PerFrameBuffer)))
            return false;
    }
    {
        D3D11_BUFFER_DESC d = {
            64 * 1024 * 1024, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        if (FAILED(Device->CreateBuffer(&d, nullptr, &PerObjectBuffer)))
            return false;
    }
    {
        D3D11_BUFFER_DESC d = {
            sizeof(FMaterialConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        if (FAILED(Device->CreateBuffer(&d, nullptr, &MaterialBuffer)))
            return false;
    }

    // ── Sampler / Rasterizer / DepthStencil ──────────────────────────────────
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MinLOD = 0.0f;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(Device->CreateSamplerState(&sd, &DiffuseSamplerState)))
            return false;
    }
    {
        D3D11_RASTERIZER_DESC rd = {D3D11_FILL_SOLID, D3D11_CULL_BACK, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE};
        if (FAILED(Device->CreateRasterizerState(&rd, &DefaultRasterizerState)))
            return false;
    }
    {
        D3D11_DEPTH_STENCIL_DESC dd = {TRUE, D3D11_DEPTH_WRITE_MASK_ALL, D3D11_COMPARISON_LESS_EQUAL, FALSE, 0, 0, {},
                                       {}};
        if (FAILED(Device->CreateDepthStencilState(&dd, &DefaultDepthStencilState)))
            return false;
    }

    // ── Default White Texture ─────────────────────────────────────────────────
    if (!CreateSolidTexture(Device.Get(), {1.0f, 1.0f, 1.0f, 1.0f}, DefaultWhiteTextureView))
        return false;

    // ── Mesh Resources ────────────────────────────────────────────────────────
    NextBaseMeshID = 0;

    // ── Billboard Shader ──────────────────────────────────────────────────────
    const char* BBShader = R"(
            cbuffer PerFrame : register(b0) { row_major float4x4 VP; float4 CR; float4 CU; float4 CP; };
            cbuffer PerObject : register(b1) { float4 R0; float4 R1; float4 R2; float4 Padding; };
            Texture2D SN : register(t0);
            SamplerState SS : register(s0);

            struct VI { float3 P : POSITION; float2 T : TEXCOORD0; };
            struct PI { float4 P : SV_POSITION; float2 T : TEXCOORD0; float3 WorldCenter : POSITION1; };

            PI VSMain(VI i)
            {
                PI o;
                float3 localCenter = Padding.yzw;
                o.WorldCenter = float3(
                    dot(float4(localCenter, 1.0f), R0),
                    dot(float4(localCenter, 1.0f), R1),
                    dot(float4(localCenter, 1.0f), R2)
                );

                float sx = length(float3(R0.x, R1.x, R2.x));
                float sy = length(float3(R0.y, R1.y, R2.y));
                
                float3 fp = o.WorldCenter + (i.P.x * sx * 2.5f * CR.xyz) + (i.P.z * sy * 2.5f * CU.xyz);
                
                o.P = mul(float4(fp, 1.0f), VP);
                o.T = i.T;

                return o;
            }

            float4 PSMain(PI i) : SV_TARGET
            {
                float3 X = normalize(float3(R0.x, R1.x, R2.x));
                float3 Y = normalize(float3(R0.y, R1.y, R2.y));
                float3 Z = normalize(float3(R0.z, R1.z, R2.z));
                
                float3 V = normalize(CP.xyz - i.WorldCenter);

                float dots[6];
                dots[0] = dot(V, X);  // +X (Front)
                dots[1] = -dot(V, Y); // -Y (Left)
                dots[2] = -dot(V, X); // -X (Back)
                dots[3] = dot(V, Y);  // +Y (Right)
                dots[4] = dot(V, Z);  // +Z (Top)
                dots[5] = -dot(V, Z); // -Z (Bottom)

                int f1 = 0, f2 = 0;
                float maxDot1 = -2.0f, maxDot2 = -2.0f;

                [unroll]
                for (int j = 0; j < 6; ++j)
                {
                    if (dots[j] > maxDot1)
                    {
                        maxDot2 = maxDot1; f2 = f1;
                        maxDot1 = dots[j]; f1 = j;
                    }
                    else if (dots[j] > maxDot2)
                    {
                        maxDot2 = dots[j]; f2 = j;
                    }
                }

                float2 uv1 = (i.T * float2(0.25f, 0.5f)) + float2(f1 % 4, f1 / 4) * float2(0.25f, 0.5f);
                float2 uv2 = (i.T * float2(0.25f, 0.5f)) + float2(f2 % 4, f2 / 4) * float2(0.25f, 0.5f);

                float4 c1 = SN.Sample(SS, uv1); // 1등 면 (Best)
                float4 c2 = SN.Sample(SS, uv2); // 2등 면

                // [수정] 블렌딩 수학 완벽 교체: 두 면의 차이가 0일때 정확히 50/50 섞이도록 수정
                float diff = maxDot1 - maxDot2;
                float weight = saturate(0.5f + (diff / 0.2f)); 
                float4 finalColor = lerp(c2, c1, weight);

                if (finalColor.a < 0.1f) discard;

                return finalColor;
            }
        )";

    D3DCompile(BBShader, strlen(BBShader), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &VS, &Err);
    D3DCompile(BBShader, strlen(BBShader), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &PS, &Err);
    Device->CreateVertexShader(VS->GetBufferPointer(), VS->GetBufferSize(), nullptr, &BillboardVS);
    Device->CreatePixelShader(PS->GetBufferPointer(), PS->GetBufferSize(), nullptr, &BillboardPS);

    D3D11_INPUT_ELEMENT_DESC bbl[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    Device->CreateInputLayout(bbl, 2, VS->GetBufferPointer(), VS->GetBufferSize(), &BillboardLayout);

    FBillboardVertex bbv[] = {
        {{-0.5f, 0, 0.5f}, {0, 0}},
        {{0.5f, 0, 0.5f}, {1, 0}},
        {{-0.5f, 0, -0.5f}, {0, 1}},
        {{0.5f, 0, -0.5f}, {1, 1}},
    };
    {
        D3D11_BUFFER_DESC bvb = {sizeof(bbv), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA bvd = {bbv, 0, 0};
        Device->CreateBuffer(&bvb, &bvd, &BillboardVB);
    }
    uint32_t bbi[] = {0, 1, 2, 2, 1, 3};
    {
        D3D11_BUFFER_DESC bib = {sizeof(bbi), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA bid = {bbi, 0, 0};
        Device->CreateBuffer(&bib, &bid, &BillboardIB);
    }

    // ── Hi-Z Build CS ─────────────────────────────────────────────────────────
    const char* HiZBuildSrc = R"(
            Texture2D<float>   SrcDepth : register(t0);
            RWTexture2D<float> DstMip   : register(u0);

            cbuffer MipParams : register(b0)
            {
                uint SrcW;
                uint SrcH;
                uint2 _pad;
            };

            [numthreads(8, 8, 1)]
            void CSBuildHiZ(uint3 DTid : SV_DispatchThreadID)
            {
                uint2 dst = DTid.xy;
                uint2 src = dst * 2;

                float d0 = SrcDepth.Load(int3(src + uint2(0, 0), 0));
                float d1 = SrcDepth.Load(int3(src + uint2(1, 0), 0));
                float d2 = SrcDepth.Load(int3(src + uint2(0, 1), 0));
                float d3 = SrcDepth.Load(int3(src + uint2(1, 1), 0));

                if (src.x + 1 >= SrcW) { d1 = d0; d3 = d2; }
                if (src.y + 1 >= SrcH) { d2 = d0; d3 = d1; }

                DstMip[dst] = max(max(d0, d1), max(d2, d3));
            }
        )";

    ComPtr<ID3DBlob> HiZBuildBlob, HiZBuildErr;
    if (FAILED(D3DCompile(HiZBuildSrc, strlen(HiZBuildSrc), "HiZBuild", nullptr, nullptr, "CSBuildHiZ", "cs_5_0", 0, 0,
                          &HiZBuildBlob, &HiZBuildErr)))
    {
        if (HiZBuildErr)
            OutputDebugStringA((char*)HiZBuildErr->GetBufferPointer());
        return false;
    }
    if (FAILED(Device->CreateComputeShader(HiZBuildBlob->GetBufferPointer(), HiZBuildBlob->GetBufferSize(), nullptr,
                                           &CSBuildHiZ)))
        return false;

    // ── Occlusion Test CS ─────────────────────────────────────────────────────
    const char* HiZCullSrc = R"(
            struct FObjectBounds
            {
                float3 BoundsMin;
                uint   ObjectIndex;
                float3 BoundsMax;
                uint   _pad;
            };

            cbuffer CullParams : register(b0)
            {
                row_major float4x4 ViewProj;
                uint  ObjectCount;
                uint  HiZMipLevels;
                float HiZTexelWidth;
                float HiZTexelHeight;
            };

            StructuredBuffer<FObjectBounds> InBounds        : register(t0);
            Texture2D<float>                HiZTexture      : register(t1);
            SamplerState                    PointClampSamp  : register(s0);
            RWStructuredBuffer<uint>        VisibilityFlags : register(u0);

            [numthreads(64, 1, 1)]
            void CSTestOcclusion(uint3 DTid : SV_DispatchThreadID)
            {
                uint idx = DTid.x;
                if (idx >= ObjectCount) return;

                FObjectBounds b = InBounds[idx];

                float2 ndcMin =  1.0f;
                float2 ndcMax = -1.0f;
                float  minZ   =  1.0f;

                [unroll]
                for (uint i = 0; i < 8; ++i)
                {
                    float3 corner = float3(
                        (i & 1) ? b.BoundsMax.x : b.BoundsMin.x,
                        (i & 2) ? b.BoundsMax.y : b.BoundsMin.y,
                        (i & 4) ? b.BoundsMax.z : b.BoundsMin.z
                    );
                    float4 clip = mul(float4(corner, 1.0f), ViewProj);

                    if (clip.w < 1e-5f)
                    {
                        VisibilityFlags[b.ObjectIndex] = 1;
                        return;
                    }

                    float3 ndc = clip.xyz / clip.w;
                    ndcMin = min(ndcMin, ndc.xy);
                    ndcMax = max(ndcMax, ndc.xy);
                    minZ   = min(minZ,   ndc.z);
                }

                if (any(ndcMax < -1.0f) || any(ndcMin > 1.0f))
                {
                    VisibilityFlags[b.ObjectIndex] = 0;
                    return;
                }

                ndcMin = clamp(ndcMin, -1.0f, 1.0f);
                ndcMax = clamp(ndcMax, -1.0f, 1.0f);

                float2 uvMin = float2( ndcMin.x * 0.5f + 0.5f, -ndcMax.y * 0.5f + 0.5f);
                float2 uvMax = float2( ndcMax.x * 0.5f + 0.5f, -ndcMin.y * 0.5f + 0.5f);

                float2 sizeUV = uvMax - uvMin;
                float  sizeX  = sizeUV.x * (float)HiZTexelWidth;
                float  sizeY  = sizeUV.y * (float)HiZTexelHeight;
                float  texels = max(sizeX, sizeY);
                uint   mip    = (uint)clamp(floor(log2(texels)), 0.0f, (float)HiZMipLevels);

                float d0 = HiZTexture.SampleLevel(PointClampSamp, float2(uvMin.x, uvMin.y), mip);
                float d1 = HiZTexture.SampleLevel(PointClampSamp, float2(uvMax.x, uvMin.y), mip);
                float d2 = HiZTexture.SampleLevel(PointClampSamp, float2(uvMin.x, uvMax.y), mip);
                float d3 = HiZTexture.SampleLevel(PointClampSamp, float2(uvMax.x, uvMax.y), mip);
                float occluderDepth = max(max(d0, d1), max(d2, d3));

                VisibilityFlags[b.ObjectIndex] = (minZ > occluderDepth) ? 0u : 1u;
            }
        )";

    ComPtr<ID3DBlob> HiZCullBlob, HiZCullErr;
    if (FAILED(D3DCompile(HiZCullSrc, strlen(HiZCullSrc), "HiZCull", nullptr, nullptr, "CSTestOcclusion", "cs_5_0", 0,
                          0, &HiZCullBlob, &HiZCullErr)))
    {
        if (HiZCullErr)
            OutputDebugStringA((char*)HiZCullErr->GetBufferPointer());
        return false;
    }
    if (FAILED(Device->CreateComputeShader(HiZCullBlob->GetBufferPointer(), HiZCullBlob->GetBufferSize(), nullptr,
                                           &CSTestOcclusion)))
        return false;

    return true;
}

// ============================================================================
void URenderer::BakeImpostor(uint32_t MeshID)
{
    const FMeshResource& res = MeshResources[MeshID];
    if (res.SourceVertices.empty())
        return;

    // ── Atlas RTV / DSV ───────────────────────────────────────────────────────
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    {
        D3D11_TEXTURE2D_DESC td = {1024,
                                   512,
                                   1,
                                   1,
                                   DXGI_FORMAT_R8G8B8A8_UNORM,
                                   {1, 0},
                                   D3D11_USAGE_DEFAULT,
                                   D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                                   0,
                                   0};
        Device->CreateTexture2D(&td, nullptr, &tex);
        Device->CreateRenderTargetView(tex.Get(), nullptr, &rtv);
        Device->CreateShaderResourceView(tex.Get(), nullptr, &srv);
    }
    ComPtr<ID3D11Texture2D> dtx;
    ComPtr<ID3D11DepthStencilView> dsv;
    {
        D3D11_TEXTURE2D_DESC dd = {
            1024, 512, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL,
            0,    0};
        Device->CreateTexture2D(&dd, nullptr, &dtx);
        Device->CreateDepthStencilView(dtx.Get(), nullptr, &dsv);
    }

    float clr[4] = {0, 0, 0, 0};
    Context->ClearRenderTargetView(rtv.Get(), clr);
    Context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    Context->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());

    // NoCull 래스터라이저
    ComPtr<ID3D11RasterizerState> NoCull;
    {
        D3D11_RASTERIZER_DESC rd = {D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE};
        Device->CreateRasterizerState(&rd, &NoCull);
    }
    Context->RSSetState(NoCull.Get());
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // PerFrame (ortho from +X)
    {
        DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH({3, 0, 0}, {0, 0, 0}, {0, 0, 1});
        DirectX::XMMATRIX proj = DirectX::XMMatrixOrthographicLH(2.5f, 2.5f, 0.1f, 10.0f);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(Context->Map(BakePerFrameBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            FPerFrameConstants pf = {};
            DirectX::XMStoreFloat4x4(&pf.ViewProj, view * proj);
            memcpy(m.pData, &pf, sizeof(pf));
            Context->Unmap(BakePerFrameBuffer.Get(), 0);
        }
    }

    // Material
    {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(Context->Map(MaterialBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            FMaterialConstants mc = {{1, 1, 1, 1}};
            memcpy(m.pData, &mc, sizeof(mc));
            Context->Unmap(MaterialBuffer.Get(), 0);
        }
    }

    Context->IASetInputLayout(InputLayout.Get());
    Context->VSSetShader(VertexShader.Get(), nullptr, 0);
    Context->PSSetShader(PixelShader.Get(), nullptr, 0);
    Context->VSSetConstantBuffers(0, 1, BakePerFrameBuffer.GetAddressOf());
    Context->PSSetConstantBuffers(2, 1, MaterialBuffer.GetAddressOf());
    Context->PSSetSamplers(0, 1, DiffuseSamplerState.GetAddressOf());
    {
        ID3D11ShaderResourceView* dsrv = res.DiffuseTextureView.Get();
        Context->PSSetShaderResources(0, 1, &dsrv);
    }
    UINT s = sizeof(FMeshVertex), o = 0;
    Context->IASetVertexBuffers(0, 1, res.VertexBuffer.GetAddressOf(), &s, &o);
    Context->IASetIndexBuffer(res.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

    // 6-face bake (4x2 atlas)
    for (int f = 0; f < 6; ++f)
    {
        D3D11_VIEWPORT vp = {(float)((f % 4) * 256), (float)((f / 4) * 256), 256.0f, 256.0f, 0, 1};
        Context->RSSetViewports(1, &vp);

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(Context->Map(BakePerObjectBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            DirectX::XMMATRIX rotMat;
            switch (f)
            {
            case 0:
                rotMat = DirectX::XMMatrixIdentity();
                break; // +X (Front)
            case 1:
                rotMat = DirectX::XMMatrixRotationZ(DirectX::XM_PIDIV2);
                break; // -Y (Left)
            case 2:
                rotMat = DirectX::XMMatrixRotationZ(DirectX::XM_PI);
                break; // -X (Back)
            case 3:
                rotMat = DirectX::XMMatrixRotationZ(-DirectX::XM_PIDIV2);
                break; // +Y (Right)
            // [수정] Z-up 환경에 맞춰 Top/Bottom 부호 반전
            case 4:
                rotMat = DirectX::XMMatrixRotationY(-DirectX::XM_PIDIV2);
                break; // +Z (Top)
            default:
                rotMat = DirectX::XMMatrixRotationY(DirectX::XM_PIDIV2);
                break; // -Z (Bottom)
            }

            DirectX::XMMATRIX objMat =
                DirectX::XMMatrixTranslation(-res.LocalCenter.x, -res.LocalCenter.y, -res.LocalCenter.z) * rotMat;

            FPerObjectConstants po = {};
            DirectX::XMMATRIX sm = DirectX::XMMatrixTranspose(objMat);
            DirectX::XMStoreFloat4(&po.Row0, sm.r[0]);
            DirectX::XMStoreFloat4(&po.Row1, sm.r[1]);
            DirectX::XMStoreFloat4(&po.Row2, sm.r[2]);
            po.Padding = {0, 0, 0, 1};
            std::memcpy(m.pData, &po, sizeof(po));
            Context->Unmap(BakePerObjectBuffer.Get(), 0);
        }
        Context->VSSetConstantBuffers(1, 1, BakePerObjectBuffer.GetAddressOf());
        Context->DrawIndexed(res.IndexCount, 0, 0);
    }

    ImpostorResources[MeshID].SnapshotTexture = tex;
    ImpostorResources[MeshID].SnapshotSRV = srv;
    ImpostorResources[MeshID].bIsBaked = true;

    ID3D11RenderTargetView* nrt = nullptr;
    Context->OMSetRenderTargets(1, &nrt, nullptr);
}

// ============================================================================
void URenderer::InitHiZResources(uint32_t Width, uint32_t Height)
{
    HiZWidth = Width;
    HiZHeight = Height;
    HiZMipCount = 1;
    uint32_t sz = std::max<uint32_t>(Width, Height);
    while (sz > 1)
    {
        sz >>= 1;
        ++HiZMipCount;
    }

    // Hi-Z Texture
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = Width;
        td.Height = Height;
        td.MipLevels = HiZMipCount;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32_FLOAT;
        td.SampleDesc = {1, 0};
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        Device->CreateTexture2D(&td, nullptr, &HiZTexture);
    }

    // 전체 SRV
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format = DXGI_FORMAT_R32_FLOAT;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MostDetailedMip = 0;
        srvd.Texture2D.MipLevels = HiZMipCount;
        Device->CreateShaderResourceView(HiZTexture.Get(), &srvd, &HiZSRV);
    }

    // mip별 UAV & SRV
    HiZMipUAVs.resize(HiZMipCount);
    HiZMipSRVs.resize(HiZMipCount);
    for (uint32_t m = 0; m < HiZMipCount; ++m)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format = DXGI_FORMAT_R32_FLOAT;
        uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavd.Texture2D.MipSlice = m;
        Device->CreateUnorderedAccessView(HiZTexture.Get(), &uavd, &HiZMipUAVs[m]);

        D3D11_SHADER_RESOURCE_VIEW_DESC msrvd = {};
        msrvd.Format = DXGI_FORMAT_R32_FLOAT;
        msrvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        msrvd.Texture2D.MostDetailedMip = m;
        msrvd.Texture2D.MipLevels = 1;
        Device->CreateShaderResourceView(HiZTexture.Get(), &msrvd, &HiZMipSRVs[m]);
    }

    // Bounds StructuredBuffer
    {
        constexpr uint32_t MaxObj = Scene::FSceneDataSOA::MAX_OBJECTS;

        D3D11_BUFFER_DESC bbd = {};
        bbd.ByteWidth = sizeof(FObjectBoundsGPU) * MaxObj;
        bbd.Usage = D3D11_USAGE_DYNAMIC;
        bbd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bbd.StructureByteStride = sizeof(FObjectBoundsGPU);
        Device->CreateBuffer(&bbd, nullptr, &BoundsBuffer);

        D3D11_SHADER_RESOURCE_VIEW_DESC bsrvd = {};
        bsrvd.Format = DXGI_FORMAT_UNKNOWN;
        bsrvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        bsrvd.Buffer.FirstElement = 0;
        bsrvd.Buffer.NumElements = MaxObj;
        Device->CreateShaderResourceView(BoundsBuffer.Get(), &bsrvd, &BoundsSRV);
    }

    // Visibility RWStructuredBuffer
    {
        constexpr uint32_t MaxObj = Scene::FSceneDataSOA::MAX_OBJECTS;

        D3D11_BUFFER_DESC vbd = {};
        vbd.ByteWidth = sizeof(uint32_t) * MaxObj;
        vbd.Usage = D3D11_USAGE_DEFAULT;
        vbd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        vbd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        vbd.StructureByteStride = sizeof(uint32_t);
        Device->CreateBuffer(&vbd, nullptr, &VisibilityBuffer);

        D3D11_UNORDERED_ACCESS_VIEW_DESC vuavd = {};
        vuavd.Format = DXGI_FORMAT_UNKNOWN;
        vuavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        vuavd.Buffer.FirstElement = 0;
        vuavd.Buffer.NumElements = MaxObj;
        Device->CreateUnorderedAccessView(VisibilityBuffer.Get(), &vuavd, &VisibilityUAV);

        // CPU Readback Staging (더블 버퍼)
        D3D11_BUFFER_DESC sbd = vbd;
        sbd.Usage = D3D11_USAGE_STAGING;
        sbd.BindFlags = 0;
        sbd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sbd.MiscFlags = 0;
        Device->CreateBuffer(&sbd, nullptr, &VisibilityStagingBuffers[0]);
        Device->CreateBuffer(&sbd, nullptr, &VisibilityStagingBuffers[1]);
    }

    // CullParam / HiZBuildParam cbuffer
    {
        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth = 256;
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Device->CreateBuffer(&cbd, nullptr, &CullParamBuffer);
        Device->CreateBuffer(&cbd, nullptr, &HiZBuildParamBuffer);
    }

    // PointClamp Sampler
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        Device->CreateSamplerState(&sd, &PointClampSamplerState);
    }
}

// ============================================================================
void URenderer::BuildHiZMips()
{
    // DSV 해제 (같은 텍스처를 SRV로 읽기 위해)
    ID3D11RenderTargetView* nullRTV = nullptr;
    Context->OMSetRenderTargets(0, &nullRTV, nullptr);

    // Depth → HiZTexture mip0 복사
    {
        ID3D11Resource* depthRes = nullptr;
        DepthCopySRV->GetResource(&depthRes);
        Context->CopySubresourceRegion(HiZTexture.Get(), 0, 0, 0, 0, depthRes, 0, nullptr);
        depthRes->Release();
    }

    Context->CSSetShader(CSBuildHiZ.Get(), nullptr, 0);

    for (uint32_t m = 1; m < HiZMipCount; ++m)
    {
        const uint32_t SrcW = std::max<uint32_t>(1u, HiZWidth >> (m - 1));
        const uint32_t SrcH = std::max<uint32_t>(1u, HiZHeight >> (m - 1));
        const uint32_t DstW = std::max<uint32_t>(1u, HiZWidth >> m);
        const uint32_t DstH = std::max<uint32_t>(1u, HiZHeight >> m);

        {
            struct FMipParams
            {
                uint32_t SrcW, SrcH, _pad[2];
            };
            D3D11_MAPPED_SUBRESOURCE mr = {};
            if (SUCCEEDED(Context->Map(HiZBuildParamBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mr)))
            {
                FMipParams p = {SrcW, SrcH, {0, 0}};
                memcpy(mr.pData, &p, sizeof(p));
                Context->Unmap(HiZBuildParamBuffer.Get(), 0);
            }
        }

        // m==1: DepthCopySRV에서 직접 읽기 (mip0 SRV 충돌 방지)
        ID3D11ShaderResourceView* srcSRV = (m == 1) ? DepthCopySRV.Get() : HiZMipSRVs[m - 1].Get();

        Context->CSSetConstantBuffers(0, 1, HiZBuildParamBuffer.GetAddressOf());
        Context->CSSetShaderResources(0, 1, &srcSRV);
        Context->CSSetUnorderedAccessViews(0, 1, HiZMipUAVs[m].GetAddressOf(), nullptr);
        Context->Dispatch((DstW + 7) / 8, (DstH + 7) / 8, 1);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        Context->CSSetShaderResources(0, 1, &nullSRV);
        Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    }

    Context->CSSetShader(nullptr, nullptr, 0);
}

// ============================================================================
void URenderer::RunOcclusionCull(Scene::FSceneDataSOA* SceneData, const DirectX::XMMATRIX& ViewProj,
                                 std::array<float, Scene::FSceneDataSOA::MAX_OBJECTS>& InOutConfidence, float DeltaTime)
{
    // 카메라 각속도 계산 (이전 프레임과 yaw/pitch 차이)
    const float YawDelta = std::abs(CameraState.YawRadians - PrevCameraYaw);
    const float PitchDelta = std::abs(CameraState.PitchRadians - PrevCameraYaw);
    const float AngularSpeed = (YawDelta + PitchDelta) / DeltaTime; // rad/s

    PrevCameraYaw = CameraState.YawRadians;
    PrevCameraPitch = CameraState.PitchRadians;

    // 각속도에 따라 FALL 범위 조정
    // 정지(0 rad/s) → 1초 후 cull
    // 고속(3 rad/s 이상) → 0.1초 후 cull
    constexpr float SLOW_DELAY = 0.1f; // 초
    constexpr float FAST_DELAY = 0.1f; // 초
    constexpr float MAX_SPEED = 3.0f;  // rad/s 기준

    const float T = std::clamp(AngularSpeed / MAX_SPEED, 0.0f, 1.0f);
    const float Delay = std::lerp(SLOW_DELAY, FAST_DELAY, T);
    const float Fall = DeltaTime / Delay * 0.1;

    const uint32_t Count = SceneData->RenderCount;
    if (Count == 0)
        return;

    // 1. Bounds 버퍼 업데이트
    {
        D3D11_MAPPED_SUBRESOURCE mr = {};
        if (FAILED(Context->Map(BoundsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mr)))
            return;

        auto* dst = static_cast<FObjectBoundsGPU*>(mr.pData);
        for (uint32_t i = 0; i < Count; ++i)
        {
            const uint32_t oid = SceneData->RenderQueue[i];
            DirectX::XMFLOAT3 center = {(SceneData->MinX[oid] + SceneData->MaxX[oid]) * 0.5f,
                                        (SceneData->MinY[oid] + SceneData->MaxY[oid]) * 0.5f,
                                        (SceneData->MinZ[oid] + SceneData->MaxZ[oid]) * 0.5f};

            DirectX::XMFLOAT3 extent = {(SceneData->MaxX[oid] - SceneData->MinX[oid]) * 0.5f,
                                        (SceneData->MaxY[oid] - SceneData->MinY[oid]) * 0.5f,
                                        (SceneData->MaxZ[oid] - SceneData->MinZ[oid]) * 0.5f};

            const float scale = 1.1f; // 5~10% 권장

            extent.x *= scale;
            extent.y *= scale;
            extent.z *= scale;

            dst[i].BoundsMin = {center.x - extent.x, center.y - extent.y, center.z - extent.z};

            dst[i].BoundsMax = {center.x + extent.x, center.y + extent.y, center.z + extent.z};
            dst[i].ObjectIndex = oid;
            dst[i]._pad = 0;
        }
        Context->Unmap(BoundsBuffer.Get(), 0);
    }

    // 2. CullParams 업데이트
    {
        struct alignas(16) FCullParams
        {
            DirectX::XMFLOAT4X4 ViewProj;
            uint32_t ObjectCount;
            uint32_t HiZMipLevels;
            float HiZTexelWidth;
            float HiZTexelHeight;
        };

        D3D11_MAPPED_SUBRESOURCE mr = {};
        if (SUCCEEDED(Context->Map(CullParamBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mr)))
        {
            auto* p = static_cast<FCullParams*>(mr.pData);
            DirectX::XMStoreFloat4x4(&p->ViewProj, ViewProj);
            p->ObjectCount = Count;
            p->HiZMipLevels = HiZMipCount - 1;
            p->HiZTexelWidth = static_cast<float>(HiZWidth);
            p->HiZTexelHeight = static_cast<float>(HiZHeight);
            Context->Unmap(CullParamBuffer.Get(), 0);
        }
    }

    // 3. Compute Shader 실행
    Context->CSSetShader(CSTestOcclusion.Get(), nullptr, 0);
    Context->CSSetConstantBuffers(0, 1, CullParamBuffer.GetAddressOf());
    Context->CSSetShaderResources(0, 1, BoundsSRV.GetAddressOf());
    Context->CSSetShaderResources(1, 1, HiZSRV.GetAddressOf());
    Context->CSSetSamplers(0, 1, PointClampSamplerState.GetAddressOf());
    UINT clearValue[4] = {0, 0, 0, 0};
    Context->ClearUnorderedAccessViewUint(VisibilityUAV.Get(), clearValue);
    Context->CSSetUnorderedAccessViews(0, 1, VisibilityUAV.GetAddressOf(), nullptr);
    Context->Dispatch((Count + 63) / 64, 1, 1);

    // 바인딩 해제
    {
        ID3D11ShaderResourceView* nullSRV[2] = {nullptr, nullptr};
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        Context->CSSetShaderResources(0, 2, nullSRV);
        Context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        Context->CSSetShader(nullptr, nullptr, 0);
    }

    // 4. GPU → CPU Readback (더블 버퍼링)
    Context->CopyResource(VisibilityStagingBuffers[StagingWriteIndex].Get(), VisibilityBuffer.Get());

    // 첫 프레임: 전부 Visible
    if (bFirstFrame)
    {
        bFirstFrame = false;
        for (uint32_t i = 0; i < Count; ++i)
            InOutConfidence[SceneData->RenderQueue[i]] = 1.0f;
        std::swap(StagingReadIndex, StagingWriteIndex);
        return;
    }

    if (WarmupFramesRemaining > 0)
    {
        --WarmupFramesRemaining;
        for (uint32_t i = 0; i < Count; ++i)
            InOutConfidence[SceneData->RenderQueue[i]] = 1.0f;
        std::swap(StagingReadIndex, StagingWriteIndex);
        return;
    }

    // 5. 이전 프레임 결과 읽기 (GPU stall 없음)
    D3D11_MAPPED_SUBRESOURCE mr = {};
    if (FAILED(Context->Map(VisibilityStagingBuffers[StagingReadIndex].Get(), 0, D3D11_MAP_READ, 0, &mr)))
    {
        std::swap(StagingReadIndex, StagingWriteIndex);
        return;
    }

    const uint32_t* flags = static_cast<const uint32_t*>(mr.pData);
    for (uint32_t i = 0; i < Count; ++i)
    {
        const uint32_t oid = SceneData->RenderQueue[i];
        if (flags[oid] != 0)
            InOutConfidence[oid] = 1.0f; // visible → 즉시 최대
        else
            InOutConfidence[oid] = std::max<float>(0.0f, InOutConfidence[oid] - Fall); // invisible → 천천히 감소
    }

    Context->Unmap(VisibilityStagingBuffers[StagingReadIndex].Get(), 0);
    std::swap(StagingReadIndex, StagingWriteIndex);
}

// ============================================================================
void URenderer::BeginFrame()
{
    const float Color[4] = {0.03f, 0.03f, 0.06f, 1.0f};
    Context->OMSetRenderTargets(1, MainRenderTargetView.GetAddressOf(), nullptr);
    Context->ClearRenderTargetView(MainRenderTargetView.Get(), Color);
    D3D11_VIEWPORT Viewport = {0.0f, 0.0f, static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight),
                               0.0f, 1.0f};
    Context->RSSetViewports(1, &Viewport);
}

// ============================================================================
void URenderer::RenderScene(const Scene::USceneManager& InSceneManager, float DeltaTime)
{
    uint32_t DrawCount = 0;
    Scene::FSceneDataSOA* SceneData = const_cast<Scene::FSceneDataSOA*>(InSceneManager.GetSceneData());
    if (!SceneData || !PerFrameBuffer || !PerObjectBuffer || !MaterialBuffer)
        return;

    constexpr uint32_t AlignedConstantSize = 256;
    constexpr uint32_t BufferCapacity = 64 * 1024 * 1024;

    // ── ViewProj 계산 ─────────────────────────────────────────────────────────
    const uint32_t ActiveViewportWidth = (std::max)(SceneViewportWidth, 1u);
    const uint32_t ActiveViewportHeight = (std::max)(SceneViewportHeight, 1u);
    if (!EnsureSceneViewportResources(ActiveViewportWidth, ActiveViewportHeight))
        return;

    ID3D11ShaderResourceView* NullShaderResources[1] = {nullptr};
    Context->PSSetShaderResources(0, 1, NullShaderResources);

    const float aspect = static_cast<float>(ActiveViewportWidth) / static_cast<float>(ActiveViewportHeight);

    DirectX::XMVECTOR camPos = DirectX::XMLoadFloat3(&CameraState.Position);
    DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(
        DirectX::XMVectorSet(std::cos(CameraState.PitchRadians) * std::cos(CameraState.YawRadians),
                             std::cos(CameraState.PitchRadians) * std::sin(CameraState.YawRadians),
                             std::sin(CameraState.PitchRadians), 0.0f));

    const DirectX::XMMATRIX view =
        DirectX::XMMatrixLookAtLH(camPos, DirectX::XMVectorAdd(camPos, forward), DirectX::XMVectorSet(0, 0, 1, 0));
    const DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(CameraState.FOVDegrees), aspect, CameraState.NearClip, CameraState.FarClip);
    const DirectX::XMMATRIX viewProj = view * proj;

    // PerFrameBuffer 업데이트
    {
        D3D11_MAPPED_SUBRESOURCE pfmap = {};
        if (SUCCEEDED(Context->Map(PerFrameBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &pfmap)))
        {
            FPerFrameConstants pf = {};
            DirectX::XMStoreFloat4x4(&pf.ViewProj, viewProj);
            DirectX::XMStoreFloat4(&pf.CameraRight, DirectX::XMMatrixTranspose(view).r[0]);
            DirectX::XMStoreFloat4(&pf.CameraUp, DirectX::XMMatrixTranspose(view).r[1]);
            DirectX::XMStoreFloat4(&pf.CameraPos, camPos);
            memcpy(pfmap.pData, &pf, sizeof(pf));
            Context->Unmap(PerFrameBuffer.Get(), 0);
        }
    }

    // Material
    {
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(Context->Map(MaterialBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            FMaterialConstants mc = {{1, 1, 1, 1}};
            memcpy(m.pData, &mc, sizeof(mc));
            Context->Unmap(MaterialBuffer.Get(), 0);
        }
    }

    const uint32_t TotalCount = SceneData->RenderCount;

    auto t0 = std::chrono::high_resolution_clock::now();

    D3D11_VIEWPORT SceneViewport = {
        0.0f, 0.0f, static_cast<float>(ActiveViewportWidth), static_cast<float>(ActiveViewportHeight), 0.0f, 1.0f};
    Context->RSSetViewports(1, &SceneViewport);

    static uint32_t PrevVisibleQueue[Scene::FSceneDataSOA::MAX_OBJECTS];
    static uint32_t PrevInvisibleQueue[Scene::FSceneDataSOA::MAX_OBJECTS];
    uint32_t PrevVisibleCount = 0;
    uint32_t PrevInvisibleCount = 0;

    for (uint32_t i = 0; i < TotalCount; ++i)
    {
        const uint32_t oid = SceneData->RenderQueue[i];
        if (!bHasPrevFrame || VisibilityConfidence[oid] > 0.0f)
            PrevVisibleQueue[PrevVisibleCount++] = oid;
        else
            PrevInvisibleQueue[PrevInvisibleCount++] = oid;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    CurrentMetrics.SplitTime = std::chrono::duration<float, std::milli>(t1 - t0).count();

    // ── Pass 1: PrevVisible Depth Prepass ────────────────────────────────────
    {
        Context->ClearDepthStencilView(DepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        ID3D11RenderTargetView* nullRTV = nullptr;
        Context->OMSetRenderTargets(0, &nullRTV, DepthStencilView.Get());
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->IASetInputLayout(InputLayout.Get());
        Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Context->PSSetShader(nullptr, nullptr, 0);
        Context->VSSetConstantBuffers(0, 1, PerFrameBuffer.GetAddressOf());
        Context->RSSetState(DefaultRasterizerState.Get());
        Context->OMSetDepthStencilState(DefaultDepthStencilState.Get(), 0);

        const uint32_t Bulk = PrevVisibleCount * AlignedConstantSize;
        D3D11_MAP MapType = D3D11_MAP_WRITE_NO_OVERWRITE;
        if (PerObjectRingBufferOffset + Bulk > BufferCapacity)
        {
            MapType = D3D11_MAP_WRITE_DISCARD;
            PerObjectRingBufferOffset = 0;
        }

        D3D11_MAPPED_SUBRESOURCE pm = {};
        if (FAILED(Context->Map(PerObjectBuffer.Get(), 0, MapType, 0, &pm)))
            return;

        uint8_t* base = static_cast<uint8_t*>(pm.pData) + PerObjectRingBufferOffset;
        for (uint32_t i = 0; i < PrevVisibleCount; ++i)
        {
            const uint32_t oid = PrevVisibleQueue[i];
            const Math::FPacked3x4Matrix& mat = SceneData->WorldMatrices[oid];
            FPerObjectConstants* dest = reinterpret_cast<FPerObjectConstants*>(base + i * AlignedConstantSize);
            DirectX::XMStoreFloat4(&dest->Row0, mat.Row0);
            DirectX::XMStoreFloat4(&dest->Row1, mat.Row1);
            DirectX::XMStoreFloat4(&dest->Row2, mat.Row2);
            dest->Padding = {0, 0, 0, 0};
        }
        Context->Unmap(PerObjectBuffer.Get(), 0);

        const uint32_t Pass1Base = PerObjectRingBufferOffset;
        PerObjectRingBufferOffset += Bulk;

        for (uint32_t i = 0; i < PrevVisibleCount; ++i)
        {
            const uint32_t oid = PrevVisibleQueue[i];
            const uint32_t mid = SceneData->MeshIDs[oid];
            if (mid >= TOTAL_MESH_RESOURCE_COUNT)
                continue;
            const FMeshResource& res = MeshResources[mid];
            if (!res.VertexBuffer || !res.IndexBuffer)
                continue;

            if (Context1)
            {
                UINT off = (Pass1Base + i * AlignedConstantSize) / 16;
                UINT cnt = AlignedConstantSize / 16;
                Context1->VSSetConstantBuffers1(1, 1, PerObjectBuffer.GetAddressOf(), &off, &cnt);
            }
            UINT stride = sizeof(FMeshVertex), offset = 0;
            Context->IASetVertexBuffers(0, 1, res.VertexBuffer.GetAddressOf(), &stride, &offset);
            Context->IASetIndexBuffer(res.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            Context->DrawIndexed(res.IndexCount, 0, 0);
            DrawCount++;
        }
    }

    auto t2 = std::chrono::high_resolution_clock::now();
    CurrentMetrics.PrepassTime = std::chrono::duration<float, std::milli>(t2 - t1).count();

    BuildHiZMips();

    auto t3 = std::chrono::high_resolution_clock::now();
    CurrentMetrics.HiZTime = std::chrono::duration<float, std::milli>(t3 - t2).count();

    RunOcclusionCull(SceneData, viewProj, VisibilityConfidence, DeltaTime);
    bHasPrevFrame = true;

    auto t4 = std::chrono::high_resolution_clock::now();
    CurrentMetrics.CullTime = std::chrono::duration<float, std::milli>(t4 - t3).count();

    // ── Pass 2 직전: confidence 기반 재필터링 ────────────────────────────
    // RunOcclusionCull이 confidence를 업데이트했으므로
    // PrevVisibleQueue에서 confidence가 0인 것을 제거
    uint32_t FilteredCount = 0;
    for (uint32_t i = 0; i < PrevVisibleCount; ++i)
    {
        const uint32_t oid = PrevVisibleQueue[i];
        if (VisibilityConfidence[oid] > 0.0f)
            PrevVisibleQueue[FilteredCount++] = oid;
    }
    PrevVisibleCount = FilteredCount;

    // ── Pass 2: 최종 컬러 렌더 (PrevVisible만) ───────────────────────────────
    const uint32_t FinalCount = PrevVisibleCount;

    Context->OMSetRenderTargets(1, SceneRenderTargetView.GetAddressOf(), DepthStencilView.Get());
    const float SceneClearColor[4] = {0.03f, 0.03f, 0.06f, 1.0f};
    Context->ClearRenderTargetView(SceneRenderTargetView.Get(), SceneClearColor);
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->IASetInputLayout(InputLayout.Get());
    Context->VSSetShader(VertexShader.Get(), nullptr, 0);
    Context->PSSetShader(PixelShader.Get(), nullptr, 0);
    Context->VSSetConstantBuffers(0, 1, PerFrameBuffer.GetAddressOf());
    Context->PSSetConstantBuffers(0, 1, PerFrameBuffer.GetAddressOf());
    Context->PSSetSamplers(0, 1, DiffuseSamplerState.GetAddressOf());
    Context->RSSetState(DefaultRasterizerState.Get());
    Context->OMSetDepthStencilState(DefaultDepthStencilState.Get(), 0);

    const uint32_t FinalBulk = FinalCount * AlignedConstantSize;
    D3D11_MAP FinalMapType = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (PerObjectRingBufferOffset + FinalBulk > BufferCapacity)
    {
        FinalMapType = D3D11_MAP_WRITE_DISCARD;
        PerObjectRingBufferOffset = 0;
    }

    D3D11_MAPPED_SUBRESOURCE FinalMap = {};
    if (FAILED(Context->Map(PerObjectBuffer.Get(), 0, FinalMapType, 0, &FinalMap)))
        return;

    uint8_t* FinalDest = static_cast<uint8_t*>(FinalMap.pData) + PerObjectRingBufferOffset;
    const uint32_t FinalBase = PerObjectRingBufferOffset;

    static uint32_t FinalSortedQueue[Scene::FSceneDataSOA::MAX_OBJECTS];
    std::array<uint32_t, RENDER_BUCKET_COUNT> MeshCounts = {};
    std::array<uint32_t, RENDER_BUCKET_COUNT> MeshOffsets = {};

    auto GetRenderBucketIndex = [&](uint32_t MeshID, uint32_t& OutBucketIndex) -> bool
    {
        if (MeshID < TOTAL_MESH_RESOURCE_COUNT)
        {
            OutBucketIndex = MeshID;
            return true;
        }

        if (MeshID >= BILLBOARD_MESH_ID_OFFSET && MeshID < BILLBOARD_MESH_ID_OFFSET + BASE_MESH_TYPES)
        {
            OutBucketIndex = TOTAL_MESH_RESOURCE_COUNT + (MeshID - BILLBOARD_MESH_ID_OFFSET);
            return true;
        }

        return false;
    };

    for (uint32_t i = 0; i < FinalCount; ++i)
    {
        uint32_t BucketIndex = 0;
        if (GetRenderBucketIndex(SceneData->MeshIDs[PrevVisibleQueue[i]], BucketIndex))
        {
            ++MeshCounts[BucketIndex];
        }
    }

    {
        uint32_t cur = 0;
        for (uint32_t i = 0; i < RENDER_BUCKET_COUNT; ++i)
        {
            MeshOffsets[i] = cur;
            cur += MeshCounts[i];
        }
    }

    std::array<uint32_t, RENDER_BUCKET_COUNT> TempOffsets = MeshOffsets;

    for (uint32_t i = 0; i < FinalCount; ++i)
    {
        const uint32_t oid = PrevVisibleQueue[i];
        uint32_t BucketIndex = 0;
        if (!GetRenderBucketIndex(SceneData->MeshIDs[oid], BucketIndex))
            continue;

        const uint32_t sidx = TempOffsets[BucketIndex]++;
        FinalSortedQueue[sidx] = oid;

        const Math::FPacked3x4Matrix& mat = SceneData->WorldMatrices[oid];
        FPerObjectConstants* dest = reinterpret_cast<FPerObjectConstants*>(FinalDest + sidx * AlignedConstantSize);
        DirectX::XMStoreFloat4(&dest->Row0, mat.Row0);
        DirectX::XMStoreFloat4(&dest->Row1, mat.Row1);
        DirectX::XMStoreFloat4(&dest->Row2, mat.Row2);

        const Graphics::URenderer::FMeshResource* res = GetMeshResource(SceneData->MeshIDs[oid]);
        DirectX::XMFLOAT3 localCenter = res ? res->LocalCenter : DirectX::XMFLOAT3(0, 0, 0);

        dest->Padding = {localCenter.x, localCenter.y, localCenter.z, 0.0f};
    }
    Context->Unmap(PerObjectBuffer.Get(), 0);
    PerObjectRingBufferOffset += FinalBulk;

    // 메시별 배치 드로우
    for (uint32_t mid = 0; mid < TOTAL_MESH_RESOURCE_COUNT; ++mid)
    {
        if (MeshCounts[mid] == 0)
            continue;
        const FMeshResource& res = MeshResources[mid];
        if (!res.VertexBuffer || !res.IndexBuffer)
            continue;

        {
            D3D11_MAPPED_SUBRESOURCE matmap = {};
            if (SUCCEEDED(Context->Map(MaterialBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &matmap)))
            {
                FMaterialConstants mc = {{1, 1, 1, 1}};
                memcpy(matmap.pData, &mc, sizeof(mc));
                Context->Unmap(MaterialBuffer.Get(), 0);
            }
        }
        Context->PSSetConstantBuffers(2, 1, MaterialBuffer.GetAddressOf());

        ID3D11ShaderResourceView* srv =
            res.DiffuseTextureView ? res.DiffuseTextureView.Get() : DefaultWhiteTextureView.Get();
        Context->PSSetShaderResources(0, 1, &srv);

        UINT stride = sizeof(FMeshVertex), offset = 0;
        Context->IASetVertexBuffers(0, 1, res.VertexBuffer.GetAddressOf(), &stride, &offset);
        Context->IASetIndexBuffer(res.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

        for (uint32_t i = MeshOffsets[mid]; i < MeshOffsets[mid] + MeshCounts[mid]; ++i)
        {
            if (Context1)
            {
                UINT off = (FinalBase + i * AlignedConstantSize) / 16;
                UINT cnt = AlignedConstantSize / 16;
                Context1->VSSetConstantBuffers1(1, 1, PerObjectBuffer.GetAddressOf(), &off, &cnt);
            }
            Context->DrawIndexed(res.IndexCount, 0, 0);
            DrawCount++;
        }
    }
    auto t5 = std::chrono::high_resolution_clock::now();
    CurrentMetrics.DrawTime = std::chrono::duration<float, std::milli>(t5 - t4).count();

    // 통계 업데이트
    CurrentMetrics.DrawCount = DrawCount;
    CurrentMetrics.TotalObjectsCount = InSceneManager.GetObjectCount();
    CurrentMetrics.PrevVisible = PrevVisibleCount;
    CurrentMetrics.PrevInvisible = PrevInvisibleCount;

    // 글로벌 지표 동기화 (HUD용)
    Core::GPerformanceMetrics.SplitTime = CurrentMetrics.SplitTime;
    Core::GPerformanceMetrics.PrepassTime = CurrentMetrics.PrepassTime;
    Core::GPerformanceMetrics.HiZTime = CurrentMetrics.HiZTime;
    Core::GPerformanceMetrics.CullTime = CurrentMetrics.CullTime;
    Core::GPerformanceMetrics.DrawTime = CurrentMetrics.DrawTime;
    Core::GPerformanceMetrics.DrawCount = CurrentMetrics.DrawCount;
    Core::GPerformanceMetrics.TotalObjectsCount = CurrentMetrics.TotalObjectsCount;
    Core::GPerformanceMetrics.PrevVisible = CurrentMetrics.PrevVisible;
    Core::GPerformanceMetrics.PrevInvisible = CurrentMetrics.PrevInvisible;

    // ── Billboard 렌더 ────────────────────────────────────────────────────────
    // (Billboard는 기존 RenderScene의 고-밀도 버전 로직과 동일하게 유지)
    Context->IASetInputLayout(BillboardLayout.Get());
    Context->VSSetShader(BillboardVS.Get(), nullptr, 0);
    Context->PSSetShader(BillboardPS.Get(), nullptr, 0);

    for (uint32_t mid = 0; mid < BASE_MESH_TYPES; ++mid)
    {
        if (!ImpostorResources[mid].bIsBaked)
            continue;
        const uint32_t BucketIndex = TOTAL_MESH_RESOURCE_COUNT + mid;
        if (MeshCounts[BucketIndex] == 0)
            continue;

        Context->PSSetShaderResources(0, 1, ImpostorResources[mid].SnapshotSRV.GetAddressOf());
        UINT stride = sizeof(FBillboardVertex), offset = 0;
        Context->IASetVertexBuffers(0, 1, BillboardVB.GetAddressOf(), &stride, &offset);
        Context->IASetIndexBuffer(BillboardIB.Get(), DXGI_FORMAT_R32_UINT, 0);

        for (uint32_t i = MeshOffsets[BucketIndex]; i < MeshOffsets[BucketIndex] + MeshCounts[BucketIndex]; ++i)
        {
            if (Context1)
            {
                UINT off = (FinalBase + i * AlignedConstantSize) / 16;
                UINT cnt = AlignedConstantSize / 16;
                Context1->VSSetConstantBuffers1(1, 1, PerObjectBuffer.GetAddressOf(), &off, &cnt);
                Context1->PSSetConstantBuffers1(1, 1, PerObjectBuffer.GetAddressOf(), &off, &cnt);
            }
            Context->DrawIndexed(6, 0, 0);
            DrawCount++;
        }
    }

    // ── 타이밍 로그 ──────────────────────────────────────────────────────────
    auto ms = [](auto a, auto b) { return std::chrono::duration<float, std::milli>(b - a).count(); };
    char buf[256];

    // DrawDebugBVH(InSceneManager);
    // DrawDebugBVH(InSceneManager);
    DrawDebugGrid(InSceneManager);

    // ========================================================================
    // [DEBUG] 모든 객체의 AABB 및 Sphere를 그리고 싶을 때 아래 주석을 해제하세요.
    // DrawDebugObjects(InSceneManager);
    // ========================================================================

    DrawDebugSelected(InSceneManager);
    DrawDebugBVH(InSceneManager);
    DrawDebugUniformGrid(InSceneManager);

    if (DebugRenderer)
    {
        DebugRenderer->Render(Context.Get(), view * proj);
    }
}

void URenderer::DrawDebugUniformGrid(const Scene::USceneManager& InSceneManager)
{
    if (!DebugSettings.bDrawUniformGrid || !DebugRenderer)
        return;

    const Scene::UUniformGrid* Grid = InSceneManager.GetGrid();
    if (!Grid)
        return;

    const auto& Cells = Grid->GetCells();
    for (const auto& Cell : Cells)
    {
        if (Cell.Count > 0)
        {
            DebugRenderer->AddBox(Cell.CellBox, {1.0f, 0.0f, 1.0f, 1.0f}); // Magenta for non-empty cells
        }
        else
        {
            DebugRenderer->AddBox(Cell.CellBox, {0.2f, 0.2f, 0.2f, 0.2f}); // Dark gray for empty cells
        }
    }
}

void URenderer::DrawDebugSelected(const Scene::USceneManager& InSceneManager)
{
    if (!DebugRenderer)
        return;

    const Scene::FSceneSelectionData& Selection = InSceneManager.GetSelectionData();
    if (!Selection.bHasSelection)
        return;

    const Scene::FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
    if (!SceneData)
        return;

    for (uint32_t ObjIndex : Selection.SelectedObjectIndices)
    {
        if (ObjIndex < SceneData->TotalObjectCount)
        {
            Math::FBox Box;
            Box.Min = {SceneData->MinX[ObjIndex], SceneData->MinY[ObjIndex], SceneData->MinZ[ObjIndex]};
            Box.Max = {SceneData->MaxX[ObjIndex], SceneData->MaxY[ObjIndex], SceneData->MaxZ[ObjIndex]};

            // 선택된 객체의 AABB를 눈에 띄는 색(예: 노란색)으로 그립니다.
            DebugRenderer->AddBox(Box, {0.f, 0.f, 1.0f, 1.0f});
        }
    }
}

void URenderer::DrawDebugBVH(const Scene::USceneManager& InSceneManager)
{
    if (!DebugSettings.bDrawBVH || !DebugRenderer)
        return;

    const Scene::FSceneBVH* BVH = InSceneManager.GetSceneBVH();
    if (!BVH || BVH->Nodes.empty())
        return;

    for (const auto& Node : BVH->Nodes)
    {
        DirectX::XMFLOAT4 Color =
            Node.IsLeaf() ? DirectX::XMFLOAT4{1.0f, 0.0f, 0.0f, 1.0f} : DirectX::XMFLOAT4{0.0f, 1.0f, 0.0f, 1.0f};
        DebugRenderer->AddBox(Node.Bounds, Color);
    }
}

void URenderer::DrawDebugGrid(const Scene::USceneManager& InSceneManager)
{
    if (!DebugRenderer || (!DebugSettings.bDrawGrid && !DebugSettings.bDrawWorldAxes))
        return;

    const float HalfExtent = (std::max)(DebugSettings.GridPlaneHalfExtent, 1.0f);
    const float Spacing = (std::max)(DebugSettings.GridPlaneSpacing, 0.1f);
    const DirectX::XMFLOAT4 PlaneColor = {0.35f, 0.38f, 0.45f, 1.0f};
    const DirectX::XMFLOAT4 AxisXColor = {0.95f, 0.25f, 0.25f, 1.0f};
    const DirectX::XMFLOAT4 AxisYColor = {0.25f, 0.85f, 0.35f, 1.0f};
    const DirectX::XMFLOAT4 AxisZColor = {0.30f, 0.55f, 1.0f, 1.0f};
    const float CenterThreshold = Spacing * 0.25f;

    float BaseX = std::floor(CameraState.Position.x / Spacing) * Spacing;
    float BaseY = std::floor(CameraState.Position.y / Spacing) * Spacing;

    if (DebugSettings.bDrawGrid)
    {
        for (float Offset = -HalfExtent; Offset <= HalfExtent + Spacing * 0.5f; Offset += Spacing)
        {
            float CurrentX = BaseX + Offset;
            if (std::abs(CurrentX) > CenterThreshold)
            {
                DebugRenderer->AddLine({CurrentX, BaseY - HalfExtent, 0.0f}, {CurrentX, BaseY + HalfExtent, 0.0f},
                                       PlaneColor);
            }

            float CurrentY = BaseY + Offset;
            if (std::abs(CurrentY) > CenterThreshold)
            {
                DebugRenderer->AddLine({BaseX - HalfExtent, CurrentY, 0.0f}, {BaseX + HalfExtent, CurrentY, 0.0f},
                                       PlaneColor);
            }
        }
    }

    if (DebugSettings.bDrawWorldAxes)
    {
        float BaseZ = std::floor(CameraState.Position.z / Spacing) * Spacing;
        DebugRenderer->AddLine({BaseX - HalfExtent, 0.0f, 0.0f}, {BaseX + HalfExtent, 0.0f, 0.0f}, AxisXColor);
        DebugRenderer->AddLine({0.0f, BaseY - HalfExtent, 0.0f}, {0.0f, BaseY + HalfExtent, 0.0f}, AxisYColor);
        DebugRenderer->AddLine({0.0f, 0.0f, BaseZ - HalfExtent}, {0.0f, 0.0f, BaseZ + HalfExtent}, AxisZColor);
    }
}

void URenderer::DrawDebugObjects(const Scene::USceneManager& InSceneManager)
{
    if (!DebugRenderer)
        return;

    const Scene::FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
    if (!SceneData)
        return;

    for (uint32_t i = 0; i < SceneData->TotalObjectCount; ++i)
    {
        // AABB 그리기 (초록색)
        Math::FBox Box;
        Box.Min = {SceneData->MinX[i], SceneData->MinY[i], SceneData->MinZ[i]};
        Box.Max = {SceneData->MaxX[i], SceneData->MaxY[i], SceneData->MaxZ[i]};
        DebugRenderer->AddBox(Box, {0.0f, 1.0f, 0.0f, 1.0f});

        // Sphere 그리기 (파란색)
        DirectX::XMFLOAT3 Center = {SceneData->CenterX[i], SceneData->CenterY[i], SceneData->CenterZ[i]};
        DebugRenderer->AddSphere(Center, SceneData->Radius[i], {0.0f, 0.0f, 1.0f, 1.0f});
    }
}

// ============================================================================
void URenderer::EndFrame()
{
    SwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
}

} // namespace Graphics
