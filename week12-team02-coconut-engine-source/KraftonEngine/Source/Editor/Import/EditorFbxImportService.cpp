#include "Editor/Import/EditorFbxImportService.h"

#include "Platform/Paths.h"

#if WITH_EDITOR
#include "Animation/AnimDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceManager.h"
#include "Asset/AssetPackage.h"
#include "Core/Log.h"
#include "Editor/Import/EditorFbxImporter.h"
#include "Materials/MaterialManager.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/Skeleton.h"
#include "Mesh/SkeletonManager.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "Object/FUObjectArray.h"
#include "Serialization/WindowsArchive.h"
#endif

#include <algorithm>
#include <cwctype>
#if WITH_EDITOR
#include <exception>
#endif
#include <filesystem>
#if WITH_EDITOR
#include <memory>
#endif

TArray<FMeshAssetListItem> FEditorFbxImportService::AvailableFbxFiles;

namespace
{
#if WITH_EDITOR
	std::wstring GetLowerExtension(const FString& Path)
	{
		std::filesystem::path SrcPath(FPaths::ToWide(Path));
		std::wstring Ext = SrcPath.extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		return Ext;
	}

	FString NormalizeProjectPath(const FString& Path)
	{
		return FPaths::MakeProjectRelative(Path);
	}

	std::filesystem::path ResolveProjectPath(const FString& Path)
	{
		std::filesystem::path FullPath(FPaths::ToWide(Path));
		if (!FullPath.is_absolute())
		{
			FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
		}
		return FullPath.lexically_normal();
	}

	bool TryGetSourceFileState(const FString& SourcePath, uint64& OutTimestamp, uint64& OutFileSize)
	{
		const std::filesystem::path FullPath = ResolveProjectPath(SourcePath);
		if (!std::filesystem::exists(FullPath) || !std::filesystem::is_regular_file(FullPath))
		{
			return false;
		}

		OutFileSize = static_cast<uint64>(std::filesystem::file_size(FullPath));
		const auto WriteTime = std::filesystem::last_write_time(FullPath);
		OutTimestamp = static_cast<uint64>(WriteTime.time_since_epoch().count());
		return true;
	}

	FAssetImportMetadata MakeImportMetadata(const FString& SourcePath)
	{
		FAssetImportMetadata Metadata;
		Metadata.SourcePath = NormalizeProjectPath(SourcePath);
		TryGetSourceFileState(SourcePath, Metadata.SourceTimestamp, Metadata.SourceFileSize);
		return Metadata;
	}

	FString BuildAdjacentPackagePath(const FString& FbxFilePath, const wchar_t* Suffix)
	{
		const std::filesystem::path FbxPath = ResolveProjectPath(FbxFilePath);
		std::filesystem::path PackagePath = FbxPath;
		PackagePath.replace_filename(FbxPath.stem().wstring() + Suffix + L".uasset");
		return NormalizeProjectPath(FPaths::ToUtf8(PackagePath.generic_wstring()));
	}

	FString SanitizeFileStem(const FString& Name)
	{
		FString Result = Name.empty() ? "None" : Name;
		for (char& Ch : Result)
		{
			const unsigned char U = static_cast<unsigned char>(Ch);
			if (U < 32 || Ch == '<' || Ch == '>' || Ch == ':' || Ch == '"' ||
				Ch == '/' || Ch == '\\' || Ch == '|' || Ch == '?' || Ch == '*')
			{
				Ch = '_';
			}
		}
		return Result.empty() ? FString("None") : Result;
	}

	FString BuildSkeletalMeshPackagePath(const FString& FbxFilePath, const FString& MeshNodeName, bool bSingleMesh)
	{
		if (bSingleMesh)
		{
			return BuildAdjacentPackagePath(FbxFilePath, L"_SkeletalMesh");
		}

		const std::filesystem::path FbxPath = ResolveProjectPath(FbxFilePath);
		const FString FbxStem = FPaths::ToUtf8(FbxPath.stem().wstring());
		const FString NodeStem = SanitizeFileStem(MeshNodeName);
		std::filesystem::path PackagePath = FbxPath;
		PackagePath.replace_filename(FPaths::ToWide(FbxStem + "_" + NodeStem + "_SkeletalMesh.uasset"));
		return NormalizeProjectPath(FPaths::ToUtf8(PackagePath.generic_wstring()));
	}

	FString BuildAnimSequencePackagePath(const FString& FbxFilePath, const FString& AnimStackName, bool bSingleStack)
	{
		if (bSingleStack)
		{
			return BuildAdjacentPackagePath(FbxFilePath, L"_AnimSequence");
		}

		const std::filesystem::path FbxPath = ResolveProjectPath(FbxFilePath);
		const FString FbxStem = FPaths::ToUtf8(FbxPath.stem().wstring());
		const FString StackStem = SanitizeFileStem(AnimStackName);
		std::filesystem::path PackagePath = FbxPath;
		PackagePath.replace_filename(FPaths::ToWide(FbxStem + "_" + StackStem + "_AnimSequence.uasset"));
		return NormalizeProjectPath(FPaths::ToUtf8(PackagePath.generic_wstring()));
	}

	TArray<FString> MakeUniqueMeshNames(const TArray<FString>& MeshNames)
	{
		TMap<FString, int32> UsedCounts;
		TArray<FString> UniqueNames;
		UniqueNames.reserve(MeshNames.size());

		for (const FString& MeshName : MeshNames)
		{
			const FString BaseName = SanitizeFileStem(MeshName);
			int32& Count = UsedCounts[BaseName];
			++Count;
			if (Count == 1)
			{
				UniqueNames.push_back(BaseName);
			}
			else
			{
				UniqueNames.push_back(BaseName + "_" + std::to_string(Count));
			}
		}

		return UniqueNames;
	}

