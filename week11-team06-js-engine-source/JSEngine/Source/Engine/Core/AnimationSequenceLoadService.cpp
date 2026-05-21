#include "Core/AnimationSequenceLoadService.h"

#include "Asset/AnimationSequence.h"
#include "Asset/AnimationSequenceSerializer.h"
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
    bool IsFbxPath(const FString& Path)
    {
        std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
        std::wstring Extension = FsPath.extension().wstring();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
        return Extension == L".fbx";
    }

    TArray<FString> FindSiblingAnimationAssets(const FString& SourceFbxPath)
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

        const FString Prefix = FPaths::ToUtf8(SourceFsPath.stem().generic_wstring()) + "_anim_";
        auto CollectFromDirectory = [&](const std::filesystem::path& DirectoryPath)
        {
            std::error_code IterEc;
            if (!std::filesystem::exists(DirectoryPath, IterEc) || !std::filesystem::is_directory(DirectoryPath, IterEc) || IterEc)
            {
                return;
            }

            for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(DirectoryPath, IterEc))
            {
                if (IterEc)
                {
                    break;
                }

                if (!Entry.is_regular_file(IterEc) || IterEc)
                {
                    continue;
                }

                std::wstring Extension = Entry.path().extension().wstring();
                std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
                if (Extension != L".anim")
                {
                    continue;
                }

                const FString FileName = FPaths::ToUtf8(Entry.path().filename().generic_wstring());
                if (FileName.rfind(Prefix, 0) == 0)
                {
                    Result.push_back(FPaths::Normalize(FPaths::ToUtf8(Entry.path().generic_wstring())));
                }
            }
        };

        CollectFromDirectory(ParentPath);
        CollectFromDirectory(ParentPath / L"Animation");

        std::sort(Result.begin(), Result.end());
        return Result;
    }

    FString ResolveAnimationAssetSavePath(const FString& SourceFbxPath, const FString& SequenceToken)
    {
        const FString DefaultAssetPath = FAssetPathPolicy::MakeSiblingAnimationSequenceAssetPath(SourceFbxPath, SequenceToken);

        const std::filesystem::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourceFbxPath)));
        const std::filesystem::path DefaultFsPath(FPaths::ToWide(DefaultAssetPath));
        const std::filesystem::path LegacySiblingPath = SourceFsPath.parent_path() / DefaultFsPath.filename();
        const FString LegacyAssetPath = FPaths::Normalize(FPaths::ToUtf8(LegacySiblingPath.generic_wstring()));

        std::error_code Ec;
        if (std::filesystem::exists(LegacySiblingPath, Ec) && !Ec)
        {
            return LegacyAssetPath;
        }

        if (std::filesystem::exists(std::filesystem::path(FPaths::ToWide(DefaultAssetPath)), Ec) && !Ec)
        {
            return DefaultAssetPath;
        }

        return DefaultAssetPath;
    }

    void PreserveAuthoredAnimationMetadata(
        FAnimationSequenceSerializer& Serializer,
        const FString& AssetPath,
        UAnimationSequence& LoadedSequence)
    {
        std::error_code Ec;
        if (!std::filesystem::exists(std::filesystem::path(FPaths::ToWide(AssetPath)), Ec) || Ec)
        {
            return;
        }

        UAnimationSequence ExistingSequence;
        if (!Serializer.LoadAnimationSequenceAsset(AssetPath, ExistingSequence))
        {
            return;
        }

        LoadedSequence.ClearNotifies();
        for (const FAnimNotifyEvent& Notify : ExistingSequence.GetNotifies())
        {
            LoadedSequence.AddNotify(Notify);
        }
    }
}

FAnimationSequenceLoadService::FAnimationSequenceLoadService(FResourceManager& InResourceManager)
    : ResourceManager(InResourceManager)
{
}

