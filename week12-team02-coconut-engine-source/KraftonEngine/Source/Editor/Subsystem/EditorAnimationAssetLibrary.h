#pragma once

#include "Core/CoreTypes.h"

struct FEditorAnimationAssetListItem
{
	FString DisplayName;
	FString FullPath;
	FString SkeletonPath;
};

class FEditorAnimationAssetLibrary
{
public:
	static TArray<FEditorAnimationAssetListItem> ScanSkeletonAssets();
	static TArray<FEditorAnimationAssetListItem> ScanAnimSequencesForSkeleton(const FString& SkeletonPath);
	static TArray<FEditorAnimationAssetListItem> ScanAnimInstanceAssetsForSkeleton(const FString& SkeletonPath);
	static bool IsAnimSequenceCompatibleWithSkeleton(const FString& AnimSequencePath, const FString& SkeletonPath, FString* OutActualSkeletonPath = nullptr);
	static bool IsAnimInstanceAssetCompatibleWithSkeleton(const FString& AnimInstanceAssetPath, const FString& SkeletonPath, FString* OutActualSkeletonPath = nullptr);
};
