#include "LuaBlueprint/LuaBlueprintManager.h"

#include "Asset/AssetPackage.h"
#include "LuaBlueprint/LuaBlueprintAsset.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

ULuaBlueprintAsset* FLuaBlueprintManager::Load(const FString& Path)
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

    auto It = LoadedBlueprints.find(NormalizedPath);
    if (It != LoadedBlueprints.end())
    {
        if (IsValid(It->second))
        {
            return It->second;
        }
        LoadedBlueprints.erase(It);
    }

    if (!FAssetPackage::IsAssetPackagePath(NormalizedPath)) return nullptr;

    FWindowsBinReader Ar(NormalizedPath);
    if (!Ar.IsValid()) return nullptr;

    FAssetPackageHeader Header;
    FAssetImportMetadata Metadata;
    if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::LuaBlueprint, Header, Metadata))
    {
        return nullptr;
    }

    ULuaBlueprintAsset* NewAsset = UObjectManager::Get().CreateObject<ULuaBlueprintAsset>();
    NewAsset->Serialize(Ar);
    if (!Ar.IsValid())
    {
        UObjectManager::Get().DestroyObject(NewAsset);
        return nullptr;
    }

    NewAsset->SetSourcePath(NormalizedPath);
    if (NewAsset->IsCompileDirty())
    {
        NewAsset->Compile();
    }
    LoadedBlueprints.emplace(NormalizedPath, NewAsset);
    return NewAsset;
}

ULuaBlueprintAsset* FLuaBlueprintManager::Find(const FString& Path) const
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
    auto          It             = LoadedBlueprints.find(NormalizedPath);
    if (It == LoadedBlueprints.end())
    {
        return nullptr;
    }
    if (IsValid(It->second))
    {
        return It->second;
    }
    return nullptr;
}

bool FLuaBlueprintManager::Save(ULuaBlueprintAsset* Asset)
{
    if (!Asset) return false;
    const FString& Path = Asset->GetSourcePath();
    if (Path.empty()) return false;

    if (Asset->IsCompileDirty())
    {
        Asset->Compile();
    }

    FWindowsBinWriter Ar(FPaths::MakeProjectRelative(Path));
    if (!Ar.IsValid()) return false;

    FAssetPackageHeader Header;
    FAssetImportMetadata Metadata;
    if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::LuaBlueprint, Metadata, &Header))
    {
        return false;
    }
    Asset->Serialize(Ar);
    return Ar.IsValid();
}

void FLuaBlueprintManager::RefreshAvailableBlueprints()
{
    const std::filesystem::path ContentRoot = std::filesystem::path(FPaths::RootDir()) / L"Content";
    std::error_code ExistsError;
    if (!std::filesystem::exists(ContentRoot, ExistsError) || ExistsError) return;

    const std::filesystem::path ProjectRoot(FPaths::RootDir());
    AvailableBlueprintFiles.clear();

    std::error_code IterError;
    for (std::filesystem::recursive_directory_iterator It(
             ContentRoot, std::filesystem::directory_options::skip_permission_denied, IterError), End;
         !IterError && It != End;
         It.increment(IterError))
    {
        const std::filesystem::directory_entry& Entry = *It;
        std::error_code EntryError;
        if (!Entry.is_regular_file(EntryError) || EntryError) continue;

        std::wstring Ext = Entry.path().extension().wstring();
        std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
        if (Ext != L".uasset") continue;

        const FString        RelPath = FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());
        FAssetImportMetadata Metadata;
        if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::LuaBlueprint, Metadata))
        {
            continue;
        }

        FAssetListItem Item;
        Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
        Item.FullPath    = RelPath;
        AvailableBlueprintFiles.push_back(std::move(Item));
    }
}

void FLuaBlueprintManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& Pair : LoadedBlueprints)
    {
        Collector.AddReferencedObject(Pair.second);
    }
}

void FLuaBlueprintManager::ClearCache()
{
    LoadedBlueprints.clear();
    AvailableBlueprintFiles.clear();
}