UAnimationSequence* FAnimationSequenceLoadService::Load(const FString& Path)
{
    const FString NormalizedPath = FPaths::Normalize(Path);

    if (UAnimationSequence* FoundSequence = ResourceManager.FindAnimationSequence(NormalizedPath))
    {
        return FoundSequence;
    }

    if (FAssetPathPolicy::IsAnimationSequenceAssetPath(NormalizedPath))
    {
        return LoadAnimationAsset(NormalizedPath);
    }

    if (UAnimationSequence* ImportedAssetSequence = LoadSiblingAnimationAsset(NormalizedPath))
    {
        ResourceManager.AnimationSequenceMap[NormalizedPath] = ImportedAssetSequence;
        return ImportedAssetSequence;
    }

    if (IsFbxPath(NormalizedPath))
    {
        UE_LOG_WARNING("[AnimationSequenceLoad] Imported FBX animation asset missing. Explicit import is required. | Path=%s", NormalizedPath.c_str());
    }
    else
    {
        UE_LOG_WARNING("[AnimationSequenceLoad] Unsupported animation source path | Path=%s", NormalizedPath.c_str());
    }
    return nullptr;
}

UAnimationSequence* FAnimationSequenceLoadService::ImportFbxSource(
    const FString& Path,
    const TArray<USkeletonAsset*>* ImportedSkeletonsOverride)
{
    const FString NormalizedPath = FPaths::Normalize(Path);
    if (!IsFbxPath(NormalizedPath))
    {
        UE_LOG_WARNING("[AnimationSequenceLoad] ImportFbxSource only supports FBX | Path=%s", NormalizedPath.c_str());
        return nullptr;
    }

    return ImportFbxSourceToAsset(NormalizedPath, ImportedSkeletonsOverride);
}

UAnimationSequence* FAnimationSequenceLoadService::LoadAnimationAsset(const FString& AssetPath)
{
    UAnimationSequence* LoadedSequence = UObjectManager::Get().CreateObject<UAnimationSequence>();
    if (!ResourceManager.AnimationSequenceSerializer.LoadAnimationSequenceAsset(AssetPath, *LoadedSequence))
    {
        UObjectManager::Get().DestroyObject(LoadedSequence);
        return nullptr;
    }

    ResourceManager.AnimationSequenceMap[AssetPath] = LoadedSequence;
    if (std::find(ResourceManager.AnimationSequenceFilePaths.begin(), ResourceManager.AnimationSequenceFilePaths.end(), AssetPath)
        == ResourceManager.AnimationSequenceFilePaths.end())
    {
        ResourceManager.AnimationSequenceFilePaths.push_back(AssetPath);
    }

    const FAnimationSequence* Sequence = LoadedSequence->GetSequenceData();
    UE_LOG("[AnimationSequenceLoad] Source=AnimAsset | Path=%s | Name=%s | Notifies=%zu | Duration=%.3f",
           AssetPath.c_str(),
           Sequence ? Sequence->Name.c_str() : "",
           LoadedSequence->GetNotifies().size(),
           Sequence ? Sequence->DurationSeconds : 0.0f);
	
	LoadedSequence->ResetDirty();
    return LoadedSequence;
}

UAnimationSequence* FAnimationSequenceLoadService::LoadSiblingAnimationAsset(const FString& NormalizedPath)
{
    const TArray<FString> AssetPaths = FindSiblingAnimationAssets(NormalizedPath);
    for (const FString& AssetPath : AssetPaths)
    {
        if (UAnimationSequence* Sequence = LoadAnimationAsset(AssetPath))
        {
            return Sequence;
        }
    }

    return nullptr;
}

