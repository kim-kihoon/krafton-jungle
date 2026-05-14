#pragma once

#include "Editor/Importer/Fbx/FbxImportTypes.h"

class FResourceManager;
class UMaterialInterface;

struct FFbxExtractedMaterial
{
	FString SlotName;
	FString DiffuseTexturePath;
	FString NormalTexturePath;
	FString SpecularTexturePath;
	UMaterialInterface* Material = nullptr;
	FbxSurfaceMaterial* SourceMaterial = nullptr;
};

class FFbxMaterialExtractor
{
public:
	FFbxExtractedMaterial ExtractMaterial(
		const FString& SourcePath,
		FbxSurfaceMaterial* FbxMaterial,
		const char* FallbackName = "Material") const;

	bool ResolveMaterialAssetForSlot(
		FResourceManager& ResourceManager,
		const FString& SourcePath,
		const FString& AssetStem,
		const FFbxMaterialSlotSource& SlotSource,
		FString& OutMaterialPath,
		TArray<FString>* OutMaterialPaths) const;
};
