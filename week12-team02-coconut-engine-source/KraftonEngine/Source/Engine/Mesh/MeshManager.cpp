#include "MeshManager.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/MeshBinary.h"
#include "Materials/Material.h"
#include "Core/Log.h"
#include "Serialization/WindowsArchive.h"
#include "Engine/Platform/Paths.h"
#include "Materials/MaterialManager.h"
#include "Asset/AssetPackage.h"

#include <algorithm>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <memory>

TMap<FString, UStaticMesh*> FMeshManager::StaticMeshCache;
TMap<FString, USkeletalMesh*> FMeshManager::SkeletalMeshCache;
TArray<FMeshAssetListItem> FMeshManager::AvailableStaticMeshFiles;
TArray<FMeshAssetListItem> FMeshManager::AvailableSkeletalMeshFiles;

FMeshManager& FMeshManager::Get()
{
	static FMeshManager Instance;
	return Instance;
}

static void EnsureMeshCacheDirExists()
{
	static bool bCreated = false;
	if (!bCreated)
	{
		std::wstring CacheDir = FPaths::RootDir() + L"Asset/MeshCache/";
		FPaths::CreateDir(CacheDir);
		bCreated = true;
	}
}

static FString NormalizeProjectPath(const FString& Path)
{
	return FPaths::MakeProjectRelative(Path);
}

static FString GetMeshPackageFilePath(const FString& SourcePath, EAssetPackageType Type)
{
	(void)Type;

	std::filesystem::path SrcPath(FPaths::ToWide(SourcePath));
	std::wstring Ext = SrcPath.extension().wstring();
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);

	if (Ext == MeshBinary::AssetPackageExtension)
	{
		return NormalizeProjectPath(SourcePath);
	}

	return FString();
}

static std::filesystem::path ResolveProjectPath(const FString& Path)
{
	std::filesystem::path FullPath(FPaths::ToWide(Path));
	if (!FullPath.is_absolute())
	{
		FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
	}
	return FullPath.lexically_normal();
}

static bool TryGetSourceFileState(const FString& SourcePath, uint64& OutTimestamp, uint64& OutFileSize)
{
	std::filesystem::path FullPath = ResolveProjectPath(SourcePath);

	if (!std::filesystem::exists(FullPath) || !std::filesystem::is_regular_file(FullPath)) return false;

	OutFileSize = static_cast<uint64>(std::filesystem::file_size(FullPath));

	const auto WriteTime = std::filesystem::last_write_time(FullPath);
	OutTimestamp = static_cast<uint64>(WriteTime.time_since_epoch().count());

	return true;
}

static FAssetImportMetadata MakeImportMetadata(const FString& SourcePath)
{
	FAssetImportMetadata Metadata;
	Metadata.SourcePath = NormalizeProjectPath(SourcePath);

	TryGetSourceFileState(SourcePath, Metadata.SourceTimestamp, Metadata.SourceFileSize);

	return Metadata;
}

static bool IsPackageSourceStale(const FString& BinaryPath, EAssetPackageType ExpectedType, bool& bOutMissingSource)
{
	bOutMissingSource = false;

	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadMetadata(BinaryPath, ExpectedType, Metadata)) return true;

	uint64 CurrentTimestamp = 0;
	uint64 CurrentFileSize = 0;
	if (!TryGetSourceFileState(Metadata.SourcePath, CurrentTimestamp, CurrentFileSize))
	{
		bOutMissingSource = true;
		return true;
	}

	return !Metadata.MatchesSource(CurrentTimestamp, CurrentFileSize);
}

static bool LoadStaticMeshBinary(UStaticMesh* StaticMesh, const FString& BinaryPath)
{
	FWindowsBinReader Reader(BinaryPath);
	if (!Reader.IsValid())
	{
		UE_LOG("StaticMesh binary open failed. Path=%s", BinaryPath.c_str());
		return false;
	}

	try
	{
		FAssetPackageHeader Header;
		Reader << Header;

		if (!Header.IsValid(EAssetPackageType::StaticMesh))
		{
			UE_LOG("StaticMesh binary read failed: invalid file header. Path=%s", BinaryPath.c_str());
			return false;
		}

		FAssetImportMetadata Metadata;
		Reader << Metadata;

		StaticMesh->Serialize(Reader);
	}
	catch (const std::exception&)
	{
		UE_LOG("StaticMesh binary read failed: serialization threw an exception. Path=%s", BinaryPath.c_str());
		return false;
	}

	if (!Reader.IsValid())
	{
		UE_LOG("StaticMesh binary read failed: file data is incomplete or corrupted. Path=%s", BinaryPath.c_str());
		return false;
	}

	return true;
}

