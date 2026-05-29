#pragma once

#include "Core/CoreTypes.h"
#include "Object/ObjectIterator.h"
#include "Render/Types/RenderTypes.h"

#include <map>
#include <string>
#include <memory>

struct FStaticMesh;
struct FSkeletalMesh;
struct FStaticMaterial;
struct FImportOptions;
struct FMeshAssetListItem;
class UStaticMesh;
class USkeletalMesh;

struct FMeshAssetListItem
{
	FString DisplayName;
	FString FullPath;
};

/** 기존 : .obj, .fbx, .uasset 전부 Load/import 지원, <SourceData Load 시 Import 수행 후 .uasset Cooked 후> 구운 에셋을 로딩 후 런타임 데이터를 캐싱*/
/** .uasset 기반 Mesh asset들을 Load하고 런타임 캐싱하는 Manager 객체 */
class FMeshManager
{
public:
	static FMeshManager& Get();
	static UStaticMesh* LoadStaticMesh(const FString& PathFileName, ID3D11Device* InDevice);
	static UStaticMesh* LoadStaticMesh(const FString& PathFileName, const FImportOptions& Options, ID3D11Device* InDevice);

	static USkeletalMesh* LoadSkeletalMesh(const FString& PathFileName , ID3D11Device* InDevice);

	// Cache-only lookup. Returns the already-loaded mesh for PathFileName, or nullptr
	// if it has not been loaded yet. Does no I/O and does not touch the GPU device.
	static UStaticMesh*   FindStaticMesh(const FString& PathFileName);
	static USkeletalMesh* FindSkeletalMesh(const FString& PathFileName);

	static const TArray<FMeshAssetListItem>& GetAvailableStaticMeshFiles() { return AvailableStaticMeshFiles; };
	static const TArray<FMeshAssetListItem>& GetAvailableSkeletalMeshFiles() { return AvailableSkeletalMeshFiles; };

	static void ScanMeshAssets();
	static FString GetStaticMeshBinaryFilePath(const FString& SourcePath);
	static FString GetSkeletalMeshBinaryFilePath(const FString& SourcePath);
	static bool IsAssetPackagePath(const FString& Path);

	static bool IsStaticMeshPackage(const FString& Path);
	static bool IsSkeletalMeshPackage(const FString& Path);

public:
	static TMap<FString, UStaticMesh*> StaticMeshCache;
	static TMap<FString, USkeletalMesh*> SkeletalMeshCache;
	static TArray<FMeshAssetListItem> AvailableStaticMeshFiles;
	static TArray<FMeshAssetListItem> AvailableSkeletalMeshFiles;
};

