#pragma once

#include "Core/CoreMinimal.h"

enum class EImportedFbxAssetType : uint8
{
    Unknown = 0,
    StaticMesh,
    Skeleton,
    SkeletalMesh,
    AnimationSequence
};

struct FImportedFbxAssetRecord
{
    EImportedFbxAssetType Type = EImportedFbxAssetType::Unknown;
    FString AssetPath;
    FString SourcePath;
    FString Name;
    FString SkeletonSourcePath;
    uint32 BoneCount = 0;
};

class FImportedFbxAssetDiscovery
{
public:
    TArray<FImportedFbxAssetRecord> DiscoverInDirectory(const FString& DirectoryPath) const;
    TArray<FImportedFbxAssetRecord> DiscoverForSourceFbx(const FString& SourceFbxPath) const;
    bool ReadImportedAssetRecord(const FString& AssetPath, FImportedFbxAssetRecord& OutRecord) const;
};
