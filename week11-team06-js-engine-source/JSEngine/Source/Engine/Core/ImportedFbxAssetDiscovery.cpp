#include "Core/ImportedFbxAssetDiscovery.h"

#include "Asset/AnimationSequence.h"
#include "Asset/AnimationSequenceSerializer.h"
#include "Asset/BinarySerializer.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Asset/StaticMeshTypes.h"
#include "Asset/SkeletonSerializer.h"
#include "Core/AssetPathPolicy.h"
#include "Core/Paths.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace
{
    bool IsBinPath(const std::filesystem::path& Path)
    {
        std::wstring Extension = Path.extension().wstring();
        std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
        return Extension == L".bin";
    }

    FString NormalizePath(const std::filesystem::path& Path)
    {
        return FPaths::Normalize(FPaths::ToUtf8(Path.generic_wstring()));
    }

    bool IsAnimationSequenceAssetPath(const std::filesystem::path& Path)
    {
        return FAssetPathPolicy::IsAnimationSequenceAssetPath(NormalizePath(Path));
    }

    bool IsDiscoverableImportedAssetPath(const std::filesystem::path& Path)
    {
        return IsBinPath(Path) || IsAnimationSequenceAssetPath(Path);
    }

	bool IsSiblingImportedAssetForSource(const std::filesystem::path& BinaryPath, const std::filesystem::path& SourceFbxPath)
	{
        const FString Stem = FPaths::ToUtf8(SourceFbxPath.stem().generic_wstring());
        const FString FileName = FPaths::ToUtf8(BinaryPath.filename().generic_wstring());
        if (IsBinPath(BinaryPath))
        {
            if (BinaryPath.parent_path() != SourceFbxPath.parent_path())
            {
                return false;
            }

            return FileName.rfind(Stem + "_skeleton_", 0) == 0
                || FileName.rfind(Stem + "_skeletalmesh_", 0) == 0;
        }

        if (IsAnimationSequenceAssetPath(BinaryPath))
        {
            const std::filesystem::path AnimationAssetDir = SourceFbxPath.parent_path() / L"Animation";
            return BinaryPath.parent_path() == AnimationAssetDir
                && FileName.rfind(Stem + "_anim_", 0) == 0;
        }

        return false;
	}

    void AddRecordIfUnique(TArray<FImportedFbxAssetRecord>& Records, const FImportedFbxAssetRecord& Record)
    {
        if (Record.AssetPath.empty())
        {
            return;
        }

        auto ExistingIt = std::find_if(
            Records.begin(),
            Records.end(),
            [&Record](const FImportedFbxAssetRecord& Existing)
            {
                return Existing.AssetPath == Record.AssetPath;
            });

        if (ExistingIt == Records.end())
        {
            Records.push_back(Record);
        }
    }
}

bool FImportedFbxAssetDiscovery::ReadImportedAssetRecord(const FString& AssetPath, FImportedFbxAssetRecord& OutRecord) const
{
    const FString NormalizedAssetPath = FPaths::Normalize(AssetPath);

    if (FAssetPathPolicy::IsAnimationSequenceAssetPath(NormalizedAssetPath))
    {
        FAnimationSequenceSerializer Serializer;
        UAnimationSequence SequenceAsset;
        if (Serializer.LoadAnimationSequenceAsset(NormalizedAssetPath, SequenceAsset))
        {
            const FAnimationSequence* Sequence = SequenceAsset.GetSequenceData();
            OutRecord = {};
            OutRecord.Type = EImportedFbxAssetType::AnimationSequence;
            OutRecord.AssetPath = NormalizedAssetPath;
            OutRecord.SourcePath = SequenceAsset.GetSourceImportPath();
            OutRecord.Name = Sequence ? Sequence->Name : FString();
            OutRecord.SkeletonSourcePath = Sequence ? Sequence->SkeletonSourcePath : FString();
            return true;
        }

        return false;
    }

    {
        FSkeletonSerializer Serializer;
        FSkeleton Skeleton;
        if (Serializer.LoadSkeleton(NormalizedAssetPath, Skeleton))
        {
            OutRecord = {};
            OutRecord.Type = EImportedFbxAssetType::Skeleton;
            OutRecord.AssetPath = NormalizedAssetPath;
            OutRecord.Name = Skeleton.RootNodeName;
            OutRecord.BoneCount = static_cast<uint32>(Skeleton.Bones.size());
            return true;
        }
    }

    {
        FBinarySerializer Serializer;
        FStaticMesh Mesh;
        if (Serializer.LoadStaticMesh(NormalizedAssetPath, Mesh))
        {
            OutRecord = {};
            OutRecord.Type = EImportedFbxAssetType::StaticMesh;
            OutRecord.AssetPath = NormalizedAssetPath;
            OutRecord.SourcePath = Mesh.PathFileName;
            OutRecord.Name = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(NormalizedAssetPath)).stem().wstring());
            return true;
        }
    }

    {
        FBinarySerializer Serializer;
        FSkeletalMesh Mesh;
        if (Serializer.LoadSkeletalMesh(NormalizedAssetPath, Mesh))
        {
            OutRecord = {};
            OutRecord.Type = EImportedFbxAssetType::SkeletalMesh;
            OutRecord.AssetPath = NormalizedAssetPath;
            OutRecord.SourcePath = Mesh.PathFileName;
            OutRecord.SkeletonSourcePath = Mesh.SkeletonSourcePath;
            OutRecord.BoneCount = static_cast<uint32>(Mesh.Bones.size());
            return true;
        }
    }

    return false;
}

