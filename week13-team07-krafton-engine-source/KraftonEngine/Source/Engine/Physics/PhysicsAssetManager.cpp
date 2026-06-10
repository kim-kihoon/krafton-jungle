#include "PhysicsAssetManager.h"

#include "Asset/AssetPackage.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Core/Logging/Log.h"
#include "Object/Object.h"
#include "Physics/PhysicsAsset.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

void FPhysicsAssetManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& Pair : LoadedPhysicsAssets)
	{
		Collector.AddReferencedObject(Pair.second);
	}
}

void FPhysicsAssetManager::ClearCache()
{
	LoadedPhysicsAssets.clear();
	AvailablePhysicsAssetFiles.clear();
}

UPhysicsAsset* FPhysicsAssetManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedPhysicsAssets.find(NormalizedPath);
	if (It != LoadedPhysicsAssets.end())
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
	if (!Header.IsValid(EAssetPackageType::PhysicsAsset))
	{
		return nullptr;
	}

	FAssetImportMetadata Metadata;
	Ar << Metadata;

	UPhysicsAsset* NewAsset = UObjectManager::Get().CreateObject<UPhysicsAsset>();
	NewAsset->Serialize(Ar);
	if (!Ar.IsValid())
	{
		UObjectManager::Get().DestroyObject(NewAsset);
		return nullptr;
	}

	NewAsset->SetSourcePath(NormalizedPath);
	LoadedPhysicsAssets.emplace(NormalizedPath, NewAsset);
	return NewAsset;
}

UPhysicsAsset* FPhysicsAssetManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto It = LoadedPhysicsAssets.find(NormalizedPath);
	return It != LoadedPhysicsAssets.end() ? It->second : nullptr;
}

UPhysicsAsset* FPhysicsAssetManager::Reload(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	LoadedPhysicsAssets.erase(NormalizedPath);
	return Load(NormalizedPath);
}

bool FPhysicsAssetManager::Save(UPhysicsAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	const FString Path = FPaths::MakeProjectRelative(Asset->GetSourcePath());
	if (Path.empty() || Path == "None")
	{
		return false;
	}

	std::filesystem::path FullPath = std::filesystem::path(FPaths::RootDir()) / FPaths::ToWide(FPaths::MakeProjectRelative(Path));
	FPaths::CreateDir(FullPath.parent_path().wstring());

	FWindowsBinWriter Ar(Path);
	if (!Ar.IsValid())
	{
		return false;
	}

	FAssetPackageHeader Header;
	Header.Type = static_cast<uint32>(EAssetPackageType::PhysicsAsset);

	FAssetImportMetadata Metadata;
	Ar << Header;
	Ar << Metadata;
	Asset->Serialize(Ar);

	if (!Ar.IsValid())
	{
		return false;
	}

	Asset->SetSourcePath(Path);
	LoadedPhysicsAssets[Path] = Asset;
	RefreshAvailablePhysicsAssets();
	return true;
}

UPhysicsAsset* FPhysicsAssetManager::CreatePhysicsAsset(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	if (NormalizedPath.empty())
	{
		return nullptr;
	}

	auto It = LoadedPhysicsAssets.find(NormalizedPath);
	if (It != LoadedPhysicsAssets.end())
	{
		return It->second;
	}

	UPhysicsAsset* NewAsset = UObjectManager::Get().CreateObject<UPhysicsAsset>();
	NewAsset->SetSourcePath(NormalizedPath);
	LoadedPhysicsAssets.emplace(NormalizedPath, NewAsset);
	return NewAsset;
}

void FPhysicsAssetManager::RefreshAvailablePhysicsAssets()
{
	AvailablePhysicsAssetFiles.clear();

	const std::filesystem::path ContentRoot = std::filesystem::path(FPaths::RootDir()) / L"Content";
	if (!std::filesystem::exists(ContentRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());
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
		if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::PhysicsAsset, Metadata))
		{
			continue;
		}

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
		Item.FullPath = RelPath;
		AvailablePhysicsAssetFiles.push_back(std::move(Item));
	}
}
