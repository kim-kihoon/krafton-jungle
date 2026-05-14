#include "Editor/Importer/Fbx/FbxAssetImporter.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/PlatformTime.h"
#include "Core/ResourceManager.h"
#include "Editor/Importer/Fbx/FbxMaterialExtractor.h"
#include "Editor/Importer/Fbx/FbxSceneDocument.h"
#include "Editor/Importer/Fbx/FbxSceneInspector.h"
#include "Editor/Importer/Fbx/FbxSceneUtils.h"
#include "Editor/Importer/Fbx/FbxSkeletalMeshExtractor.h"
#include "Editor/Importer/Fbx/FbxSkeletonExtractor.h"
#include "Editor/Importer/Fbx/FbxStaticMeshExtractor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace
{
	uint64 GetFileWriteTimeTicks(const FString& Path)
	{
		namespace fs = std::filesystem;

		fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(Path)));
		if (!fs::exists(FilePath))
		{
			return 0;
		}

		auto WriteTime = fs::last_write_time(FilePath);
		auto Duration = WriteTime.time_since_epoch();
		return static_cast<uint64>(std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
	}

	uint64 HashFileFNV1a(const FString& Path)
	{
		std::ifstream File(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(Path))), std::ios::binary);
		if (!File.is_open())
		{
			return 0;
		}

		uint64 Hash = 14695981039346656037ull;
		char Buffer[64 * 1024];
		while (File.good())
		{
			File.read(Buffer, sizeof(Buffer));
			const std::streamsize ReadBytes = File.gcount();
			for (std::streamsize Index = 0; Index < ReadBytes; ++Index)
			{
				Hash ^= static_cast<unsigned char>(Buffer[Index]);
				Hash *= 1099511628211ull;
			}
		}

		return Hash;
	}

	FString MakeFbxStaticMeshAssetPath(const FString& SourcePath, const FString& AssetStem)
	{
		namespace fs = std::filesystem;

		const fs::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
		const std::wstring AssetFileName = FPaths::ToWide(FbxSceneUtils::SanitizeAssetFileToken(AssetStem)) + L".asset";
		const fs::path AssetPath = (SourceFsPath.parent_path() / AssetFileName).lexically_normal();
		fs::create_directories(fs::path(FPaths::ToAbsolute(AssetPath.parent_path().generic_wstring())));

		return FPaths::ToUtf8(AssetPath.generic_wstring());
	}

	FString GetAssetStemFromPath(const FString& AssetPath)
	{
		const std::filesystem::path AssetFsPath(FPaths::ToWide(FPaths::Normalize(AssetPath)));
		return FPaths::ToUtf8(AssetFsPath.stem().generic_wstring());
	}

	bool EnsureMaterialAssets(
		FResourceManager& ResourceManager,
		const FString& SourcePath,
		const FString& AssetStem,
		const TArray<FFbxMaterialSlotSource>& SlotSources,
		TMap<FString, FString>& OutMaterialAssetPaths,
		TArray<FString>* OutMaterialPaths)
	{
		FFbxMaterialExtractor MaterialExtractor;
		bool bAllOk = true;
		for (const FFbxMaterialSlotSource& SlotSource : SlotSources)
		{
			FString MaterialAssetPath;
			bAllOk &= MaterialExtractor.ResolveMaterialAssetForSlot(
				ResourceManager,
				SourcePath,
				AssetStem,
				SlotSource,
				MaterialAssetPath,
				OutMaterialPaths);

			OutMaterialAssetPaths[SlotSource.SlotName] = MaterialAssetPath.empty() ? FString("DefaultWhite") : MaterialAssetPath;
		}

		return bAllOk;
	}
}

