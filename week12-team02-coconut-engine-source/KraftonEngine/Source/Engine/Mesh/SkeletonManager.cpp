#include "Mesh/SkeletonManager.h"

#include "Asset/AssetPackage.h"
#include "Core/Log.h"
#include "Mesh/Skeleton.h"
#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

USkeleton* FSkeletonManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = SkeletonCache.find(NormalizedPath);
	if (It != SkeletonCache.end())
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
		UE_LOG("Skeleton load failed: package open failed. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetPackageHeader Header;
	Reader << Header;
	if (!Header.IsValid(EAssetPackageType::Skeleton))
	{
		UE_LOG("Skeleton load failed: invalid package header. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetImportMetadata Metadata;
	Reader << Metadata;

	USkeleton* Skeleton = GUObjectArray.CreateObject<USkeleton>();
	Skeleton->Serialize(Reader);

	if (!Reader.IsValid())
	{
		GUObjectArray.DestroyObject(Skeleton);
		UE_LOG("Skeleton load failed: package data is incomplete. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	Skeleton->SetAssetPathFileName(NormalizedPath);
	SkeletonCache.emplace(NormalizedPath, Skeleton);
	return Skeleton;
}

USkeleton* FSkeletonManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto It = SkeletonCache.find(NormalizedPath);
	return It != SkeletonCache.end() ? It->second : nullptr;
}

void FSkeletonManager::RegisterSkeleton(const FString& Path, USkeleton* Skeleton)
{
	if (!Skeleton)
	{
		return;
	}

	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	Skeleton->SetAssetPathFileName(NormalizedPath);
	SkeletonCache[NormalizedPath] = Skeleton;
}

bool FSkeletonManager::IsSkeletonPackage(const FString& Path) const
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::Skeleton, Metadata);
}