static bool SaveStaticMeshBinary(UStaticMesh* StaticMesh, const FString& BinaryPath, const FString& SourcePath)
{
	FWindowsBinWriter Writer(BinaryPath);
	if (!Writer.IsValid())
	{
		UE_LOG("StaticMesh binary save failed: could not open file. Path=%s", BinaryPath.c_str());
		return false;
	}

	try
	{
		FAssetPackageHeader Header;
		Header.Type = static_cast<uint32>(EAssetPackageType::StaticMesh);
		Writer << Header;

		FAssetImportMetadata Metadata = MakeImportMetadata(SourcePath);
		Writer << Metadata;

		StaticMesh->Serialize(Writer);
	}
	catch (const std::exception&)
	{
		UE_LOG("StaticMesh binary save failed: serialization threw an exception. Path=%s", BinaryPath.c_str());
		return false;
	}

	return Writer.IsValid();
}

static bool LoadSkeletalMeshBinary(USkeletalMesh* SkeletalMesh, const FString& BinaryPath)
{
	FWindowsBinReader Reader(BinaryPath);
	if (!Reader.IsValid())
	{
		UE_LOG("SkeletalMesh binary open failed. Path=%s", BinaryPath.c_str());
		return false;
	}

	try
	{
		FAssetPackageHeader Header;
		Reader << Header;

		if (!Header.IsValid(EAssetPackageType::SkeletalMesh))
		{
			UE_LOG("SkeletalMesh binary read failed: invalid file header. Path=%s", BinaryPath.c_str());
			return false;
		}

		FAssetImportMetadata Metadata;
		Reader << Metadata;

		SkeletalMesh->Serialize(Reader);
	}
	catch (const std::exception&)
	{
		UE_LOG("SkeletalMesh binary read failed: serialization threw an exception. Path=%s", BinaryPath.c_str());
		return false;
	}

	if (!Reader.IsValid())
	{
		UE_LOG("SkeletalMesh binary read failed: file data is incomplete or corrupted. Path=%s", BinaryPath.c_str());
		return false;
	}

	return true;
}

static bool SaveSkeletalMeshBinary(USkeletalMesh* SkeletalMesh, const FString& BinaryPath, const FString& SourcePath)
{
	FWindowsBinWriter Writer(BinaryPath);
	if (!Writer.IsValid())
	{
		UE_LOG("SkeletalMesh binary save failed: could not open file. Path=%s", BinaryPath.c_str());
		return false;
	}

	try
	{
		FAssetPackageHeader Header;
		Header.Type = static_cast<uint32>(EAssetPackageType::SkeletalMesh);
		Writer << Header;

		FAssetImportMetadata Metadata = MakeImportMetadata(SourcePath);
		Writer << Metadata;

		SkeletalMesh->Serialize(Writer);
	}
	catch (const std::exception&)
	{
		UE_LOG("SkeletalMesh binary save failed: serialization threw an exception. Path=%s", BinaryPath.c_str());
		return false;
	}

	return Writer.IsValid();
}

FString FMeshManager::GetStaticMeshBinaryFilePath(const FString& SourcePath)
{
	return GetMeshPackageFilePath(SourcePath, EAssetPackageType::StaticMesh);
}

FString FMeshManager::GetSkeletalMeshBinaryFilePath(const FString& SourcePath)
{
	return GetMeshPackageFilePath(SourcePath, EAssetPackageType::SkeletalMesh);
}

bool FMeshManager::IsAssetPackagePath(const FString& Path)
{
	return FAssetPackage::IsAssetPackagePath(Path);
}

bool FMeshManager::IsStaticMeshPackage(const FString& Path)
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::StaticMesh, Metadata);
}

bool FMeshManager::IsSkeletalMeshPackage(const FString& Path)
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::SkeletalMesh, Metadata);
}

void FMeshManager::ScanMeshAssets()
{
	AvailableStaticMeshFiles.clear();
	AvailableSkeletalMeshFiles.clear();

	const std::filesystem::path ProjectRoot(FPaths::RootDir());
	const std::filesystem::path AssetRoots[] = {
		ProjectRoot / L"Asset",
		ProjectRoot / L"Content",
		ProjectRoot / L"Data",
	};

	for (const std::filesystem::path& MeshAssetRoot : AssetRoots)
	{
		if (!std::filesystem::exists(MeshAssetRoot))
		{
			continue;
		}

		for (const auto& Entry : std::filesystem::recursive_directory_iterator(MeshAssetRoot))
		{
			if (!Entry.is_regular_file()) continue;

			const std::filesystem::path& Path = Entry.path();
			std::wstring Ext = Path.extension().wstring();
			std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);

			if (Ext != MeshBinary::StaticMeshBinaryExtension) continue;

			TArray<FMeshAssetListItem>* TargetList = nullptr;

			FString RelPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());

			FAssetImportMetadata Metadata;
			if (FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::StaticMesh, Metadata))
			{
				TargetList = &AvailableStaticMeshFiles;
			}
			else if (FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::SkeletalMesh, Metadata))
			{
				TargetList = &AvailableSkeletalMeshFiles;
			}
			else
			{
				continue;
			}

			FMeshAssetListItem Item;
			Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
			Item.FullPath = RelPath;
			TargetList->push_back(std::move(Item));
		}
	}
}

