#include "Animation/AnimSequenceManager.h"

#include "Animation/AnimSequence.h"
#include "Asset/AssetPackage.h"
#include "Core/Log.h"
#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

UAnimSequence* FAnimSequenceManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = AnimSequenceCache.find(NormalizedPath);
	if (It != AnimSequenceCache.end())
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
		UE_LOG("AnimSequence load failed: package open failed. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetPackageHeader Header;
	Reader << Header;
	if (!Header.IsValid(EAssetPackageType::AnimSequence))
	{
		UE_LOG("AnimSequence load failed: invalid package header. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetImportMetadata Metadata;
	Reader << Metadata;

	UAnimSequence* AnimSequence = GUObjectArray.CreateObject<UAnimSequence>();
	AnimSequence->Serialize(Reader);

	if (!Reader.IsValid())
	{
		GUObjectArray.DestroyObject(AnimSequence);
		UE_LOG("AnimSequence load failed: package data is incomplete. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	AnimSequence->SetAssetPathFileName(NormalizedPath);
	AnimSequenceCache.emplace(NormalizedPath, AnimSequence);
	return AnimSequence;
}

UAnimSequence* FAnimSequenceManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto It = AnimSequenceCache.find(NormalizedPath);
	return It != AnimSequenceCache.end() ? It->second : nullptr;
}

void FAnimSequenceManager::RegisterAnimSequence(const FString& Path, UAnimSequence* AnimSequence)
{
	if (!AnimSequence)
	{
		return;
	}

	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	AnimSequence->SetAssetPathFileName(NormalizedPath);
	AnimSequenceCache[NormalizedPath] = AnimSequence;
}

bool FAnimSequenceManager::Save(UAnimSequence* AnimSequence)
{
	if (!AnimSequence)
	{
		return false;
	}

	const FString& Path = AnimSequence->GetAssetPathFileName();
	if (Path.empty() || Path == "None")
	{
		return false;
	}

	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	FAssetImportMetadata Metadata;
	FAssetPackage::ReadMetadata(NormalizedPath, EAssetPackageType::AnimSequence, Metadata);

	FWindowsBinWriter Writer(NormalizedPath);
	if (!Writer.IsValid())
	{
		return false;
	}

	FAssetPackageHeader Header;
	Header.Type = static_cast<uint32>(EAssetPackageType::AnimSequence);

	Writer << Header;
	Writer << Metadata;
	AnimSequence->Serialize(Writer);

	if (!Writer.IsValid())
	{
		return false;
	}

	AnimSequence->SetAssetPathFileName(NormalizedPath);
	AnimSequenceCache[NormalizedPath] = AnimSequence;
	return true;
}

bool FAnimSequenceManager::IsAnimSequencePackage(const FString& Path) const
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::AnimSequence, Metadata);
}
