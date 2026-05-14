#pragma once

#include "Asset/BinarySerializer.h"

class FResourceManager;

enum class EFbxAssetImportResult
{
	NotFbx,
	ImportedStaticMesh,
	ImportedSkeletalMesh,
	SkippedAnimationOnly,
	SkippedUnsupported,
	Failed,
};

class FFbxAssetImporter
{
public:
	FFbxAssetImporter() = default;
	~FFbxAssetImporter() = default;

	EFbxAssetImportResult ImportIfNeeded(
		FResourceManager& ResourceManager,
		const FString& SourcePath,
		TArray<FString>* OutMaterialPaths = nullptr);

private:
	bool IsStaticMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const;
	bool IsSkeletalMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const;
	bool IsSkeletonAssetValid(const FString& SourcePath, const FString& AssetPath) const;

private:
	FBinarySerializer BinarySerializer;
};
