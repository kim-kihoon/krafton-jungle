#include "Core/SkeletalMeshLoadService.h"

#include "Core/AssetPathPolicy.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>

namespace
{
	bool IsBinaryPath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
		std::wstring Extension = FsPath.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
		return Extension == L".bin";
	}

	bool IsFbxPath(const FString& Path)
	{
		std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
		std::wstring Extension = FsPath.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
		return Extension == L".fbx";
	}

	FString TryDeriveSourceFbxPathFromSiblingSkeletalMeshBinary(const FString& BinaryPath)
	{
		const std::filesystem::path BinaryFsPath(FPaths::ToWide(FPaths::Normalize(BinaryPath)));
		const FString FileStem = FPaths::ToUtf8(BinaryFsPath.stem().generic_wstring());
		const FString Marker = "_skeletalmesh_";
		const size_t MarkerPos = FileStem.find(Marker);
		if (MarkerPos == FString::npos || MarkerPos == 0)
		{
			return {};
		}

		std::filesystem::path SourcePath = BinaryFsPath.parent_path() / FPaths::ToWide(FileStem.substr(0, MarkerPos) + ".fbx");
		SourcePath = SourcePath.lexically_normal();
		const FString NormalizedSourcePath = FPaths::Normalize(FPaths::ToUtf8(SourcePath.generic_wstring()));
		return FAssetPathPolicy::FileExists(NormalizedSourcePath) ? NormalizedSourcePath : FString{};
	}

	FString ResolveMaterialSourcePathForSkeletalBinary(const FString& BinaryPath, const FString& SerializedPath)
	{
		const FString NormalizedSerializedPath = FPaths::Normalize(SerializedPath);
		if (IsFbxPath(NormalizedSerializedPath))
		{
			// 패키지에 FBX가 없어도 serialized source path는 material alias 식별자로 유지
			return NormalizedSerializedPath;
		}

		const FString DerivedSourcePath = TryDeriveSourceFbxPathFromSiblingSkeletalMeshBinary(BinaryPath);
		return DerivedSourcePath.empty() ? FPaths::Normalize(BinaryPath) : DerivedSourcePath;
	}

	TArray<FString> FindSiblingSkeletalMeshBinaries(const FString& SourceFbxPath, const FString& SkeletonName)
	{
		TArray<FString> Result;

		const FString NormalizedSourcePath = FPaths::Normalize(SourceFbxPath);
		const std::filesystem::path SourceFsPath(FPaths::ToWide(NormalizedSourcePath));
		const std::filesystem::path ParentPath = SourceFsPath.parent_path();

		std::error_code Ec;
		if (ParentPath.empty() || !std::filesystem::exists(ParentPath, Ec) || Ec)
		{
			return Result;
		}

		const FString Prefix = FPaths::ToUtf8(SourceFsPath.stem().generic_wstring()) + "_skeletalmesh_";
		const FString SanitizedSkeletonName = FAssetPathPolicy::SanitizeImportedAssetToken(SkeletonName);

		for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(ParentPath, Ec))
		{
			if (Ec)
			{
				break;
			}

			if (!Entry.is_regular_file(Ec) || Ec)
			{
				continue;
			}

			std::wstring Extension = Entry.path().extension().wstring();
			std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
			if (Extension != L".bin")
			{
				continue;
			}

			const FString FileName = FPaths::ToUtf8(Entry.path().filename().generic_wstring());
			if (FileName.rfind(Prefix, 0) != 0)
			{
				continue;
			}

			if (!SkeletonName.empty())
			{
				const FString Expected = Prefix + SanitizedSkeletonName + ".bin";
				if (FileName != Expected)
				{
					continue;
				}
			}

			Result.push_back(FPaths::Normalize(FPaths::ToUtf8(Entry.path().generic_wstring())));
		}

		std::sort(Result.begin(), Result.end());
		return Result;
	}
}

FSkeletalMeshLoadService::FSkeletalMeshLoadService(FResourceManager& InResourceManager)
	: ResourceManager(InResourceManager)
{
}

USkeletalMesh* FSkeletalMeshLoadService::Load(const FString& Path)
{
	return Load(Path, FString());
}