	bool SaveSkeletonPackage(USkeleton* Skeleton, const FString& PackagePath, const FString& SourcePath)
	{
		std::filesystem::create_directories(ResolveProjectPath(PackagePath).parent_path());

		FWindowsBinWriter Writer(PackagePath);
		if (!Writer.IsValid())
		{
			UE_LOG("FBX skeleton import package save failed: could not open file. Path=%s", PackagePath.c_str());
			return false;
		}

		try
		{
			FAssetPackageHeader Header;
			Header.Type = static_cast<uint32>(EAssetPackageType::Skeleton);
			Writer << Header;

			FAssetImportMetadata Metadata = MakeImportMetadata(SourcePath);
			Writer << Metadata;

			Skeleton->Serialize(Writer);
		}
		catch (const std::exception&)
		{
			UE_LOG("FBX skeleton import package save failed: serialization threw an exception. Path=%s", PackagePath.c_str());
			return false;
		}

		return Writer.IsValid();
	}

	bool SaveStaticMeshPackage(UStaticMesh* StaticMesh, const FString& PackagePath, const FString& SourcePath)
	{
		std::filesystem::create_directories(ResolveProjectPath(PackagePath).parent_path());

		FWindowsBinWriter Writer(PackagePath);
		if (!Writer.IsValid())
		{
			UE_LOG("FBX static import package save failed: could not open file. Path=%s", PackagePath.c_str());
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
			UE_LOG("FBX static import package save failed: serialization threw an exception. Path=%s", PackagePath.c_str());
			return false;
		}

		return Writer.IsValid();
	}

	bool SaveSkeletalMeshPackage(USkeletalMesh* SkeletalMesh, const FString& PackagePath, const FString& SourcePath)
	{
		std::filesystem::create_directories(ResolveProjectPath(PackagePath).parent_path());

		FWindowsBinWriter Writer(PackagePath);
		if (!Writer.IsValid())
		{
			UE_LOG("FBX skeletal import package save failed: could not open file. Path=%s", PackagePath.c_str());
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
			UE_LOG("FBX skeletal import package save failed: serialization threw an exception. Path=%s", PackagePath.c_str());
			return false;
		}

		return Writer.IsValid();
	}

	bool SaveAnimSequencePackage(UAnimSequence* AnimSequence, const FString& PackagePath, const FString& SourcePath)
	{
		std::filesystem::create_directories(ResolveProjectPath(PackagePath).parent_path());

		FWindowsBinWriter Writer(PackagePath);
		if (!Writer.IsValid())
		{
			UE_LOG("FBX animation import package save failed: could not open file. Path=%s", PackagePath.c_str());
			return false;
		}

		try
		{
			FAssetPackageHeader Header;
			Header.Type = static_cast<uint32>(EAssetPackageType::AnimSequence);
			Writer << Header;

			FAssetImportMetadata Metadata = MakeImportMetadata(SourcePath);
			Writer << Metadata;

			AnimSequence->Serialize(Writer);
		}
		catch (const std::exception&)
		{
			UE_LOG("FBX animation import package save failed: serialization threw an exception. Path=%s", PackagePath.c_str());
			return false;
		}

		return Writer.IsValid();
	}

	void RefreshAssetLists()
	{
		FMeshManager::ScanMeshAssets();
		FMaterialManager::Get().ScanMaterialAssets();
		FEditorFbxImportService::ScanFbxSourceFiles();
	}

	bool ContainsSourceIndex(const TArray<int32>* SourceIndices, int32 SourceIndex)
	{
		if (!SourceIndices)
		{
			return true;
		}

		return std::find(SourceIndices->begin(), SourceIndices->end(), SourceIndex) != SourceIndices->end();
	}

	FString ResolvePackagePathOverride(const TMap<int32, FString>* PackagePathOverrides, int32 SourceIndex, const FString& DefaultPackagePath)
	{
		if (!PackagePathOverrides)
		{
			return DefaultPackagePath;
		}

		auto It = PackagePathOverrides->find(SourceIndex);
		if (It == PackagePathOverrides->end() || It->second.empty())
		{
			return DefaultPackagePath;
		}

		return NormalizeProjectPath(It->second);
	}

	void CollectFbxNodes(FbxNode* Node, TArray<FbxNode*>& OutNodes)
	{
		if (!Node)
		{
			return;
		}

		OutNodes.push_back(Node);
		for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
		{
			CollectFbxNodes(Node->GetChild(ChildIndex), OutNodes);
		}
	}

	bool LoadRawFbxScene(const FString& FilePath, const char* SceneName, FbxManager*& OutSdkManager, FbxScene*& OutScene)
	{
		OutSdkManager = nullptr;
		OutScene = nullptr;

		FbxManager* SdkManager = FbxManager::Create();
		if (!SdkManager)
		{
			return false;
		}

		FbxIOSettings* IoSettings = FbxIOSettings::Create(SdkManager, IOSROOT);
		if (!IoSettings)
		{
			SdkManager->Destroy();
			return false;
		}
		SdkManager->SetIOSettings(IoSettings);

		FbxScene* Scene = FbxScene::Create(SdkManager, SceneName);
		FbxImporter* Importer = FbxImporter::Create(SdkManager, "");
		if (!Scene || !Importer)
		{
			if (Importer) Importer->Destroy();
			SdkManager->Destroy();
			return false;
		}

		const FString FullPath = FPaths::ToUtf8(ResolveProjectPath(FilePath).generic_wstring());
		if (!Importer->Initialize(FullPath.c_str(), -1, SdkManager->GetIOSettings()))
		{
			Importer->Destroy();
			SdkManager->Destroy();
			return false;
		}

		const bool bImported = Importer->Import(Scene);
		Importer->Destroy();
		if (!bImported)
		{
			SdkManager->Destroy();
			return false;
		}

		OutSdkManager = SdkManager;
		OutScene = Scene;
		return true;
	}