TArray<FImportedFbxAssetRecord> FImportedFbxAssetDiscovery::DiscoverInDirectory(const FString& DirectoryPath) const
{
    TArray<FImportedFbxAssetRecord> Result;

    const std::filesystem::path RootPath(FPaths::ToWide(FPaths::Normalize(DirectoryPath)));
    std::error_code Ec;
    if (!std::filesystem::exists(RootPath, Ec) || !std::filesystem::is_directory(RootPath, Ec) || Ec)
    {
        return Result;
    }

    for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(RootPath, std::filesystem::directory_options::skip_permission_denied, Ec))
    {
        if (Ec)
        {
            break;
        }

        if (!Entry.is_regular_file(Ec) || Ec || !IsBinPath(Entry.path()))
        {
            continue;
        }

        FImportedFbxAssetRecord Record;
        if (ReadImportedAssetRecord(NormalizePath(Entry.path()), Record))
        {
            Result.push_back(Record);
        }
    }

    std::sort(
        Result.begin(),
        Result.end(),
        [](const FImportedFbxAssetRecord& A, const FImportedFbxAssetRecord& B)
        {
            return A.AssetPath < B.AssetPath;
        });

    return Result;
}

TArray<FImportedFbxAssetRecord> FImportedFbxAssetDiscovery::DiscoverForSourceFbx(const FString& SourceFbxPath) const
{
    TArray<FImportedFbxAssetRecord> Result;

    const FString NormalizedSourceFbxPath = FPaths::Normalize(SourceFbxPath);
    const std::filesystem::path SourcePath(FPaths::ToWide(NormalizedSourceFbxPath));
    const std::filesystem::path ParentPath = SourcePath.parent_path();

    std::error_code Ec;
    if (ParentPath.empty() || !std::filesystem::exists(ParentPath, Ec) || Ec)
    {
        return Result;
    }

    auto DiscoverSiblingAssetsInDirectory = [&](const std::filesystem::path& DirectoryPath)
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

            if (!Entry.is_regular_file(IterEc) || IterEc || !IsDiscoverableImportedAssetPath(Entry.path()))
            {
                continue;
            }

            if (!IsSiblingImportedAssetForSource(Entry.path(), SourcePath))
            {
                continue;
            }

            FImportedFbxAssetRecord Record;
            if (ReadImportedAssetRecord(NormalizePath(Entry.path()), Record))
            {
                AddRecordIfUnique(Result, Record);
            }
        }
    };

    DiscoverSiblingAssetsInDirectory(ParentPath);
    DiscoverSiblingAssetsInDirectory(ParentPath / L"Animation");

    const FString StaticMeshBinaryPath = FAssetPathPolicy::MakeStaticMeshCacheBinaryPath(NormalizedSourceFbxPath);
    FImportedFbxAssetRecord StaticMeshRecord;
    if (ReadImportedAssetRecord(StaticMeshBinaryPath, StaticMeshRecord) &&
        StaticMeshRecord.Type == EImportedFbxAssetType::StaticMesh &&
        FPaths::Normalize(StaticMeshRecord.SourcePath) == NormalizedSourceFbxPath)
    {
        AddRecordIfUnique(Result, StaticMeshRecord);
    }

    std::sort(
        Result.begin(),
        Result.end(),
        [](const FImportedFbxAssetRecord& A, const FImportedFbxAssetRecord& B)
        {
            return A.AssetPath < B.AssetPath;
        });

    return Result;
}
