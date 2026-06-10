#pragma once

#include "Asset/AssetRegistry.h"
#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Object/GarbageCollection.h"

class UVectorFieldAsset;

class FVectorFieldManager : public TSingleton<FVectorFieldManager>, public FGCObject
{
	friend class TSingleton<FVectorFieldManager>;

public:
	// GC 루트 — 로드된 벡터 필드 에셋을 다른 에셋 매니저들과 동일하게 살려둔다.
	// (없으면 다음 GC sweep 이 LoadedVectorFields 의 UVectorFieldAsset 를 회수해 dangling 발생)
	const char* GetReferencerName() const override { return "FVectorFieldManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	UVectorFieldAsset* Load(const FString& Path);
	UVectorFieldAsset* Find(const FString& Path) const;

	bool Save(UVectorFieldAsset* Asset, const struct FAssetImportMetadata* MetadataOverride = nullptr);
	bool ImportFga(const FString& SourceFgaPath, FString& OutPackagePath, UVectorFieldAsset** OutAsset = nullptr, FString* OutError = nullptr);
	bool Reimport(const FString& PackagePath, UVectorFieldAsset** OutAsset = nullptr, FString* OutError = nullptr);

	void RefreshAvailableVectorFields();
	const TArray<FAssetListItem>& GetAvailableVectorFieldFiles() const { return AvailableVectorFieldFiles; }

private:
	TMap<FString, UVectorFieldAsset*> LoadedVectorFields;
	TArray<FAssetListItem> AvailableVectorFieldFiles;
};