	void CopySkeletonBoneNames(const TArray<FBone>& Bones, FFbxImportSourceInfo& OutInfo)
	{
		if (!OutInfo.SkeletonBoneNames.empty())
		{
			return;
		}

		OutInfo.SkeletonBoneNames.reserve(Bones.size());
		for (const FBone& Bone : Bones)
		{
			OutInfo.SkeletonBoneNames.push_back(Bone.Name);
		}
	}

	bool ImportStaticMeshFromFbxInternal(
		const FString& FbxFilePath,
		const FImportOptions& Options,
		ID3D11Device* Device,
		UStaticMesh*& OutStaticMesh,
		bool bRefreshAssetLists,
		const TArray<int32>* MeshNodeIndexFilter,
		const FString& PackagePathOverride)
	{
		OutStaticMesh = nullptr;

		if (!Device)
		{
			UE_LOG("FBX static import failed: D3D device is null. Path=%s", FbxFilePath.c_str());
			return false;
		}

		if (GetLowerExtension(FbxFilePath) != L".fbx")
		{
			UE_LOG("FBX static import failed: unsupported source extension. Path=%s", FbxFilePath.c_str());
			return false;
		}

		std::unique_ptr<FStaticMesh> NewMeshAsset = std::make_unique<FStaticMesh>();
		TArray<FStaticMaterial> ParsedMaterials;
		if (!FEditorFbxImporter::ImportStatic(FbxFilePath, &Options, *NewMeshAsset, ParsedMaterials, MeshNodeIndexFilter))
		{
			UE_LOG("FBX static import failed: importer returned empty mesh. Path=%s", FbxFilePath.c_str());
			return false;
		}

		const FString SourcePath = NormalizeProjectPath(FbxFilePath);
		const FString PackagePath = PackagePathOverride.empty()
			? FEditorFbxImportService::GetStaticMeshPackagePathForFbx(FbxFilePath)
			: NormalizeProjectPath(PackagePathOverride);

		UStaticMesh* StaticMesh = GUObjectArray.CreateObject<UStaticMesh>();
		NewMeshAsset->PathFileName = SourcePath;
		StaticMesh->SetStaticMaterials(std::move(ParsedMaterials));
		StaticMesh->SetStaticMeshAsset(NewMeshAsset.release());

		FMeshManager::StaticMeshCache.erase(PackagePath);
		if (!SaveStaticMeshPackage(StaticMesh, PackagePath, FbxFilePath))
		{
			return false;
		}

		StaticMesh->InitResources(Device);
		StaticMesh->SetAssetPathFileName(PackagePath);
		FMeshManager::StaticMeshCache[PackagePath] = StaticMesh;
		OutStaticMesh = StaticMesh;

		if (bRefreshAssetLists)
		{
			RefreshAssetLists();
		}

		UE_LOG("FBX static mesh imported successfully. Source=%s Package=%s", FbxFilePath.c_str(), PackagePath.c_str());
		return true;
	}

	bool ImportSkeletalMeshesFromFbxInternal(
		const FString& FbxFilePath,
		ID3D11Device* Device,
		TArray<USkeletalMesh*>& OutSkeletalMeshes,
		bool bRefreshAssetLists,
		const TArray<int32>* MeshIndexFilter,
		const TMap<int32, FString>* PackagePathOverrides)
	{
		OutSkeletalMeshes.clear();

		if (!Device)
		{
			UE_LOG("FBX skeletal import failed: D3D device is null. Path=%s", FbxFilePath.c_str());
			return false;
		}

		if (GetLowerExtension(FbxFilePath) != L".fbx")
		{
			UE_LOG("FBX skeletal import failed: unsupported source extension. Path=%s", FbxFilePath.c_str());
			return false;
		}

		if (!FEditorFbxImporter::Import(FbxFilePath))
		{
			UE_LOG("FBX skeletal import failed: importer returned empty mesh. Path=%s", FbxFilePath.c_str());
			return false;
		}

		const FString SourcePath = NormalizeProjectPath(FbxFilePath);
		const FString SkeletonPackagePath = FEditorFbxImportService::GetSkeletonPackagePathForFbx(SourcePath);

		std::unique_ptr<FSkeletonAsset> NewSkeletonAsset = std::make_unique<FSkeletonAsset>();
		NewSkeletonAsset->PathFileName = SourcePath;
		NewSkeletonAsset->Bones = std::move(FEditorFbxImporter::Bones);

		USkeleton* Skeleton = GUObjectArray.CreateObject<USkeleton>();
		Skeleton->SetAssetPathFileName(SkeletonPackagePath);
		Skeleton->SetSkeletonAsset(NewSkeletonAsset.release());

		if (!SaveSkeletonPackage(Skeleton, SkeletonPackagePath, SourcePath))
		{
			return false;
		}
		FSkeletonManager::Get().RegisterSkeleton(SkeletonPackagePath, Skeleton);

		TArray<FString> MeshNames;
		MeshNames.reserve(FEditorFbxImporter::ImportedSkeletalMeshes.size());
		for (const FEditorFbxImporter::FImportedSkeletalMesh& ImportedMesh : FEditorFbxImporter::ImportedSkeletalMeshes)
		{
			MeshNames.push_back(ImportedMesh.MeshName);
		}

		const bool bSingleMesh = MeshNames.size() <= 1;
		const TArray<FString> UniqueNames = MakeUniqueMeshNames(MeshNames);

		for (int32 MeshIndex = 0; MeshIndex < static_cast<int32>(FEditorFbxImporter::ImportedSkeletalMeshes.size()); ++MeshIndex)
		{
			if (!ContainsSourceIndex(MeshIndexFilter, MeshIndex))
			{
				continue;
			}

			FEditorFbxImporter::FImportedSkeletalMesh& ImportedMesh = FEditorFbxImporter::ImportedSkeletalMeshes[MeshIndex];
			const FString MeshName = MeshIndex < static_cast<int32>(UniqueNames.size()) ? UniqueNames[MeshIndex] : ImportedMesh.MeshName;
			const FString DefaultPackagePath = FEditorFbxImportService::GetSkeletalMeshPackagePathForFbx(SourcePath, MeshName, bSingleMesh);
			const FString PackagePath = ResolvePackagePathOverride(PackagePathOverrides, MeshIndex, DefaultPackagePath);

			std::unique_ptr<FSkeletalMesh> NewMeshAsset = std::make_unique<FSkeletalMesh>();
			NewMeshAsset->PathFileName = SourcePath;
			NewMeshAsset->SkeletonPath = SkeletonPackagePath;
			NewMeshAsset->MeshBindGlobal = ImportedMesh.MeshBindGlobal;
			NewMeshAsset->Vertices = std::move(ImportedMesh.Vertices);
			NewMeshAsset->Indices = std::move(ImportedMesh.Indices);
			NewMeshAsset->Sections = std::move(ImportedMesh.Sections);

			USkeletalMesh* SkeletalMesh = GUObjectArray.CreateObject<USkeletalMesh>();
			SkeletalMesh->SetSkeletalMaterials(std::move(ImportedMesh.Materials));
			SkeletalMesh->SetSkeletalMeshAsset(NewMeshAsset.release());
			SkeletalMesh->SetSkeleton(Skeleton);

			FMeshManager::SkeletalMeshCache.erase(PackagePath);
			if (!SaveSkeletalMeshPackage(SkeletalMesh, PackagePath, SourcePath))
			{
				return false;
			}

			SkeletalMesh->InitResources(Device);
			SkeletalMesh->SetAssetPathFileName(PackagePath);
			FMeshManager::SkeletalMeshCache[PackagePath] = SkeletalMesh;
			OutSkeletalMeshes.push_back(SkeletalMesh);
		}

		if (bRefreshAssetLists)
		{
			RefreshAssetLists();
		}

		UE_LOG("FBX skeletal meshes imported successfully. Source=%s Skeleton=%s MeshCount=%zu", FbxFilePath.c_str(), SkeletonPackagePath.c_str(), OutSkeletalMeshes.size());
		return !OutSkeletalMeshes.empty();
	}

