#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

class UAnimInstanceAsset;

class FAnimInstanceAssetManager : public TSingleton<FAnimInstanceAssetManager>
{
	friend class TSingleton<FAnimInstanceAssetManager>;

public:
	UAnimInstanceAsset* Load(const FString& Path);
	UAnimInstanceAsset* Find(const FString& Path) const;
	void RegisterAnimInstanceAsset(const FString& Path, UAnimInstanceAsset* Asset);
	bool Save(UAnimInstanceAsset* Asset);
	bool IsAnimInstanceAssetPackage(const FString& Path) const;

private:
	TMap<FString, UAnimInstanceAsset*> LoadedAssets;
};
