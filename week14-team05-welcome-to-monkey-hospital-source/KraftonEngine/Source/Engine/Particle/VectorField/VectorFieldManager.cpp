#include "Particle/VectorField/VectorFieldManager.h"

#include "Asset/AssetPackage.h"
#include "Particle/VectorField/VectorFieldAsset.h"
#include "Particle/VectorField/VectorFieldFgaImporter.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

namespace
{
	void SetError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	std::filesystem::path ResolveProjectOrAbsolutePath(const FString& Path)
	{
		std::filesystem::path Result(FPaths::ToWide(Path));
		if (!Result.is_absolute())
		{
			Result = std::filesystem::path(FPaths::RootDir()) / Result;
		}
		return Result.lexically_normal();
	}

	uint64 FileTimeToUInt64(const std::filesystem::file_time_type& FileTime)
	{
		return static_cast<uint64>(FileTime.time_since_epoch().count());
	}

	bool BuildImportMetadata(const FString& SourcePath, FAssetImportMetadata& OutMetadata)
	{
		const std::filesystem::path FullSourcePath = ResolveProjectOrAbsolutePath(SourcePath);
		if (!std::filesystem::exists(FullSourcePath) || !std::filesystem::is_regular_file(FullSourcePath))
		{
			return false;
		}

		OutMetadata.SourcePath = FPaths::MakeProjectRelative(FPaths::ToUtf8(FullSourcePath.wstring()));
		OutMetadata.SourceTimestamp = FileTimeToUInt64(std::filesystem::last_write_time(FullSourcePath));
		OutMetadata.SourceFileSize = static_cast<uint64>(std::filesystem::file_size(FullSourcePath));
		return true;
	}

	FString BuildVectorFieldPackagePathForSource(const FString& SourceFgaPath)
	{
		std::filesystem::path SourcePath = ResolveProjectOrAbsolutePath(SourceFgaPath);
		SourcePath.replace_extension(L".uasset");
		return FPaths::MakeProjectRelative(FPaths::ToUtf8(SourcePath.wstring()));
	}
}

void FVectorFieldManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& Pair : LoadedVectorFields)
	{
		Collector.AddReferencedObject(Pair.second);
	}
}

UVectorFieldAsset* FVectorFieldManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedVectorFields.find(NormalizedPath);
	if (It != LoadedVectorFields.end())
	{
		return It->second;
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath))
	{
		return nullptr;
	}

	FWindowsBinReader Ar(NormalizedPath);
	if (!Ar.IsValid())
	{
		return nullptr;
	}

	FAssetPackageHeader Header;
	Ar << Header;
	if (!Header.IsValid(EAssetPackageType::VectorField))
	{
		return nullptr;
	}

	FAssetImportMetadata Metadata;
	Ar << Metadata;

	UVectorFieldAsset* NewAsset = UObjectManager::Get().CreateObject<UVectorFieldAsset>();
	NewAsset->Serialize(Ar);

	if (!Ar.IsValid() || !NewAsset->IsValidGrid())
	{
		UObjectManager::Get().DestroyObject(NewAsset);
		return nullptr;
	}

	NewAsset->SetSourcePath(NormalizedPath);
	LoadedVectorFields.emplace(NormalizedPath, NewAsset);
	return NewAsset;
}

UVectorFieldAsset* FVectorFieldManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto It = LoadedVectorFields.find(NormalizedPath);
	return It != LoadedVectorFields.end() ? It->second : nullptr;
}

bool FVectorFieldManager::Save(UVectorFieldAsset* Asset, const FAssetImportMetadata* MetadataOverride)
{
	if (!Asset || !Asset->IsValidGrid())
	{
		return false;
	}

	const FString& Path = Asset->GetSourcePath();
	if (Path.empty())
	{
		return false;
	}

	FWindowsBinWriter Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid())
	{
		return false;
	}

	FAssetPackageHeader Header;
	Header.Type = static_cast<uint32>(EAssetPackageType::VectorField);

	FAssetImportMetadata Metadata;
	if (MetadataOverride)
	{
		Metadata = *MetadataOverride;
	}

	Ar << Header;
	Ar << Metadata;
	Asset->Serialize(Ar);

	return Ar.IsValid();
}