	bool ImportAnimSequencesFromFbxInternal(
		const FString& FbxFilePath,
		const FString& TargetSkeletonPath,
		TArray<UAnimSequence*>& OutAnimSequences,
		bool bRefreshAssetLists,
		const TArray<int32>* SequenceIndexFilter,
		const TMap<int32, FString>* PackagePathOverrides)
	{
		OutAnimSequences.clear();

		if (GetLowerExtension(FbxFilePath) != L".fbx")
		{
			UE_LOG("FBX animation import failed: unsupported source extension. Path=%s", FbxFilePath.c_str());
			return false;
		}

		const FString SourcePath = NormalizeProjectPath(FbxFilePath);
		const bool bUseExplicitSkeleton = !TargetSkeletonPath.empty();
		const FString SkeletonPackagePath = bUseExplicitSkeleton
			? NormalizeProjectPath(TargetSkeletonPath)
			: FEditorFbxImportService::GetSkeletonPackagePathForFbx(SourcePath);

		USkeleton* Skeleton = nullptr;
		FSkeletonAsset* TargetSkeletonAsset = nullptr;
		FAssetImportMetadata SkeletonMetadata;
		if (FAssetPackage::ReadMetadata(SkeletonPackagePath, EAssetPackageType::Skeleton, SkeletonMetadata))
		{
			Skeleton = FSkeletonManager::Get().Load(SkeletonPackagePath);
			if (Skeleton && Skeleton->GetSkeletonAsset() && !Skeleton->GetSkeletonAsset()->Bones.empty())
			{
				TargetSkeletonAsset = Skeleton->GetSkeletonAsset();
			}
			else
			{
				Skeleton = nullptr;
			}
		}

		if (bUseExplicitSkeleton && !TargetSkeletonAsset)
		{
			UE_LOG("FBX animation import failed: target Skeleton is invalid. Source=%s Skeleton=%s", FbxFilePath.c_str(), SkeletonPackagePath.c_str());
			return false;
		}

		FSkeletonAsset ParsedSkeleton;
		TArray<FEditorFbxImporter::FImportedAnimSequence> ImportedSequences;
		if (!FEditorFbxImporter::ImportAnimations(SourcePath, TargetSkeletonAsset, ParsedSkeleton, ImportedSequences))
		{
			UE_LOG("FBX animation import failed: importer could not parse animation source. Path=%s", FbxFilePath.c_str());
			return false;
		}

		if (ImportedSequences.empty())
		{
			UE_LOG("FBX animation import skipped: source has no AnimStack. Path=%s", FbxFilePath.c_str());
			return false;
		}

		if (!Skeleton && !bUseExplicitSkeleton)
		{
			if (ParsedSkeleton.Bones.empty())
			{
				UE_LOG("FBX animation import failed: no valid Skeleton package and source did not contain bones. Path=%s", FbxFilePath.c_str());
				return false;
			}

			std::unique_ptr<FSkeletonAsset> NewSkeletonAsset = std::make_unique<FSkeletonAsset>();
			NewSkeletonAsset->PathFileName = SourcePath;
			NewSkeletonAsset->Bones = std::move(ParsedSkeleton.Bones);

			Skeleton = GUObjectArray.CreateObject<USkeleton>();
			Skeleton->SetAssetPathFileName(SkeletonPackagePath);
			Skeleton->SetSkeletonAsset(NewSkeletonAsset.release());

			if (!SaveSkeletonPackage(Skeleton, SkeletonPackagePath, SourcePath))
			{
				return false;
			}
			FSkeletonManager::Get().RegisterSkeleton(SkeletonPackagePath, Skeleton);
			TargetSkeletonAsset = Skeleton->GetSkeletonAsset();
		}

		if (!TargetSkeletonAsset)
		{
			TargetSkeletonAsset = Skeleton ? Skeleton->GetSkeletonAsset() : nullptr;
		}

		if (!TargetSkeletonAsset)
		{
			UE_LOG("FBX animation import failed: resolved Skeleton is invalid. Path=%s", FbxFilePath.c_str());
			return false;
		}

		TArray<FString> StackNames;
		StackNames.reserve(ImportedSequences.size());
		for (const FEditorFbxImporter::FImportedAnimSequence& ImportedSequence : ImportedSequences)
		{
			StackNames.push_back(ImportedSequence.StackName);
		}

		const bool bSingleStack = StackNames.size() <= 1;
		const TArray<FString> UniqueNames = MakeUniqueMeshNames(StackNames);

		for (int32 SequenceIndex = 0; SequenceIndex < static_cast<int32>(ImportedSequences.size()); ++SequenceIndex)
		{
			if (!ContainsSourceIndex(SequenceIndexFilter, SequenceIndex))
			{
				continue;
			}

			FEditorFbxImporter::FImportedAnimSequence& ImportedSequence = ImportedSequences[SequenceIndex];
			const FString StackName = SequenceIndex < static_cast<int32>(UniqueNames.size()) ? UniqueNames[SequenceIndex] : ImportedSequence.StackName;
			const FString DefaultPackagePath = FEditorFbxImportService::GetAnimSequencePackagePathForFbx(SourcePath, StackName, bSingleStack);
			const FString PackagePath = ResolvePackagePathOverride(PackagePathOverrides, SequenceIndex, DefaultPackagePath);

			UAnimSequence* AnimSequence = GUObjectArray.CreateObject<UAnimSequence>();
			UAnimDataModel* DataModel = GUObjectArray.CreateObject<UAnimDataModel>(AnimSequence);
			DataModel->SetPlayLength(ImportedSequence.PlayLength);
			DataModel->SetFrameRate(ImportedSequence.FrameRate);
			DataModel->SetNumberOfFrames(ImportedSequence.NumberOfFrames);
			DataModel->SetNumberOfKeys(ImportedSequence.NumberOfKeys);
			DataModel->SetBoneAnimationTracks(std::move(ImportedSequence.Tracks));

			AnimSequence->SetAssetPathFileName(PackagePath);
			AnimSequence->SetSkeleton(Skeleton);
			AnimSequence->SetSequenceLength(ImportedSequence.PlayLength);
			AnimSequence->SetRateScale(1.0f);
			AnimSequence->SetLooping(true);
			AnimSequence->SetDataModel(DataModel);

			if (!SaveAnimSequencePackage(AnimSequence, PackagePath, SourcePath))
			{
				return false;
			}

			FAnimSequenceManager::Get().RegisterAnimSequence(PackagePath, AnimSequence);
			OutAnimSequences.push_back(AnimSequence);
		}

		if (bRefreshAssetLists)
		{
			FEditorFbxImportService::ScanFbxSourceFiles();
		}

		UE_LOG("FBX animation sequences imported successfully. Source=%s Skeleton=%s SequenceCount=%zu", FbxFilePath.c_str(), SkeletonPackagePath.c_str(), OutAnimSequences.size());
		return !OutAnimSequences.empty();
	}
#else
	FString NormalizeProjectPath(const FString& Path)
	{
		return FPaths::MakeProjectRelative(Path);
	}