UAnimationSequence* FAnimationSequenceLoadService::ImportFbxSourceToAsset(
    const FString& NormalizedPath,
    const TArray<USkeletonAsset*>* ImportedSkeletonsOverride)
{
    const auto SourceStart = std::chrono::steady_clock::now();
    const TArray<USkeletonAsset*> LocalImportedSkeletons =
        ImportedSkeletonsOverride ? TArray<USkeletonAsset*>() : ResourceManager.ImportSkeletonsFromFbx(NormalizedPath);
    const TArray<USkeletonAsset*>& ImportedSkeletons =
        ImportedSkeletonsOverride ? *ImportedSkeletonsOverride : LocalImportedSkeletons;
    const USkeletonAsset* TargetSkeleton = ImportedSkeletons.empty() ? nullptr : ImportedSkeletons[0];

    FAnimationImportOptions ImportOptions;
    ImportOptions.bImportAllStacks = true;
    TArray<FAnimationSequence*> ImportedSequences = ResourceManager.FbxImporter.LoadAnimationSequences(NormalizedPath, ImportOptions);
    const auto SourceEnd = std::chrono::steady_clock::now();
    const double SourceLoadSec = std::chrono::duration<double>(SourceEnd - SourceStart).count();

    if (ImportedSequences.empty() || ImportedSequences[0] == nullptr)
    {
        UE_LOG_ERROR("[AnimationSequenceLoad] Failed import | Path=%s | FbxSec=%.6f",
                     NormalizedPath.c_str(), SourceLoadSec);
        for (FAnimationSequence* Sequence : ImportedSequences)
        {
            delete Sequence;
        }
        return nullptr;
    }

    UAnimationSequence* FirstLoadedSequence = nullptr;
    for (FAnimationSequence* Sequence : ImportedSequences)
    {
        if (!Sequence)
        {
            continue;
        }

        const FString SequenceToken = Sequence->Name.empty() ? FString("anim") : Sequence->Name;
        const FString SaveAssetPath = ResolveAnimationAssetSavePath(NormalizedPath, SequenceToken);

        if (TargetSkeleton)
        {
            Sequence->SkeletonSourcePath = FAssetPathPolicy::MakeAssetRelativePath(
                SaveAssetPath,
                TargetSkeleton->GetAssetPathFileName());
        }

        UAnimationSequence* LoadedSequence = FinalizeLoadedSequence(Sequence, SaveAssetPath);
        LoadedSequence->SetAssetPath(SaveAssetPath);
        LoadedSequence->SetSourceImportPath(NormalizedPath);
        PreserveAuthoredAnimationMetadata(ResourceManager.AnimationSequenceSerializer, SaveAssetPath, *LoadedSequence);

        const bool bSaveAssetOk = ResourceManager.AnimationSequenceSerializer.SaveAnimationSequenceAsset(SaveAssetPath, *LoadedSequence);
        UE_LOG("[AnimationSequenceLoad] Source=FBX | Path=%s | FbxSec=%.6f | AnimSave=%s | AnimPath=%s",
               NormalizedPath.c_str(),
               SourceLoadSec,
               bSaveAssetOk ? "OK" : "FAIL",
               SaveAssetPath.c_str());

        if (bSaveAssetOk)
        {
            if (!FirstLoadedSequence)
            {
                FirstLoadedSequence = LoadedSequence;
            }
            continue;
        }
    }

    if (!FirstLoadedSequence)
    {
        return nullptr;
    }

    ResourceManager.AnimationSequenceMap[NormalizedPath] = FirstLoadedSequence;
    return FirstLoadedSequence;
}

UAnimationSequence* FAnimationSequenceLoadService::FinalizeLoadedSequence(FAnimationSequence* SequenceData, const FString& CacheKey)
{
    UAnimationSequence* LoadedSequence = UObjectManager::Get().CreateObject<UAnimationSequence>();
    LoadedSequence->SetSequenceData(SequenceData);

    ResourceManager.AnimationSequenceMap[CacheKey] = LoadedSequence;
    if (std::find(ResourceManager.AnimationSequenceFilePaths.begin(), ResourceManager.AnimationSequenceFilePaths.end(), CacheKey)
        == ResourceManager.AnimationSequenceFilePaths.end())
    {
        ResourceManager.AnimationSequenceFilePaths.push_back(CacheKey);
    }

    const FAnimationSequence* Sequence = LoadedSequence->GetSequenceData();
    UE_LOG("[AnimationSequenceLoad] Loaded | Path=%s | Name=%s | BoneTracks=%zu | ShapeKeyTracks=%zu | Duration=%.3f",
           CacheKey.c_str(),
           Sequence ? Sequence->Name.c_str() : "",
           Sequence ? Sequence->BoneTracks.size() : 0,
           Sequence ? Sequence->ShapeKeyTracks.size() : 0,
           Sequence ? Sequence->DurationSeconds : 0.0f);

    return LoadedSequence;
}