EFbxAssetImportResult FFbxAssetImporter::ImportIfNeeded(
	FResourceManager& ResourceManager,
	const FString& SourcePath,
	TArray<FString>* OutMaterialPaths)
{
	if (!FbxSceneUtils::IsFbxSourcePath(SourcePath))
	{
		return EFbxAssetImportResult::NotFbx;
	}

	const double StartTime = FPlatformTime::Seconds();

	FFbxSceneDocument Document;
	if (!Document.Load(SourcePath))
	{
		return EFbxAssetImportResult::Failed;
	}

	const FFbxSceneImportManifest Manifest = FFbxSceneInspector().Inspect(Document);
	if ((Manifest.bHasSkeleton || Manifest.bHasSkin) && !Manifest.SkinnedMeshNodes.empty())
	{
		FFbxSkeletonExtractResult SkeletonResult;
		if (!FFbxSkeletonExtractor().Extract(Document, Manifest, SkeletonResult))
		{
			UE_LOG("[FbxAssetImporter] Failed to extract skeleton: %s", SourcePath.c_str());
			return EFbxAssetImportResult::Failed;
		}

		FFbxSkeletalMeshExtractResult MeshResult;
		if (!FFbxSkeletalMeshExtractor().Extract(Document, Manifest, SkeletonResult, MeshResult))
		{
			UE_LOG("[FbxAssetImporter] Failed to extract skeletal mesh: %s", SourcePath.c_str());
			return EFbxAssetImportResult::Failed;
		}

		if (MeshResult.Mesh.Vertices.empty() ||
			MeshResult.Mesh.Indices.empty() ||
			SkeletonResult.Skeleton.Bones.empty() ||
			SkeletonResult.Skeleton.InverseBindPoseMatrices.size() != SkeletonResult.Skeleton.Bones.size())
		{
			UE_LOG("[FbxAssetImporter] Invalid skeletal data extracted: %s", SourcePath.c_str());
			return EFbxAssetImportResult::Failed;
		}

		const FString SkeletalMeshAssetPath = ResourceManager.MakeSkeletalMeshAssetPath(SourcePath);
		const FString SkeletonAssetPath = ResourceManager.MakeSkeletonAssetPath(SourcePath);
		const FString MeshAssetStem = GetAssetStemFromPath(SkeletalMeshAssetPath);

		TMap<FString, FString> MaterialAssetPaths;
		bool bHadFailure = !EnsureMaterialAssets(
			ResourceManager,
			SourcePath,
			MeshAssetStem,
			MeshResult.SlotSources,
			MaterialAssetPaths,
			OutMaterialPaths);

		SkeletonResult.Skeleton.PathFileName = SkeletonAssetPath;

		FSkeletalMesh MeshData = MeshResult.Mesh;
		MeshData.PathFileName = SkeletalMeshAssetPath;
		MeshData.SkeletonAssetPath = SkeletonAssetPath;
		MeshData.Bones.clear();
		MeshData.InverseBindPoseMatrices.clear();
		for (FSkeletalMeshMaterialSlot& Slot : MeshData.MaterialSlots)
		{
			auto It = MaterialAssetPaths.find(Slot.SlotName);
			Slot.MaterialAssetPath = (It != MaterialAssetPaths.end()) ? It->second : FString("DefaultWhite");
			Slot.Material = nullptr;
		}

		if (!IsSkeletonAssetValid(SourcePath, SkeletonAssetPath))
		{
			const bool bSaveSkeletonOk = BinarySerializer.SaveSkeleton(SkeletonAssetPath, SourcePath, SkeletonResult.Skeleton);
			UE_LOG("[FbxAssetImporter] Skeleton Source=%s | Asset=%s | AssetSave=%s",
				SourcePath.c_str(),
				SkeletonAssetPath.c_str(),
				bSaveSkeletonOk ? "OK" : "FAIL");

			bHadFailure |= !bSaveSkeletonOk;
		}

		if (!IsSkeletalMeshAssetValid(SourcePath, SkeletalMeshAssetPath))
		{
			const bool bSaveMeshOk = BinarySerializer.SaveSkeletalMesh(SkeletalMeshAssetPath, SourcePath, MeshData);
			UE_LOG("[FbxAssetImporter] SkeletalMesh Source=%s | Asset=%s | Skeleton=%s | AssetSave=%s",
				SourcePath.c_str(),
				SkeletalMeshAssetPath.c_str(),
				SkeletonAssetPath.c_str(),
				bSaveMeshOk ? "OK" : "FAIL");

			bHadFailure |= !bSaveMeshOk;
		}

		const bool bSkeletonRegistered = ResourceManager.RegisterSkeletonAsset(SkeletonAssetPath);
		const bool bMeshRegistered = ResourceManager.RegisterSkeletalMeshAsset(SkeletalMeshAssetPath);
		bHadFailure |= !bSkeletonRegistered || !bMeshRegistered;

		const double EndTime = FPlatformTime::Seconds();
		UE_LOG("[FbxAssetImporter] Skeletal import %s | Source=%s | Mesh=%s | Skeleton=%s | Sec=%.3f",
			(!bHadFailure && bMeshRegistered) ? "OK" : "FAILED",
			SourcePath.c_str(),
			SkeletalMeshAssetPath.c_str(),
			SkeletonAssetPath.c_str(),
			EndTime - StartTime);

		return (!bHadFailure && bMeshRegistered) ?
			EFbxAssetImportResult::ImportedSkeletalMesh :
			EFbxAssetImportResult::Failed;
	}

	if (Manifest.bHasAnimation && Manifest.MeshNodes.empty())
	{
		UE_LOG("[FbxAssetImporter] Skipped animation-only FBX: %s", SourcePath.c_str());
		return EFbxAssetImportResult::SkippedAnimationOnly;
	}

	if (!Manifest.StaticMeshNodes.empty() && !Manifest.bHasSkeleton && !Manifest.bHasSkin)
	{
		TArray<FFbxStaticMeshExtractResult> MeshResults;
		if (!FFbxStaticMeshExtractor().Extract(Document, Manifest, MeshResults))
		{
			UE_LOG("[FbxAssetImporter] No static mesh data extracted: %s", SourcePath.c_str());
			return EFbxAssetImportResult::Failed;
		}

		bool bRegisteredAny = false;
		bool bHadFailure = false;
		for (FFbxStaticMeshExtractResult& MeshResult : MeshResults)
		{
			const FString StaticMeshAssetPath = MakeFbxStaticMeshAssetPath(SourcePath, MeshResult.AssetStem);

			TMap<FString, FString> MaterialAssetPaths;
			bHadFailure |= !EnsureMaterialAssets(
				ResourceManager,
				SourcePath,
				MeshResult.AssetStem,
				MeshResult.SlotSources,
				MaterialAssetPaths,
				OutMaterialPaths);

			MeshResult.Mesh.PathFileName = StaticMeshAssetPath;
			for (FStaticMeshMaterialSlot& Slot : MeshResult.Mesh.Slots)
			{
				auto It = MaterialAssetPaths.find(Slot.SlotName);
				Slot.MaterialAssetPath = (It != MaterialAssetPaths.end()) ? It->second : FString("DefaultWhite");
				Slot.Material = nullptr;
			}

			if (!IsStaticMeshAssetValid(SourcePath, StaticMeshAssetPath))
			{
				const bool bSaveAssetOk = BinarySerializer.SaveStaticMesh(StaticMeshAssetPath, SourcePath, MeshResult.Mesh);
				UE_LOG("[FbxAssetImporter] StaticMesh Source=%s | Node=%s | Asset=%s | AssetSave=%s",
					SourcePath.c_str(),
					MeshResult.MeshNodeName.c_str(),
					StaticMeshAssetPath.c_str(),
					bSaveAssetOk ? "OK" : "FAIL");

				if (!bSaveAssetOk)
				{
					bHadFailure = true;
					continue;
				}
			}

			const bool bRegistered = ResourceManager.RegisterStaticMeshAsset(StaticMeshAssetPath);
			bRegisteredAny |= bRegistered;
			bHadFailure |= !bRegistered;
		}

		const double EndTime = FPlatformTime::Seconds();
		UE_LOG("[FbxAssetImporter] Static import %s | Source=%s | Meshes=%d | Sec=%.3f",
			bRegisteredAny ? (bHadFailure ? "PARTIAL" : "OK") : "FAILED",
			SourcePath.c_str(),
			static_cast<int32>(MeshResults.size()),
			EndTime - StartTime);

		return bRegisteredAny ? EFbxAssetImportResult::ImportedStaticMesh : EFbxAssetImportResult::Failed;
	}

	UE_LOG("[FbxAssetImporter] Skipped unsupported FBX: %s", SourcePath.c_str());
	return EFbxAssetImportResult::SkippedUnsupported;
}