	std::filesystem::path ResolveProjectPath(const FString& Path)
	{
		std::filesystem::path FullPath(FPaths::ToWide(Path));
		if (!FullPath.is_absolute())
		{
			FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
		}
		return FullPath.lexically_normal();
	}

	FString BuildAdjacentPackagePath(const FString& FbxFilePath, const wchar_t* Suffix)
	{
		const std::filesystem::path FbxPath = ResolveProjectPath(FbxFilePath);
		std::filesystem::path PackagePath = FbxPath;
		PackagePath.replace_filename(FbxPath.stem().wstring() + Suffix + L".uasset");
		return NormalizeProjectPath(FPaths::ToUtf8(PackagePath.generic_wstring()));
	}

	FString SanitizeFileStem(const FString& Name)
	{
		FString Result = Name.empty() ? "None" : Name;
		for (char& Ch : Result)
		{
			const unsigned char U = static_cast<unsigned char>(Ch);
			if (U < 32 || Ch == '<' || Ch == '>' || Ch == ':' || Ch == '"' ||
				Ch == '/' || Ch == '\\' || Ch == '|' || Ch == '?' || Ch == '*')
			{
				Ch = '_';
			}
		}
		return Result.empty() ? FString("None") : Result;
	}

	FString BuildSkeletalMeshPackagePath(const FString& FbxFilePath, const FString& MeshNodeName, bool bSingleMesh)
	{
		if (bSingleMesh)
		{
			return BuildAdjacentPackagePath(FbxFilePath, L"_SkeletalMesh");
		}

		const std::filesystem::path FbxPath = ResolveProjectPath(FbxFilePath);
		const FString FbxStem = FPaths::ToUtf8(FbxPath.stem().wstring());
		const FString NodeStem = SanitizeFileStem(MeshNodeName);
		std::filesystem::path PackagePath = FbxPath;
		PackagePath.replace_filename(FPaths::ToWide(FbxStem + "_" + NodeStem + "_SkeletalMesh.uasset"));
		return NormalizeProjectPath(FPaths::ToUtf8(PackagePath.generic_wstring()));
	}

