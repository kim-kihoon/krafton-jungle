#include "Core/StaticMeshLoadService.h"

#include "Core/AssetPathPolicy.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
std::wstring GetLowerExtension(const FString& Path)
{
	fs::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	std::wstring Extension = FsPath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
	return Extension;
}

bool IsFbxPath(const FString& Path)
{
	return GetLowerExtension(Path) == L".fbx";
}
}

FStaticMeshLoadService::FStaticMeshLoadService(FResourceManager& InResourceManager)
	: ResourceManager(InResourceManager)
{
}

UStaticMesh* FStaticMeshLoadService::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (UStaticMesh* FoundMesh = ResourceManager.FindStaticMesh(NormalizedPath))
	{
		return FoundMesh;
	}

	const std::wstring RequestedExt = GetLowerExtension(NormalizedPath);

	if (RequestedExt == L".obj" && !FAssetPathPolicy::FileExists(NormalizedPath))
	{
		const FString CookedPath = FAssetPathPolicy::MakeCookedStaticMeshBinaryPath(NormalizedPath);
		if (UStaticMesh* CookedMesh = LoadMissingObjBinaryFallback(NormalizedPath, CookedPath))
		{
			ResourceManager.StaticMeshCache.RegisterLoaded(NormalizedPath, CookedMesh);
			return CookedMesh;
		}

		const FString SiblingBinaryPath = FAssetPathPolicy::MakeSiblingStaticMeshBinaryPath(NormalizedPath);
		if (UStaticMesh* SiblingMesh = LoadMissingObjBinaryFallback(NormalizedPath, SiblingBinaryPath))
		{
			ResourceManager.StaticMeshCache.RegisterLoaded(NormalizedPath, SiblingMesh);
			return SiblingMesh;
		}

		const FString CacheBinaryPath = FAssetPathPolicy::MakeStaticMeshCacheBinaryPath(NormalizedPath);
		if (UStaticMesh* CachedMesh = LoadMissingObjBinaryFallback(NormalizedPath, CacheBinaryPath))
		{
			ResourceManager.StaticMeshCache.RegisterLoaded(NormalizedPath, CachedMesh);
			return CachedMesh;
		}
	}

	if (RequestedExt == L".bin")
	{
		return LoadBinaryDrop(NormalizedPath);
	}

	if (RequestedExt == L".obj" || RequestedExt == L".fbx")
	{
		return RequestedExt == L".fbx"
			? LoadImportedFbxBinary(NormalizedPath)
			: LoadObjSourceOrCachedBinary(NormalizedPath);
	}

	UE_LOG_WARNING("[StaticMeshLoad] Unsupported mesh extension | Path=%s", NormalizedPath.c_str());
	return nullptr;
}

