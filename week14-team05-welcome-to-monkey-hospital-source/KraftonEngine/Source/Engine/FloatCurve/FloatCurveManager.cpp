#include "FloatCurveManager.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include "FloatCurveAsset.h"
#include "Platform/Paths.h"
#include "Asset/AssetPackage.h"
#include "Serialization/WindowsArchive.h"

UFloatCurveAsset* FFloatCurveManager::Load(const FString& Path)
{
	FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto it = LoadedCurves.find(NormalizedPath);
	if (it != LoadedCurves.end())
	{
		if (IsValid(it->second))
		{
			return it->second;
		}
		LoadedCurves.erase(it);
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath)) return nullptr;

	FWindowsBinReader Ar(NormalizedPath);
	if (!Ar.IsValid()) return nullptr;

	FAssetPackageHeader Header;
	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::FloatCurve, Header, Metadata)) return nullptr;

	UFloatCurveAsset* NewAsset = UObjectManager::Get().CreateObject<UFloatCurveAsset>();
	NewAsset->Serialize(Ar);

	if (!Ar.IsValid())
	{
		UObjectManager::Get().DestroyObject(NewAsset);
		return nullptr;
	}

	NewAsset->SetSourcePath(NormalizedPath);
	LoadedCurves.emplace(NormalizedPath, NewAsset);
	return NewAsset;
}

UFloatCurveAsset* FFloatCurveManager::Find(const FString& Path) const
{
	FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto it = LoadedCurves.find(NormalizedPath);
	if (it == LoadedCurves.end())
	{
		return nullptr;
	}
	if (IsValid(it->second))
	{
		return it->second;
	}
	return nullptr;
}

bool FFloatCurveManager::Save(UFloatCurveAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	const FString& Path = Asset->GetSourcePath();
	if (Path.empty()) return false;

	FWindowsBinWriter Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;

	FAssetImportMetadata Metadata;

	if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::FloatCurve, Metadata))
	{
		return false;
	}

	Asset->Serialize(Ar);

	return Ar.IsValid();
}


void FFloatCurveManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& Pair : LoadedCurves)
    {
        Collector.AddReferencedObject(Pair.second, "FFloatCurveManager.LoadedCurves");
    }
}

void FFloatCurveManager::ClearCache()
{
    LoadedCurves.clear();
}