	FString BuildAnimSequencePackagePath(const FString& FbxFilePath, const FString& AnimStackName, bool bSingleStack)
	{
		if (bSingleStack)
		{
			return BuildAdjacentPackagePath(FbxFilePath, L"_AnimSequence");
		}

		const std::filesystem::path FbxPath = ResolveProjectPath(FbxFilePath);
		const FString FbxStem = FPaths::ToUtf8(FbxPath.stem().wstring());
		const FString StackStem = SanitizeFileStem(AnimStackName);
		std::filesystem::path PackagePath = FbxPath;
		PackagePath.replace_filename(FPaths::ToWide(FbxStem + "_" + StackStem + "_AnimSequence.uasset"));
		return NormalizeProjectPath(FPaths::ToUtf8(PackagePath.generic_wstring()));
	}
#endif
}

FString FEditorFbxImportService::GetStaticMeshPackagePathForFbx(const FString& FbxFilePath)
{
	return BuildAdjacentPackagePath(FbxFilePath, L"_StaticMesh");
}

FString FEditorFbxImportService::GetSkeletalMeshPackagePathForFbx(const FString& FbxFilePath)
{
	return BuildAdjacentPackagePath(FbxFilePath, L"_SkeletalMesh");
}

FString FEditorFbxImportService::GetSkeletalMeshPackagePathForFbx(const FString& FbxFilePath, const FString& MeshNodeName, bool bSingleMesh)
{
	return BuildSkeletalMeshPackagePath(FbxFilePath, MeshNodeName, bSingleMesh);
}

FString FEditorFbxImportService::GetSkeletonPackagePathForFbx(const FString& FbxFilePath)
{
	return BuildAdjacentPackagePath(FbxFilePath, L"_Skeleton");
}

FString FEditorFbxImportService::GetAnimSequencePackagePathForFbx(const FString& FbxFilePath)
{
	return BuildAdjacentPackagePath(FbxFilePath, L"_AnimSequence");
}

FString FEditorFbxImportService::GetAnimSequencePackagePathForFbx(const FString& FbxFilePath, const FString& AnimStackName, bool bSingleStack)
{
	return BuildAnimSequencePackagePath(FbxFilePath, AnimStackName, bSingleStack);
}

bool FEditorFbxImportService::DiscoverSkeletalMeshSourcesFromFbx(const FString& FbxFilePath, TArray<FString>& OutPackagePaths)
{
	OutPackagePaths.clear();
#if WITH_EDITOR
	TArray<FString> MeshNames;
	if (!FEditorFbxImporter::DiscoverMeshNames(FbxFilePath, MeshNames))
	{
		return false;
	}

	if (MeshNames.empty())
	{
		return false;
	}

	OutPackagePaths.push_back(GetSkeletalMeshPackagePathForFbx(FbxFilePath));
	return true;
#else
	(void)FbxFilePath;
	return false;
#endif
}

bool FEditorFbxImportService::DiscoverAnimSequenceSourcesFromFbx(const FString& FbxFilePath, TArray<FString>& OutPackagePaths)
{
	OutPackagePaths.clear();
#if WITH_EDITOR
	TArray<FString> StackNames;
	if (!FEditorFbxImporter::DiscoverAnimStackNames(FbxFilePath, StackNames))
	{
		return false;
	}

	if (StackNames.empty())
	{
		return true;
	}

	const bool bSingleStack = StackNames.size() <= 1;
	const TArray<FString> UniqueNames = MakeUniqueMeshNames(StackNames);
	for (int32 StackIndex = 0; StackIndex < static_cast<int32>(StackNames.size()); ++StackIndex)
	{
		const FString StackName = StackIndex < static_cast<int32>(UniqueNames.size()) ? UniqueNames[StackIndex] : StackNames[StackIndex];
		OutPackagePaths.push_back(GetAnimSequencePackagePathForFbx(FbxFilePath, StackName, bSingleStack));
	}

	return true;
#else
	(void)FbxFilePath;
	return false;
#endif
}