bool FVectorFieldManager::ImportFga(const FString& SourceFgaPath, FString& OutPackagePath, UVectorFieldAsset** OutAsset, FString* OutError)
{
	FVectorFieldFgaImportResult ImportResult;
	if (!FVectorFieldFgaImporter::LoadFgaFile(SourceFgaPath, ImportResult, OutError))
	{
		return false;
	}

	FAssetImportMetadata Metadata;
	if (!BuildImportMetadata(SourceFgaPath, Metadata))
	{
		SetError(OutError, "Failed to build vector field import metadata.");
		return false;
	}

	OutPackagePath = BuildVectorFieldPackagePathForSource(SourceFgaPath);

	UVectorFieldAsset* Asset = Find(OutPackagePath);
	if (!Asset)
	{
		Asset = UObjectManager::Get().CreateObject<UVectorFieldAsset>();
	}
	Asset->SetSourcePath(OutPackagePath);

	if (!FVectorFieldFgaImporter::FillAssetFromResult(Asset, ImportResult, OutError))
	{
		if (!Find(OutPackagePath))
		{
			UObjectManager::Get().DestroyObject(Asset);
		}
		return false;
	}

	if (!Save(Asset, &Metadata))
	{
		SetError(OutError, "Failed to save imported vector field uasset.");
		if (!Find(OutPackagePath))
		{
			UObjectManager::Get().DestroyObject(Asset);
		}
		return false;
	}

	LoadedVectorFields[FPaths::MakeProjectRelative(OutPackagePath)] = Asset;
	RefreshAvailableVectorFields();

	if (OutAsset)
	{
		*OutAsset = Asset;
	}
	return true;
}

bool FVectorFieldManager::Reimport(const FString& PackagePath, UVectorFieldAsset** OutAsset, FString* OutError)
{
	const FString NormalizedPackagePath = FPaths::MakeProjectRelative(PackagePath);

	FAssetImportMetadata OldMetadata;
	if (!FAssetPackage::ReadMetadata(NormalizedPackagePath, EAssetPackageType::VectorField, OldMetadata) || OldMetadata.SourcePath.empty())
	{
		SetError(OutError, "Vector field package has no FGA source metadata.");
		return false;
	}

	FVectorFieldFgaImportResult ImportResult;
	if (!FVectorFieldFgaImporter::LoadFgaFile(OldMetadata.SourcePath, ImportResult, OutError))
	{
		return false;
	}

	FAssetImportMetadata NewMetadata;
	if (!BuildImportMetadata(OldMetadata.SourcePath, NewMetadata))
	{
		SetError(OutError, "Failed to rebuild vector field import metadata.");
		return false;
	}

	UVectorFieldAsset* Asset = Find(NormalizedPackagePath);
	if (!Asset)
	{
		Asset = UObjectManager::Get().CreateObject<UVectorFieldAsset>();
	}
	Asset->SetSourcePath(NormalizedPackagePath);

	if (!FVectorFieldFgaImporter::FillAssetFromResult(Asset, ImportResult, OutError))
	{
		if (!Find(NormalizedPackagePath))
		{
			UObjectManager::Get().DestroyObject(Asset);
		}
		return false;
	}

	if (!Save(Asset, &NewMetadata))
	{
		SetError(OutError, "Failed to save reimported vector field package.");
		if (!Find(NormalizedPackagePath))
		{
			UObjectManager::Get().DestroyObject(Asset);
		}
		return false;
	}

	LoadedVectorFields[NormalizedPackagePath] = Asset;
	RefreshAvailableVectorFields();

	if (OutAsset)
	{
		*OutAsset = Asset;
	}
	return true;
}

void FVectorFieldManager::RefreshAvailableVectorFields()
{
	const std::filesystem::path ContentRoot = std::filesystem::path(FPaths::RootDir()) / L"Content";
	if (!std::filesystem::exists(ContentRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());
	AvailableVectorFieldFiles.clear();

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(ContentRoot))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		std::wstring Ext = Entry.path().extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".uasset")
		{
			continue;
		}

		const FString RelPath = FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());

		FAssetImportMetadata Metadata;
		if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::VectorField, Metadata))
		{
			continue;
		}

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
		Item.FullPath = RelPath;
		AvailableVectorFieldFiles.push_back(std::move(Item));
	}
}