USkeletalMesh* FSkeletalMeshLoadService::Load(const FString& Path, const FString& SkeletonName)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	const FString CacheLookupKey = SkeletonName.empty() ? NormalizedPath : NormalizedPath + "#" + SkeletonName;

	if (USkeletalMesh* FoundMesh = ResourceManager.FindSkeletalMesh(CacheLookupKey))
	{
		return FoundMesh;
	}

	if (IsBinaryPath(NormalizedPath))
	{
		return LoadBinary(NormalizedPath, NormalizedPath);
	}

	ResourceManager.LoadMaterial(NormalizedPath, EMaterialShaderType::SurfaceLit);

	if (USkeletalMesh* ImportedMesh = LoadSiblingImportedBinary(NormalizedPath, SkeletonName))
	{
		ResourceManager.SkeletalMeshMap[CacheLookupKey] = ImportedMesh;
		return ImportedMesh;
	}

	if (IsFbxPath(NormalizedPath))
	{
		UE_LOG_WARNING("[SkeletalMeshLoad] Imported FBX binary missing. Explicit import is required. | Path=%s", NormalizedPath.c_str());
	}
	else
	{
		UE_LOG_WARNING("[SkeletalMeshLoad] Unsupported skeletal mesh source path | Path=%s", NormalizedPath.c_str());
	}
	return nullptr;
}