bool FEditorFbxImportService::InspectFbxSource(const FString& FbxFilePath, FFbxImportSourceInfo& OutInfo)
{
	OutInfo = FFbxImportSourceInfo();
	OutInfo.SourcePath = NormalizeProjectPath(FbxFilePath);
#if WITH_EDITOR
	if (GetLowerExtension(FbxFilePath) != L".fbx")
	{
		return false;
	}

	FbxManager* SdkManager = nullptr;
	FbxScene* Scene = nullptr;
	if (!LoadRawFbxScene(FbxFilePath, "FBX Import Inspect Scene", SdkManager, Scene))
	{
		return false;
	}

	OutInfo.MaterialCount = Scene->GetSrcObjectCount<FbxSurfaceMaterial>();

	if (FbxNode* RootNode = Scene->GetRootNode())
	{
		TArray<FbxNode*> Nodes;
		CollectFbxNodes(RootNode, Nodes);

		int32 MeshSourceIndex = 0;
		for (FbxNode* Node : Nodes)
		{
			if (!Node)
			{
				continue;
			}

			FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
			if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
			{
				++OutInfo.SkeletonNodeCount;
			}

			FbxMesh* Mesh = Node->GetMesh();
			if (!Mesh)
			{
				continue;
			}

			const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
			FbxSkin* Skin = SkinCount > 0 ? static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin)) : nullptr;

			FFbxImportMeshInfo MeshInfo;
			MeshInfo.SourceIndex = MeshSourceIndex++;
			MeshInfo.Name = Node->GetName() && Node->GetName()[0] ? FString(Node->GetName()) : FString("Mesh_") + std::to_string(MeshInfo.SourceIndex + 1);
			MeshInfo.bSkinned = Skin && Skin->GetClusterCount() > 0;
			MeshInfo.MaterialCount = Node->GetMaterialCount();
			MeshInfo.PolygonCount = Mesh->GetPolygonCount();
			OutInfo.Meshes.push_back(std::move(MeshInfo));
		}
	}

	const int32 StackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
	for (int32 StackIndex = 0; StackIndex < StackCount; ++StackIndex)
	{
		FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		if (!Stack)
		{
			continue;
		}

		const char* StackName = Stack->GetName();
		FFbxImportAnimStackInfo StackInfo;
		StackInfo.SourceIndex = StackIndex;
		StackInfo.Name = StackName && StackName[0] ? FString(StackName) : FString("Take_") + std::to_string(StackIndex + 1);
		OutInfo.AnimStacks.push_back(std::move(StackInfo));
	}

	SdkManager->Destroy();

	bool bHasSkinnedMesh = false;
	for (const FFbxImportMeshInfo& MeshInfo : OutInfo.Meshes)
	{
		if (MeshInfo.bSkinned)
		{
			bHasSkinnedMesh = true;
			break;
		}
	}

	const bool bMayContainSkeletalData = OutInfo.SkeletonNodeCount > 0 || bHasSkinnedMesh;
	if (bMayContainSkeletalData && FEditorFbxImporter::Import(OutInfo.SourcePath))
	{
		for (int32 MeshIndex = 0; MeshIndex < static_cast<int32>(FEditorFbxImporter::ImportedSkeletalMeshes.size()); ++MeshIndex)
		{
			const FEditorFbxImporter::FImportedSkeletalMesh& ImportedMesh = FEditorFbxImporter::ImportedSkeletalMeshes[MeshIndex];
			FFbxImportSkeletalMeshInfo MeshInfo;
			MeshInfo.SourceIndex = MeshIndex;
			MeshInfo.Name = ImportedMesh.MeshName.empty() ? FString("SkeletalMesh_") + std::to_string(MeshIndex + 1) : ImportedMesh.MeshName;
			MeshInfo.MaterialCount = static_cast<int32>(ImportedMesh.Materials.size());
			OutInfo.SkeletalMeshes.push_back(std::move(MeshInfo));
		}
		CopySkeletonBoneNames(FEditorFbxImporter::Bones, OutInfo);
	}

	if (bMayContainSkeletalData && OutInfo.SkeletonBoneNames.empty() && !OutInfo.AnimStacks.empty())
	{
		FSkeletonAsset ParsedSkeleton;
		TArray<FEditorFbxImporter::FImportedAnimSequence> IgnoredSequences;
		if (FEditorFbxImporter::ImportAnimations(OutInfo.SourcePath, nullptr, ParsedSkeleton, IgnoredSequences))
		{
			CopySkeletonBoneNames(ParsedSkeleton.Bones, OutInfo);
		}
	}

	return true;
#else
	(void)FbxFilePath;
	return false;
#endif
}

bool FEditorFbxImportService::ImportFromRequest(const FFbxImportRequest& Request, ID3D11Device* Device, FFbxImportResult& OutResult)
{
	OutResult = FFbxImportResult();
#if WITH_EDITOR
	if (Request.SourcePath.empty())
	{
		OutResult.Messages.push_back("Source path is empty.");
		return false;
	}

	bool bAttemptedImport = false;

	if (Request.bCombineStaticMeshes)
	{
		bAttemptedImport = true;
		UStaticMesh* ImportedStaticMesh = nullptr;
		if (ImportStaticMeshFromFbxInternal(Request.SourcePath, Request.StaticMeshOptions, Device, ImportedStaticMesh, false, nullptr, Request.CombinedStaticMeshPackagePath))
		{
			++OutResult.StaticMeshCount;
			if (ImportedStaticMesh)
			{
				OutResult.ImportedPackagePaths.push_back(ImportedStaticMesh->GetAssetPathFileName());
			}
		}
		else
		{
			OutResult.Messages.push_back("Combined Static Mesh import failed.");
		}
	}

	for (const FFbxImportItemRequest& Item : Request.StaticMeshes)
	{
		if (!Item.bImport)
		{
			continue;
		}

		bAttemptedImport = true;
		TArray<int32> MeshFilter = { Item.SourceIndex };
		UStaticMesh* ImportedStaticMesh = nullptr;
		if (ImportStaticMeshFromFbxInternal(Request.SourcePath, Request.StaticMeshOptions, Device, ImportedStaticMesh, false, &MeshFilter, Item.PackagePath))
		{
			++OutResult.StaticMeshCount;
			if (ImportedStaticMesh)
			{
				OutResult.ImportedPackagePaths.push_back(ImportedStaticMesh->GetAssetPathFileName());
			}
		}
		else
		{
			OutResult.Messages.push_back("Static Mesh import failed: " + Item.SourceName);
		}
	}

	TArray<int32> SkeletalMeshFilter;
	TMap<int32, FString> SkeletalMeshPackageOverrides;
	for (const FFbxImportItemRequest& Item : Request.SkeletalMeshes)
	{
		if (!Item.bImport)
		{
			continue;
		}

		SkeletalMeshFilter.push_back(Item.SourceIndex);
		SkeletalMeshPackageOverrides[Item.SourceIndex] = Item.PackagePath;
	}

	if (!SkeletalMeshFilter.empty())
	{
		bAttemptedImport = true;
		TArray<USkeletalMesh*> ImportedSkeletalMeshes;
		if (ImportSkeletalMeshesFromFbxInternal(Request.SourcePath, Device, ImportedSkeletalMeshes, false, &SkeletalMeshFilter, &SkeletalMeshPackageOverrides))
		{
			OutResult.SkeletalMeshCount += static_cast<int32>(ImportedSkeletalMeshes.size());
			for (USkeletalMesh* ImportedMesh : ImportedSkeletalMeshes)
			{
				if (ImportedMesh)
				{
					OutResult.ImportedPackagePaths.push_back(ImportedMesh->GetAssetPathFileName());
				}
			}
		}
		else
		{
			OutResult.Messages.push_back("Skeletal Mesh import failed.");
		}
	}

	TArray<int32> AnimSequenceFilter;
	TMap<int32, FString> AnimSequencePackageOverrides;
	for (const FFbxImportItemRequest& Item : Request.AnimSequences)
	{
		if (!Item.bImport)
		{
			continue;
		}

		AnimSequenceFilter.push_back(Item.SourceIndex);
		AnimSequencePackageOverrides[Item.SourceIndex] = Item.PackagePath;
	}

	if (!AnimSequenceFilter.empty())
	{
		bAttemptedImport = true;
		TArray<UAnimSequence*> ImportedSequences;
		if (ImportAnimSequencesFromFbxInternal(Request.SourcePath, Request.TargetSkeletonPath, ImportedSequences, false, &AnimSequenceFilter, &AnimSequencePackageOverrides))
		{
			OutResult.AnimSequenceCount += static_cast<int32>(ImportedSequences.size());
			for (UAnimSequence* ImportedSequence : ImportedSequences)
			{
				if (ImportedSequence)
				{
					OutResult.ImportedPackagePaths.push_back(ImportedSequence->GetAssetPathFileName());
				}
			}
		}
		else
		{
			OutResult.Messages.push_back("Animation import failed.");
		}
	}

	OutResult.bSucceeded = OutResult.StaticMeshCount > 0 || OutResult.SkeletalMeshCount > 0 || OutResult.AnimSequenceCount > 0;
	if (!bAttemptedImport)
	{
		OutResult.Messages.push_back("No FBX import items were selected.");
	}

	if (OutResult.bSucceeded && Request.bRefreshAssetLists)
	{
		RefreshAssetLists();
	}

	return OutResult.bSucceeded;
#else
	(void)Request;
	(void)Device;
	OutResult.Messages.push_back("FBX import is editor-only.");
	return false;
#endif
}

