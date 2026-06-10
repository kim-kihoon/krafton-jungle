#include "AnimGraphManager.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"

#include "Animation/Graph/AnimGraphAsset.h"
#include "Asset/AssetPackage.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

UAnimGraphAsset* FAnimGraphManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedGraphs.find(NormalizedPath);
	if (It != LoadedGraphs.end())
	{
		if (IsValid(It->second))
		{
			return It->second;
		}
		LoadedGraphs.erase(It);
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath)) return nullptr;

	FWindowsBinReader Ar(NormalizedPath);
	if (!Ar.IsValid()) return nullptr;

	FAssetPackageHeader Header;
	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::AnimGraph, Header, Metadata)) return nullptr;

	UAnimGraphAsset* NewAsset = UObjectManager::Get().CreateObject<UAnimGraphAsset>();
	NewAsset->Serialize(Ar);

	if (!Ar.IsValid())
	{
		UObjectManager::Get().DestroyObject(NewAsset);
		return nullptr;
	}

	NewAsset->SetSourcePath(NormalizedPath);
	LoadedGraphs.emplace(NormalizedPath, NewAsset);
	return NewAsset;
}

UAnimGraphAsset* FAnimGraphManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto It = LoadedGraphs.find(NormalizedPath);
	if (It == LoadedGraphs.end())
	{
		return nullptr;
	}
	if (IsValid(It->second))
	{
		return It->second;
	}
	return nullptr;
}

bool FAnimGraphManager::Save(UAnimGraphAsset* Asset)
{
	if (!Asset) return false;

	const FString& Path = Asset->GetSourcePath();
	if (Path.empty()) return false;

	FWindowsBinWriter Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;

	FAssetImportMetadata Metadata;
	if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::AnimGraph, Metadata))
	{
		return false;
	}

	Asset->Serialize(Ar);
	return Ar.IsValid();
}

void FAnimGraphManager::RefreshAvailableGraphs()
{
	const std::filesystem::path ContentRoot = std::filesystem::path(FPaths::RootDir()) / L"Content";
	if (!std::filesystem::exists(ContentRoot)) return;

	const std::filesystem::path ProjectRoot(FPaths::RootDir());
	AvailableGraphFiles.clear();

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(ContentRoot))
	{
		if (!Entry.is_regular_file()) continue;

		std::wstring Ext = Entry.path().extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".uasset") continue;

		const FString RelPath =
			FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());

		FAssetImportMetadata Metadata;
		if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::AnimGraph, Metadata))
		{
			continue;
		}

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
		Item.FullPath    = RelPath;
		AvailableGraphFiles.push_back(std::move(Item));
	}
}


void FAnimGraphManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& Pair : LoadedGraphs)
    {
        Collector.AddReferencedObject(Pair.second, "FAnimGraphManager.LoadedGraphs");
    }
}

void FAnimGraphManager::ClearCache()
{
    LoadedGraphs.clear();
    AvailableGraphFiles.clear();
}