USkeletalMesh* FSkeletalMeshLoadService::ImportFbxSource(
	const FString& Path,
	const FString& SkeletonName,
	bool bImportMaterials,
	const TArray<USkeletonAsset*>* ImportedSkeletonsOverride)
{
	const FString NormalizedPath = FPaths::Normalize(Path);
	if (!IsFbxPath(NormalizedPath))
	{
		UE_LOG_WARNING("[SkeletalMeshLoad] ImportFbxSource only supports FBX | Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	return ImportFbxSourceToBinary(NormalizedPath, SkeletonName, bImportMaterials, ImportedSkeletonsOverride);
}

USkeletalMesh* FSkeletalMeshLoadService::LoadBinary(const FString& BinaryPath, const FString& CacheKey)
{
	FSkeletalMesh* LoadedMeshData = new FSkeletalMesh();
	if (!ResourceManager.BinarySerializer.LoadSkeletalMesh(BinaryPath, *LoadedMeshData))
	{
		delete LoadedMeshData;
		return nullptr;
	}

	const FString MaterialResolvePath = ResolveMaterialSourcePathForSkeletalBinary(BinaryPath, LoadedMeshData->PathFileName);
	if (IsFbxPath(MaterialResolvePath))
	{
		// FBX를 로드하는 것이 아니라 저장된 material cache와 slot alias만 복원
		ResourceManager.LoadMaterial(MaterialResolvePath, EMaterialShaderType::SurfaceLit);
	}

	LoadedMeshData->PathFileName = BinaryPath;

	UE_LOG("[SkeletalMeshLoad] Source=ImportedBinary | Path=%s", BinaryPath.c_str());
	return FinalizeLoadedMesh(
		LoadedMeshData,
		MaterialResolvePath,
		CacheKey,
		BinaryPath);
}

USkeletalMesh* FSkeletalMeshLoadService::LoadSiblingImportedBinary(const FString& NormalizedPath, const FString& SkeletonName)
{
	const TArray<FString> BinaryPaths = FindSiblingSkeletalMeshBinaries(NormalizedPath, SkeletonName);
	for (const FString& BinaryPath : BinaryPaths)
	{
		if (USkeletalMesh* Mesh = LoadBinary(BinaryPath, BinaryPath))
		{
			return Mesh;
		}
	}

	return nullptr;
}

USkeletalMesh* FSkeletalMeshLoadService::ImportFbxSourceToBinary(
	const FString& NormalizedPath,
	const FString& SkeletonName,
	bool bImportMaterials,
	const TArray<USkeletonAsset*>* ImportedSkeletonsOverride)
{
	FStaticMeshLoadOptions LoadOptions;
	const FString BinaryPath = SkeletonName.empty()
		? FAssetPathPolicy::MakeWritableSkeletalMeshCacheBinaryPath(NormalizedPath)
		: FAssetPathPolicy::MakeSiblingSkeletalMeshBinaryPath(NormalizedPath, SkeletonName);

	if (bImportMaterials)
	{
		ResourceManager.ImportMaterialFromFbx(NormalizedPath);
	}

	const auto SourceStart = std::chrono::steady_clock::now();
	const TArray<USkeletonAsset*> LocalImportedSkeletons =
		ImportedSkeletonsOverride ? TArray<USkeletonAsset*>() : ResourceManager.ImportSkeletonsFromFbx(NormalizedPath);
	const TArray<USkeletonAsset*>& ImportedSkeletons =
		ImportedSkeletonsOverride ? *ImportedSkeletonsOverride : LocalImportedSkeletons;
	FSkeletalMesh* LoadedMeshData = ResourceManager.FbxImporter.LoadSkeletalMesh(NormalizedPath, LoadOptions);
	const auto SourceEnd = std::chrono::steady_clock::now();
	const double SourceLoadSec = std::chrono::duration<double>(SourceEnd - SourceStart).count();

	if (!LoadedMeshData)
	{
		UE_LOG_ERROR("[SkeletalMeshLoad] Failed import | Path=%s | FbxSec=%.6f",
			NormalizedPath.c_str(), SourceLoadSec);
		return nullptr;
	}

	const USkeletonAsset* TargetSkeleton = nullptr;
	if (!ImportedSkeletons.empty())
	{
		if (!SkeletonName.empty())
		{
			for (const USkeletonAsset* Skeleton : ImportedSkeletons)
			{
				if (Skeleton && Skeleton->GetRootNodeName() == SkeletonName)
				{
					TargetSkeleton = Skeleton;
					break;
				}
			}
		}

		if (!TargetSkeleton)
		{
			TargetSkeleton = ImportedSkeletons[0];
		}
	}

	FString SaveBinaryPath = BinaryPath;
	if (TargetSkeleton)
	{
		SaveBinaryPath = FAssetPathPolicy::MakeSiblingSkeletalMeshBinaryPath(
			NormalizedPath,
			TargetSkeleton->GetRootNodeName());
		LoadedMeshData->SkeletonSourcePath = FAssetPathPolicy::MakeAssetRelativePath(
			SaveBinaryPath,
			TargetSkeleton->GetAssetPathFileName());
	}

	// Material 포인터는 직렬화 대상이 아니므로 이 시점에 그대로 굽고, resolve는 Finalize에서 한 번만.
	const bool bSaveBinaryOk = ResourceManager.BinarySerializer.SaveSkeletalMesh(SaveBinaryPath, NormalizedPath, *LoadedMeshData);
	LoadedMeshData->PathFileName = SaveBinaryPath;
	if (bSaveBinaryOk)
	{
		UE_LOG("[SkeletalMeshLoad] Source=FBX | Path=%s | FbxSec=%.6f | BinarySave=OK | BinaryPath=%s",
		       NormalizedPath.c_str(), SourceLoadSec, SaveBinaryPath.c_str());
	}
	else
	{
		UE_LOG_WARNING("[SkeletalMeshLoad] Source=FBX | Path=%s | FbxSec=%.6f | BinarySave=FAIL | BinaryPath=%s",
		               NormalizedPath.c_str(), SourceLoadSec, SaveBinaryPath.c_str());
	}

	return FinalizeLoadedMesh(LoadedMeshData, NormalizedPath, SaveBinaryPath, SaveBinaryPath);
}

USkeletalMesh* FSkeletalMeshLoadService::FinalizeLoadedMesh(
	FSkeletalMesh* MeshData,
	const FString& ResolvePath,
	const FString& CacheKey,
	const FString& SkeletonReferenceBasePath)
{
	ResourceManager.ResolveSkeletalMeshMaterialSlots(ResolvePath, MeshData);

	USkeletalMesh* LoadedMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	LoadedMesh->SetMeshData(MeshData);

	if (!MeshData->SkeletonSourcePath.empty())
	{
		const FString SkeletonPath = FAssetPathPolicy::ResolveAssetRelativePath(
			SkeletonReferenceBasePath.empty() ? ResolvePath : SkeletonReferenceBasePath,
			MeshData->SkeletonSourcePath);

		if (USkeletonAsset* Skeleton = ResourceManager.LoadSkeleton(SkeletonPath))
		{
			LoadedMesh->SetSkeletonAsset(Skeleton);
		}
		else
		{
			UE_LOG_WARNING("[SkeletalMeshLoad] Failed to load target skeleton | Mesh=%s | Skeleton=%s",
			               CacheKey.c_str(),
			               SkeletonPath.c_str());
		}
	}

	ResourceManager.SkeletalMeshMap[CacheKey] = LoadedMesh;
	if (std::find(ResourceManager.SkeletalMeshFilePaths.begin(), ResourceManager.SkeletalMeshFilePaths.end(), CacheKey)
		== ResourceManager.SkeletalMeshFilePaths.end())
	{
		ResourceManager.SkeletalMeshFilePaths.push_back(CacheKey);
	}

	UE_LOG("[SkeletalMeshLoad] Loaded | Path=%s | Vertices=%zu | Indices=%zu | Bones=%zu | Sections=%zu",
	       CacheKey.c_str(),
	       LoadedMesh->GetVertices().size(),
	       LoadedMesh->GetIndices().size(),
	       LoadedMesh->GetBones().size(),
	       LoadedMesh->GetSections().size());

	return LoadedMesh;
}