UStaticMesh* FMeshManager::LoadStaticMesh(const FString& PathFileName, const FImportOptions& Options, ID3D11Device* InDevice)
{
	(void)Options;
	(void)InDevice;

	UE_LOG("StaticMesh load failed: runtime source import is unsupported. Use an editor import service and pass the generated .uasset path. Path=%s", PathFileName.c_str());
	return nullptr;
}

UStaticMesh* FMeshManager::LoadStaticMesh(const FString& PathFileName, ID3D11Device* InDevice)
{
	if (!IsAssetPackagePath(PathFileName))
	{
		UE_LOG("StaticMesh load failed: unsupported path. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	const FString CacheKey = GetStaticMeshBinaryFilePath(PathFileName);
	if (CacheKey.empty())
	{
		UE_LOG("StaticMesh load failed: package path is empty. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	auto It = StaticMeshCache.find(CacheKey);
	if (It != StaticMeshCache.end())
	{
		return It->second;
	}

	const std::filesystem::path BinaryPath(FPaths::ToWide(CacheKey));
	if (!std::filesystem::exists(BinaryPath))
	{
		UE_LOG("StaticMesh load failed: StaticMesh binary file does not exist. Path=%s", CacheKey.c_str());
		return nullptr;
	}

	UStaticMesh* StaticMesh = GUObjectArray.CreateObject<UStaticMesh>();
	if (!LoadStaticMeshBinary(StaticMesh, CacheKey))
	{
		return nullptr;
	}

	bool bMissingSource = false;
	if (IsPackageSourceStale(CacheKey, EAssetPackageType::StaticMesh, bMissingSource))
	{
		UE_LOG("StaticMesh package is stale. Package=%s MissingSource=%s", CacheKey.c_str(), bMissingSource ? "true" : "false");
	}

	StaticMesh->InitResources(InDevice);
	StaticMesh->SetAssetPathFileName(CacheKey);
	StaticMeshCache[CacheKey] = StaticMesh;

	return StaticMesh;
}

UStaticMesh* FMeshManager::FindStaticMesh(const FString& PathFileName)
{
	if (PathFileName.empty() || !IsAssetPackagePath(PathFileName))
	{
		return nullptr;
	}

	const FString CacheKey = GetStaticMeshBinaryFilePath(PathFileName);
	if (CacheKey.empty())
	{
		return nullptr;
	}

	auto It = StaticMeshCache.find(CacheKey);
	return It != StaticMeshCache.end() ? It->second : nullptr;
}

USkeletalMesh* FMeshManager::LoadSkeletalMesh(const FString& PathFileName, ID3D11Device* InDevice)
{
	if (!IsAssetPackagePath(PathFileName))
	{
		UE_LOG("SkeletalMesh load failed: unsupported path. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	const FString CacheKey = GetSkeletalMeshBinaryFilePath(PathFileName);
	if (CacheKey.empty())
	{
		UE_LOG("SkeletalMesh load failed: package path is empty. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	auto It = SkeletalMeshCache.find(CacheKey);
	if (It != SkeletalMeshCache.end())
	{
		return It->second;
	}

	const std::filesystem::path BinaryPath(FPaths::ToWide(CacheKey));
	if (!std::filesystem::exists(BinaryPath))
	{
		UE_LOG("SkeletalMesh load failed: SkeletalMesh binary file does not exist. Path=%s", CacheKey.c_str());
		return nullptr;
	}

	USkeletalMesh* SkeletalMesh = GUObjectArray.CreateObject<USkeletalMesh>();
	if (!LoadSkeletalMeshBinary(SkeletalMesh, CacheKey))
	{
		return nullptr;
	}

	if (!SkeletalMesh->ResolveSkeleton())
	{
		UE_LOG("SkeletalMesh load failed: referenced Skeleton package could not be loaded. Path=%s", CacheKey.c_str());
		return nullptr;
	}

	bool bMissingSource = false;
	if (IsPackageSourceStale(CacheKey, EAssetPackageType::SkeletalMesh, bMissingSource))
	{
		UE_LOG("SkeletalMesh package is stale. Package=%s MissingSource=%s", CacheKey.c_str(), bMissingSource ? "true" : "false");
	}

	SkeletalMesh->InitResources(InDevice);
	SkeletalMesh->SetAssetPathFileName(CacheKey);
	SkeletalMeshCache[CacheKey] = SkeletalMesh;

	return SkeletalMesh;
}

USkeletalMesh* FMeshManager::FindSkeletalMesh(const FString& PathFileName)
{
	if (PathFileName.empty() || !IsAssetPackagePath(PathFileName))
	{
		return nullptr;
	}

	const FString CacheKey = GetSkeletalMeshBinaryFilePath(PathFileName);
	if (CacheKey.empty())
	{
		return nullptr;
	}

	auto It = SkeletalMeshCache.find(CacheKey);
	return It != SkeletalMeshCache.end() ? It->second : nullptr;
}
