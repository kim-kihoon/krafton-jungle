#pragma once

#include "Asset/BinarySerializer.h"
#include "Asset/StaticMeshTypes.h"
#include "Editor/Importer/ObjRawTypes.h"
#include <Core/ResourceTypes.h>

class FResourceManager;

class FObjStaticMeshImporter
{
public:
	FObjStaticMeshImporter() = default;
	~FObjStaticMeshImporter() = default;

	bool ImportIfNeeded(FResourceManager& ResourceManager, const FString& SourcePath, TArray<FString>* OutMaterialPaths = nullptr);
	FStaticMesh* ImportStaticMesh(const FString& Path, const FStaticMeshLoadOptions& LoadOptions);

private:
	// OBJ -> Raw Data
	bool ParseObj(const FString& Path, FObjRawData& InRawData);
	// Raw Data -> Cooked Data
	bool BuildStaticMesh(const FString& Path, FStaticMesh* InStaticMesh, FObjRawData& RawData);

	/* Helpers */
	bool ParsePositionLine(const FString& Line, FObjRawData& InRawData);
	bool ParseTexCoordLine(const FString& Line, FObjRawData& InRawData);
	bool ParseNormalLine(const FString& Line, FObjRawData& InRawData);
	void ParseMtllibLine(const FString& Line, FObjRawData& InRawData);
	void ParseUseMtlLine(const FString& Line, FString& CurrentMaterialName, FObjRawData& InRawData);
	bool ParseFaceLine(const FString& Line, const FString& CurrentMaterialName, FObjRawData& InRawData);
	bool ParseFaceVertexToken(const FString& Token, FObjRawIndex& OutIndex, FObjRawData& InRawData);
	
	FNormalVertex MakeVertex(const FObjRawIndex& RawIndex, FObjRawData& RawData) const;
	uint32 GetOrCreateVertexIndex(const FObjRawIndex& RawIndex, TMap<FObjVertexKey, uint32>& VertexMap, FStaticMesh* StaticMesh, FObjRawData& RawData);
	
	void NormalizeObjRawData(FObjRawData& RawData);
	void NormalizeRawSizeToUnitCube(FObjRawData& RawData);
	
	int32 GetOrAddMaterialSlot(const FString& MaterialName);
	FAABB BuildLocalBounds(FStaticMesh* InStaticMesh) const;
	void ComputeNormals(FObjRawData& RawData);
	bool EnsureMaterialAssets(FResourceManager& ResourceManager, const FString& SourcePath, TMap<FString, FString>& OutMaterialAssetPaths, TArray<FString>* OutMaterialPaths);
	bool IsStaticMeshAssetValid(const FString& SourcePath, const FString& AssetPath) const;

private:
	TArray<FString> BuiltMaterialSlotName;
	FBinarySerializer BinarySerializer;
};