bool FFbxAssetImporter::IsStaticMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const
{
	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadStaticMeshHeader(AssetPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(SourcePath);
	if (SourceWriteTime == 0 || Header.SourceFileWriteTime != SourceWriteTime)
	{
		return false;
	}

	const uint64 SourceHash = HashFileFNV1a(SourcePath);
	if (SourceHash == 0 || Header.SourceFileHash != SourceHash)
	{
		return false;
	}

	return true;
}

bool FFbxAssetImporter::IsSkeletalMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const
{
	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadSkeletalMeshHeader(AssetPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(SourcePath);
	if (SourceWriteTime == 0 || Header.SourceFileWriteTime != SourceWriteTime)
	{
		return false;
	}

	const uint64 SourceHash = HashFileFNV1a(SourcePath);
	if (SourceHash == 0 || Header.SourceFileHash != SourceHash)
	{
		return false;
	}

	return true;
}

bool FFbxAssetImporter::IsSkeletonAssetValid(const FString& SourcePath, const FString& AssetPath) const
{
	FStaticMeshBinaryHeader Header;
	if (!BinarySerializer.ReadSkeletonHeader(AssetPath, Header))
	{
		return false;
	}

	const uint64 SourceWriteTime = GetFileWriteTimeTicks(SourcePath);
	if (SourceWriteTime == 0 || Header.SourceFileWriteTime != SourceWriteTime)
	{
		return false;
	}

	const uint64 SourceHash = HashFileFNV1a(SourcePath);
	if (SourceHash == 0 || Header.SourceFileHash != SourceHash)
	{
		return false;
	}

	return true;
}
