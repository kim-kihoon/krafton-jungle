#include "Animation/AnimInstanceAssetManager.h"

#include "Animation/AnimInstanceAsset.h"
#include "Asset/AssetPackage.h"
#include "Core/Log.h"
#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

UAnimInstanceAsset* FAnimInstanceAssetManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedAssets.find(NormalizedPath);
	if (It != LoadedAssets.end())
	{
		return It->second;
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath))
	{
		return nullptr;
	}

	FWindowsBinReader Reader(NormalizedPath);
	if (!Reader.IsValid())
	{
		UE_LOG("AnimInstanceAsset load failed: package open failed. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetPackageHeader Header;
	Reader << Header;
	if (!Header.IsValid(EAssetPackageType::AnimInstance))
	{
		UE_LOG("AnimInstanceAsset load failed: invalid package header. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetImportMetadata Metadata;
	Reader << Metadata;

	UAnimInstanceAsset* Asset = GUObjectArray.CreateObject<UAnimInstanceAsset>();
	Asset->Serialize(Reader);

	if (!Reader.IsValid())
	{
		GUObjectArray.DestroyObject(Asset);
		UE_LOG("AnimInstanceAsset load failed: package data is incomplete. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	Asset->SetAssetPathFileName(NormalizedPath);
	LoadedAssets.emplace(NormalizedPath, Asset);
	return Asset;
}

UAnimInstanceAsset* FAnimInstanceAssetManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto It = LoadedAssets.find(NormalizedPath);
	return It != LoadedAssets.end() ? It->second : nullptr;
}

void FAnimInstanceAssetManager::RegisterAnimInstanceAsset(const FString& Path, UAnimInstanceAsset* Asset)
{
	if (!Asset)
	{
		return;
	}

	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	Asset->SetAssetPathFileName(NormalizedPath);
	LoadedAssets[NormalizedPath] = Asset;
}

bool FAnimInstanceAssetManager::Save(UAnimInstanceAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	const FString& Path = Asset->GetAssetPathFileName();
	if (Path.empty() || Path == "None")
	{
		return false;
	}

	FWindowsBinWriter Writer(FPaths::MakeProjectRelative(Path));
	if (!Writer.IsValid())
	{
		return false;
	}

	FAssetPackageHeader Header;
	Header.Type = static_cast<uint32>(EAssetPackageType::AnimInstance);

	FAssetImportMetadata Metadata;
	Writer << Header;
	Writer << Metadata;

	Asset->Serialize(Writer);
	return Writer.IsValid();
}

bool FAnimInstanceAssetManager::IsAnimInstanceAssetPackage(const FString& Path) const
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::AnimInstance, Metadata);
}