void FEditorFbxImportService::ScanFbxSourceFiles()
{
	AvailableFbxFiles.clear();

	const std::filesystem::path AssetRoot(FPaths::AssetDir());
	if (!std::filesystem::exists(AssetRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());
	for (const auto& Entry : std::filesystem::recursive_directory_iterator(AssetRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		std::wstring Ext = Path.extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".fbx") continue;

		FMeshAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.filename().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableFbxFiles.push_back(std::move(Item));
	}
}

bool FEditorFbxImportService::ImportStaticMeshFromFbx(const FString& FbxFilePath, ID3D11Device* Device, UStaticMesh*& OutStaticMesh, bool bRefreshAssetLists)
{
	return ImportStaticMeshFromFbx(FbxFilePath, FImportOptions::Default(), Device, OutStaticMesh, bRefreshAssetLists);
}

bool FEditorFbxImportService::ImportStaticMeshFromFbx(const FString& FbxFilePath, const FImportOptions& Options, ID3D11Device* Device, UStaticMesh*& OutStaticMesh, bool bRefreshAssetLists)
{
#if WITH_EDITOR
	return ImportStaticMeshFromFbxInternal(FbxFilePath, Options, Device, OutStaticMesh, bRefreshAssetLists, nullptr, FString());
#else
	(void)FbxFilePath;
	(void)Options;
	(void)Device;
	(void)bRefreshAssetLists;
	OutStaticMesh = nullptr;
	return false;
#endif
}

bool FEditorFbxImportService::ImportSkeletalMeshesFromFbx(const FString& FbxFilePath, ID3D11Device* Device, TArray<USkeletalMesh*>& OutSkeletalMeshes, bool bRefreshAssetLists)
{
#if WITH_EDITOR
	return ImportSkeletalMeshesFromFbxInternal(FbxFilePath, Device, OutSkeletalMeshes, bRefreshAssetLists, nullptr, nullptr);
#else
	(void)FbxFilePath;
	(void)Device;
	(void)bRefreshAssetLists;
	OutSkeletalMeshes.clear();
	return false;
#endif
}

bool FEditorFbxImportService::ImportSkeletalMeshFromFbx(const FString& FbxFilePath, ID3D11Device* Device, USkeletalMesh*& OutSkeletalMesh, bool bRefreshAssetLists)
{
	OutSkeletalMesh = nullptr;
	TArray<USkeletalMesh*> ImportedMeshes;
	if (!ImportSkeletalMeshesFromFbx(FbxFilePath, Device, ImportedMeshes, bRefreshAssetLists))
	{
		return false;
	}

	OutSkeletalMesh = ImportedMeshes.empty() ? nullptr : ImportedMeshes.front();
	return OutSkeletalMesh != nullptr;
}

bool FEditorFbxImportService::ImportAnimSequencesFromFbx(const FString& FbxFilePath, TArray<UAnimSequence*>& OutAnimSequences, bool bRefreshAssetLists)
{
	return ImportAnimSequencesFromFbx(FbxFilePath, FString(), OutAnimSequences, bRefreshAssetLists);
}

bool FEditorFbxImportService::ImportAnimSequencesFromFbx(const FString& FbxFilePath, const FString& TargetSkeletonPath, TArray<UAnimSequence*>& OutAnimSequences, bool bRefreshAssetLists)
{
#if WITH_EDITOR
	return ImportAnimSequencesFromFbxInternal(FbxFilePath, TargetSkeletonPath, OutAnimSequences, bRefreshAssetLists, nullptr, nullptr);
#else
	(void)FbxFilePath;
	(void)TargetSkeletonPath;
	(void)bRefreshAssetLists;
	OutAnimSequences.clear();
	return false;
#endif
}
