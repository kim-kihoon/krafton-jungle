#include "CameraShakeManager.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include "CameraShakeAsset.h"
#include "Asset/AssetPackage.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

UCameraShakeAsset* FCameraShakeManager::Load(const FString& Path)
{
	FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto it = LoadedShakes.find(NormalizedPath);
	if (it != LoadedShakes.end())
	{
		if (IsValid(it->second))
		{
			return it->second;
		}
		LoadedShakes.erase(it);
	}

	if (FAssetPackage::IsAssetPackagePath(NormalizedPath))
	{
		FWindowsBinReader Ar(NormalizedPath);
		if (!Ar.IsValid()) return nullptr;

		FAssetPackageHeader Header;
		FAssetImportMetadata Metadata;
		if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::CameraShake, Header, Metadata)) return nullptr;

		UCameraShakeAsset* NewAsset = UObjectManager::Get().CreateObject<UCameraShakeAsset>();
		NewAsset->Serialize(Ar);

		if (!Ar.IsValid())
		{
			UObjectManager::Get().DestroyObject(NewAsset);
			return nullptr;
		}

		NewAsset->SetSourcePath(NormalizedPath);
		LoadedShakes.emplace(NormalizedPath, NewAsset);
		return NewAsset;
	}

	UCameraShakeAsset* NewAsset = UObjectManager::Get().CreateObject<UCameraShakeAsset>();
	const FString FullPath = FPaths::ToUtf8(FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(NormalizedPath)));
	if (NewAsset->LoadFromFile(FullPath))
	{
		NewAsset->SetSourcePath(NormalizedPath);
		LoadedShakes.emplace(NormalizedPath, NewAsset);
		return NewAsset;
	}
	else
	{
		UObjectManager::Get().DestroyObject(NewAsset);
		return nullptr;
	}
}

UCameraShakeAsset* FCameraShakeManager::Find(const FString& Path) const
{
	FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto it = LoadedShakes.find(NormalizedPath);
	if (it == LoadedShakes.end())
	{
		return nullptr;
	}
	if (IsValid(it->second))
	{
		return it->second;
	}
	return nullptr;
}

bool FCameraShakeManager::Save(UCameraShakeAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}
	const FString& Path = Asset->GetSourcePath();
	if (Path.empty())
	{
		return false;
	}

	if (!FAssetPackage::IsAssetPackagePath(Path))
	{
		const FString FullPath = FPaths::ToUtf8(FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(Path)));
		return Asset->SaveToFile(FullPath);
	}

	FWindowsBinWriter Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;

	FAssetImportMetadata Metadata;

	if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::CameraShake, Metadata))
	{
		return false;
	}

	Asset->Serialize(Ar);

	return Ar.IsValid();
}


void FCameraShakeManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& Pair : LoadedShakes)
    {
        Collector.AddReferencedObject(Pair.second, "FCameraShakeManager.LoadedShakes");
    }
}

void FCameraShakeManager::ClearCache()
{
    LoadedShakes.clear();
}