UStaticMesh* FStaticMeshLoadService::ImportFbxSource(const FString& Path, bool bImportMaterials)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!IsFbxPath(NormalizedPath))
	{
		UE_LOG_WARNING("[StaticMeshLoad] ImportFbxSource only supports FBX | Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	if (bImportMaterials)
	{
		ResourceManager.LoadMaterial(NormalizedPath, EMaterialShaderType::SurfaceLit);
	}

	FStaticMeshLoadOptions LoadOptions = ResourceManager.StaticMeshCache.GetLoadOptions(NormalizedPath);
	const FString BinaryPath = FAssetPathPolicy::MakeWritableStaticMeshCacheBinaryPath(NormalizedPath);

	const auto SourceStart = std::chrono::steady_clock::now();
	FStaticMesh* LoadedMeshData = ResourceManager.FbxImporter.Load(NormalizedPath, LoadOptions);
	const auto SourceEnd = std::chrono::steady_clock::now();
	const double SourceLoadSec = std::chrono::duration<double>(SourceEnd - SourceStart).count();

	if (!LoadedMeshData)
	{
		UE_LOG_ERROR("[StaticMeshLoad] Failed import | Path=%s | FbxSec=%.6f", NormalizedPath.c_str(), SourceLoadSec);
		return nullptr;
	}

	const bool bSaveBinaryOk = ResourceManager.BinarySerializer.SaveStaticMesh(BinaryPath, NormalizedPath, *LoadedMeshData);
	if (bSaveBinaryOk)
	{
		UE_LOG(
			"[StaticMeshLoad] Source=FBX | Path=%s | FbxSec=%.6f | BinarySave=OK | BinaryPath=%s",
			NormalizedPath.c_str(),
			SourceLoadSec,
			BinaryPath.c_str());
	}
	else
	{
		UE_LOG_WARNING(
			"[StaticMeshLoad] Source=FBX | Path=%s | FbxSec=%.6f | BinarySave=FAIL | BinaryPath=%s",
			NormalizedPath.c_str(),
			SourceLoadSec,
			BinaryPath.c_str());
	}

	return FinalizeLoadedMesh(
		LoadedMeshData,
		NormalizedPath,
		NormalizedPath,
		/*SecondaryCacheKey=*/ FString{},
		/*bLogLodTiming=*/ true,
		/*bLogLodSkipped=*/ true);
}

UStaticMesh* FStaticMeshLoadService::LoadMissingObjBinaryFallback(const FString& RequestedPath, const FString& BinaryPath)
{
	if (BinaryPath.empty() || !FAssetPathPolicy::FileExists(BinaryPath))
	{
		return nullptr;
	}

	UE_LOG("[StaticMeshLoad] Redirect missing OBJ to binary mesh | Source=%s | Binary=%s",
		RequestedPath.c_str(),
		BinaryPath.c_str());
	return ResourceManager.LoadStaticMesh(BinaryPath);
}

UStaticMesh* FStaticMeshLoadService::FinalizeLoadedMesh(
	FStaticMesh* MeshData,
	const FString& ResolvePath,
	const FString& PrimaryCacheKey,
	const FString& SecondaryCacheKey,
	bool bLogLodTiming,
	bool bLogLodSkipped)
{
	ResourceManager.ResolveStaticMeshMaterialSlots(ResolvePath, MeshData);
	UStaticMesh* LoadedMesh = ResourceManager.CreateStaticMeshFromLoadedData(
		MeshData, PrimaryCacheKey, bLogLodTiming, bLogLodSkipped);

	ResourceManager.StaticMeshCache.RegisterLoaded(PrimaryCacheKey, LoadedMesh);
	if (!SecondaryCacheKey.empty() && SecondaryCacheKey != PrimaryCacheKey)
	{
		ResourceManager.StaticMeshCache.RegisterLoaded(SecondaryCacheKey, LoadedMesh);
	}
	return LoadedMesh;
}

UStaticMesh* FStaticMeshLoadService::LoadBinaryDrop(const FString& NormalizedPath)
{
	const auto BinaryStart = std::chrono::steady_clock::now();
	FStaticMesh* LoadedMeshData = new FStaticMesh();
	if (!ResourceManager.BinarySerializer.LoadStaticMesh(NormalizedPath, *LoadedMeshData))
	{
		delete LoadedMeshData;
		UE_LOG_WARNING("[StaticMeshLoad] Failed binary cache drop | Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	const FString SourcePath = FPaths::Normalize(LoadedMeshData->PathFileName);
	if (!SourcePath.empty())
	{
		if (UStaticMesh* FoundSourceMesh = ResourceManager.FindStaticMesh(SourcePath))
		{
			delete LoadedMeshData;
			ResourceManager.StaticMeshCache.RegisterLoaded(NormalizedPath, FoundSourceMesh);
			return FoundSourceMesh;
		}
		if (FAssetPathPolicy::FileExists(SourcePath))
		{
			ResourceManager.LoadMaterial(SourcePath, EMaterialShaderType::SurfaceLit);
		}
	}

	UStaticMesh* LoadedMesh = FinalizeLoadedMesh(
		LoadedMeshData,
		SourcePath.empty() ? NormalizedPath : SourcePath,  // ResolvePath
		NormalizedPath,                                    // PrimaryCacheKey (= .bin path)
		SourcePath,                                        // SecondaryCacheKey (empty면 skip)
		/*bLogLodTiming=*/ false,
		/*bLogLodSkipped=*/ false);

	const auto BinaryEnd = std::chrono::steady_clock::now();
	const double BinaryLoadSec = std::chrono::duration<double>(BinaryEnd - BinaryStart).count();
	UE_LOG("[StaticMeshLoad] Source=BinaryDrop | Path=%s | BinarySec=%.6f | Source=%s",
	       NormalizedPath.c_str(),
	       BinaryLoadSec,
	       SourcePath.c_str());
	return LoadedMesh;
}

UStaticMesh* FStaticMeshLoadService::LoadImportedFbxBinary(const FString& NormalizedPath)
{
	const FString BinaryPath = FAssetPathPolicy::MakeStaticMeshCacheBinaryPath(NormalizedPath);
	if (!FAssetPathPolicy::FileExists(BinaryPath))
	{
		UE_LOG_WARNING(
			"[StaticMeshLoad] Imported FBX binary missing. Explicit import is required. | Source=%s | Binary=%s",
			NormalizedPath.c_str(),
			BinaryPath.c_str());
		return nullptr;
	}

	return LoadBinaryDrop(BinaryPath);
}

UStaticMesh* FStaticMeshLoadService::LoadObjSourceOrCachedBinary(const FString& NormalizedPath)
{
	FStaticMeshLoadOptions LoadOptions = ResourceManager.StaticMeshCache.GetLoadOptions(NormalizedPath);
	const FString BinaryPath = FAssetPathPolicy::MakeWritableStaticMeshCacheBinaryPath(NormalizedPath);

	const char* const SourceTag = "OBJ";
	ResourceManager.LoadMaterial(NormalizedPath, EMaterialShaderType::SurfaceLit);

	FStaticMesh* LoadedMeshData = nullptr;
	double BinaryLoadSec = 0.0;
	double SourceLoadSec = 0.0;

	if (ResourceManager.IsStaticMeshBinaryValid(NormalizedPath, BinaryPath))
	{
		const auto BinaryStart = std::chrono::steady_clock::now();

		LoadedMeshData = new FStaticMesh();
		if (!ResourceManager.BinarySerializer.LoadStaticMesh(BinaryPath, *LoadedMeshData))
		{
			delete LoadedMeshData;
			LoadedMeshData = nullptr;
		}

		const auto BinaryEnd = std::chrono::steady_clock::now();
		BinaryLoadSec = std::chrono::duration<double>(BinaryEnd - BinaryStart).count();
	}

	if (LoadedMeshData == nullptr)
	{
		const auto SourceStart = std::chrono::steady_clock::now();
		LoadedMeshData = ResourceManager.ObjLoader.Load(NormalizedPath, LoadOptions);
		const auto SourceEnd = std::chrono::steady_clock::now();
		SourceLoadSec = std::chrono::duration<double>(SourceEnd - SourceStart).count();

		if (LoadedMeshData == nullptr)
		{
			UE_LOG_ERROR("[StaticMeshLoad] Failed | Path=%s | BinarySec=%.6f | %sSec=%.6f", NormalizedPath.c_str(), BinaryLoadSec,
			       SourceTag, SourceLoadSec);
			return nullptr;
		}

		// Binary serialize는 SlotName만 쓰고 Material* 포인터는 안 쓰므로,
		// resolve는 최종 단계(FinalizeLoadedMesh)에서 한 번만 하면 됨.
		const bool bSaveBinaryOk = ResourceManager.BinarySerializer.SaveStaticMesh(BinaryPath, NormalizedPath, *LoadedMeshData);
		if (bSaveBinaryOk)
		{
			UE_LOG(
				"[StaticMeshLoad] Source=%s | Path=%s | %sSec=%.6f | BinarySave=OK | BinaryPath=%s",
				SourceTag,
				NormalizedPath.c_str(),
				SourceTag,
				SourceLoadSec,
				BinaryPath.c_str());
		}
		else
		{
			UE_LOG_WARNING(
				"[StaticMeshLoad] Source=%s | Path=%s | %sSec=%.6f | BinarySave=FAIL | BinaryPath=%s",
				SourceTag,
				NormalizedPath.c_str(),
				SourceTag,
				SourceLoadSec,
				BinaryPath.c_str());
		}
	}
	else
	{
		UE_LOG(
			"[StaticMeshLoad] Source=Binary | Path=%s | BinarySec=%.6f | BinaryPath=%s",
			NormalizedPath.c_str(),
			BinaryLoadSec,
			BinaryPath.c_str());
	}

	UStaticMesh* LoadedMesh = FinalizeLoadedMesh(
		LoadedMeshData,
		NormalizedPath,
		NormalizedPath,
		/*SecondaryCacheKey=*/ FString{},
		/*bLogLodTiming=*/ true,
		/*bLogLodSkipped=*/ true);

	return LoadedMesh;
}
