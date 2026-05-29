#include "Editor/Subsystem/EditorAnimationAssetLibrary.h"

#include "Animation/AnimInstanceAsset.h"
#include "Animation/AnimInstanceAssetManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceManager.h"
#include "Asset/AssetPackage.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace
{
	FString ToProjectRelativePath(const std::filesystem::path& Path)
	{
		return FPaths::MakeProjectRelative(FPaths::ToUtf8(Path.lexically_normal().generic_wstring()));
	}

	FString GetLowerExtension(const std::filesystem::path& Path)
	{
		FString Extension = FPaths::ToUtf8(Path.extension().wstring());
		std::transform(Extension.begin(), Extension.end(), Extension.begin(),
			[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		return Extension;
	}

	TArray<std::filesystem::path> GetAssetRoots()
	{
		const std::filesystem::path ProjectRoot(FPaths::RootDir());
		return {
			ProjectRoot / L"Asset",
			ProjectRoot / L"Content",
			ProjectRoot / L"Data",
		};
	}

	bool IsPackageType(const FString& PackagePath, EAssetPackageType ExpectedType)
	{
		EAssetPackageType PackageType = EAssetPackageType::Unknown;
		return FAssetPackage::GetPackageType(PackagePath, PackageType)
			&& PackageType == ExpectedType;
	}

	FString NormalizeAssetPath(const FString& Path)
	{
		return FPaths::MakeProjectRelative(Path);
	}
}

TArray<FEditorAnimationAssetListItem> FEditorAnimationAssetLibrary::ScanSkeletonAssets()
{
	TArray<FEditorAnimationAssetListItem> Items;
	TSet<FString> SeenPaths;

	for (const std::filesystem::path& Root : GetAssetRoots())
	{
		if (!std::filesystem::exists(Root))
		{
			continue;
		}

		for (const auto& Entry : std::filesystem::recursive_directory_iterator(Root))
		{
			if (!Entry.is_regular_file() || GetLowerExtension(Entry.path()) != ".uasset")
			{
				continue;
			}

			const FString PackagePath = ToProjectRelativePath(Entry.path());
			if (SeenPaths.find(PackagePath) != SeenPaths.end() || !IsPackageType(PackagePath, EAssetPackageType::Skeleton))
			{
				continue;
			}
			SeenPaths.insert(PackagePath);

			FEditorAnimationAssetListItem Item;
			Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
			Item.FullPath = PackagePath;
			Item.SkeletonPath = PackagePath;
			Items.push_back(Item);
		}
	}

	std::sort(Items.begin(), Items.end(),
		[](const FEditorAnimationAssetListItem& A, const FEditorAnimationAssetListItem& B)
		{
			return A.FullPath < B.FullPath;
		});

	return Items;
}

TArray<FEditorAnimationAssetListItem> FEditorAnimationAssetLibrary::ScanAnimSequencesForSkeleton(const FString& SkeletonPath)
{
	TArray<FEditorAnimationAssetListItem> Items;
	TSet<FString> SeenPaths;

	const FString NormalizedSkeletonPath = NormalizeAssetPath(SkeletonPath);
	if (NormalizedSkeletonPath.empty())
	{
		return Items;
	}

	for (const std::filesystem::path& Root : GetAssetRoots())
	{
		if (!std::filesystem::exists(Root))
		{
			continue;
		}

		for (const auto& Entry : std::filesystem::recursive_directory_iterator(Root))
		{
			if (!Entry.is_regular_file() || GetLowerExtension(Entry.path()) != ".uasset")
			{
				continue;
			}

			const FString PackagePath = ToProjectRelativePath(Entry.path());
			if (SeenPaths.find(PackagePath) != SeenPaths.end() || !IsPackageType(PackagePath, EAssetPackageType::AnimSequence))
			{
				continue;
			}
			SeenPaths.insert(PackagePath);

			FString ActualSkeletonPath;
			if (!IsAnimSequenceCompatibleWithSkeleton(PackagePath, NormalizedSkeletonPath, &ActualSkeletonPath))
			{
				continue;
			}

			FEditorAnimationAssetListItem Item;
			Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
			Item.FullPath = PackagePath;
			Item.SkeletonPath = ActualSkeletonPath;
			Items.push_back(Item);
		}
	}

	std::sort(Items.begin(), Items.end(),
		[](const FEditorAnimationAssetListItem& A, const FEditorAnimationAssetListItem& B)
		{
			return A.FullPath < B.FullPath;
		});

	return Items;
}

bool FEditorAnimationAssetLibrary::IsAnimSequenceCompatibleWithSkeleton(const FString& AnimSequencePath, const FString& SkeletonPath, FString* OutActualSkeletonPath)
{
	if (OutActualSkeletonPath)
	{
		OutActualSkeletonPath->clear();
	}

	const FString NormalizedSequencePath = NormalizeAssetPath(AnimSequencePath);
	const FString NormalizedSkeletonPath = NormalizeAssetPath(SkeletonPath);
	if (NormalizedSequencePath.empty() || NormalizedSkeletonPath.empty())
	{
		return false;
	}

	UAnimSequence* Sequence = FAnimSequenceManager::Get().Load(NormalizedSequencePath);
	if (!Sequence)
	{
		return false;
	}

	const FString ActualSkeletonPath = NormalizeAssetPath(Sequence->GetSkeletonPath());
	if (OutActualSkeletonPath)
	{
		*OutActualSkeletonPath = ActualSkeletonPath;
	}

	return !ActualSkeletonPath.empty() && ActualSkeletonPath == NormalizedSkeletonPath;
}

TArray<FEditorAnimationAssetListItem> FEditorAnimationAssetLibrary::ScanAnimInstanceAssetsForSkeleton(const FString& SkeletonPath)
{
	TArray<FEditorAnimationAssetListItem> Items;
	TSet<FString> SeenPaths;

	const FString NormalizedSkeletonPath = NormalizeAssetPath(SkeletonPath);
	if (NormalizedSkeletonPath.empty())
	{
		return Items;
	}

	for (const std::filesystem::path& Root : GetAssetRoots())
	{
		if (!std::filesystem::exists(Root))
		{
			continue;
		}

		for (const auto& Entry : std::filesystem::recursive_directory_iterator(Root))
		{
			if (!Entry.is_regular_file() || GetLowerExtension(Entry.path()) != ".uasset")
			{
				continue;
			}

			const FString PackagePath = ToProjectRelativePath(Entry.path());
			if (SeenPaths.find(PackagePath) != SeenPaths.end() || !IsPackageType(PackagePath, EAssetPackageType::AnimInstance))
			{
				continue;
			}
			SeenPaths.insert(PackagePath);

			FString ActualSkeletonPath;
			if (!IsAnimInstanceAssetCompatibleWithSkeleton(PackagePath, NormalizedSkeletonPath, &ActualSkeletonPath))
			{
				continue;
			}

			FEditorAnimationAssetListItem Item;
			Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
			Item.FullPath = PackagePath;
			Item.SkeletonPath = ActualSkeletonPath;
			Items.push_back(Item);
		}
	}

	std::sort(Items.begin(), Items.end(),
		[](const FEditorAnimationAssetListItem& A, const FEditorAnimationAssetListItem& B)
		{
			return A.FullPath < B.FullPath;
		});

	return Items;
}

bool FEditorAnimationAssetLibrary::IsAnimInstanceAssetCompatibleWithSkeleton(const FString& AnimInstanceAssetPath, const FString& SkeletonPath, FString* OutActualSkeletonPath)
{
	if (OutActualSkeletonPath)
	{
		*OutActualSkeletonPath = FString();
	}

	const FString NormalizedAssetPath = NormalizeAssetPath(AnimInstanceAssetPath);
	const FString NormalizedSkeletonPath = NormalizeAssetPath(SkeletonPath);
	if (NormalizedAssetPath.empty() || NormalizedSkeletonPath.empty())
	{
		return false;
	}

	UAnimInstanceAsset* Asset = FAnimInstanceAssetManager::Get().Load(NormalizedAssetPath);
	if (!Asset)
	{
		return false;
	}

	const FString ActualSkeletonPath = NormalizeAssetPath(Asset->GetSkeletonPath());
	if (OutActualSkeletonPath)
	{
		*OutActualSkeletonPath = ActualSkeletonPath;
	}

	return !ActualSkeletonPath.empty() && ActualSkeletonPath == NormalizedSkeletonPath;
}
