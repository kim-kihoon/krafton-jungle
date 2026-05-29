#pragma once

#include "Core/CoreTypes.h"
#include "Mesh/MeshImportOptions.h"
#include "Mesh/MeshManager.h"

class UStaticMesh;
class USkeletalMesh;
class USkeleton;
class UAnimSequence;
struct ID3D11Device;

enum class EFbxImportAssetType : uint8
{
	StaticMesh,
	SkeletalMesh,
	AnimSequence,
};

struct FFbxImportMeshInfo
{
	int32 SourceIndex = -1;
	FString Name;
	bool bSkinned = false;
	int32 MaterialCount = 0;
	int32 PolygonCount = 0;
};

struct FFbxImportSkeletalMeshInfo
{
	int32 SourceIndex = -1;
	FString Name;
	int32 MaterialCount = 0;
};

struct FFbxImportAnimStackInfo
{
	int32 SourceIndex = -1;
	FString Name;
};

struct FFbxImportSourceInfo
{
	FString SourcePath;
	TArray<FFbxImportMeshInfo> Meshes;
	TArray<FFbxImportSkeletalMeshInfo> SkeletalMeshes;
	TArray<FFbxImportAnimStackInfo> AnimStacks;
	TArray<FString> SkeletonBoneNames;
	int32 MaterialCount = 0;
	int32 SkeletonNodeCount = 0;
};

struct FFbxImportItemRequest
{
	bool bImport = true;
	int32 SourceIndex = -1;
	FString SourceName;
	FString PackagePath;
};

struct FFbxImportRequest
{
	FString SourcePath;
	FImportOptions StaticMeshOptions = FImportOptions::Default();
	FString TargetSkeletonPath;
	bool bCombineStaticMeshes = false;
	FString CombinedStaticMeshPackagePath;
	TArray<FFbxImportItemRequest> StaticMeshes;
	TArray<FFbxImportItemRequest> SkeletalMeshes;
	TArray<FFbxImportItemRequest> AnimSequences;
	bool bRefreshAssetLists = true;
};

struct FFbxImportResult
{
	bool bSucceeded = false;
	int32 StaticMeshCount = 0;
	int32 SkeletalMeshCount = 0;
	int32 AnimSequenceCount = 0;
	TArray<FString> ImportedPackagePaths;
	TArray<FString> Messages;
};

// Editor-only FBX import path. Runtime code should load cooked .uasset files through FMeshManager.
struct FEditorFbxImportService
{
	static FString GetStaticMeshPackagePathForFbx(const FString& FbxFilePath);
	static FString GetSkeletalMeshPackagePathForFbx(const FString& FbxFilePath);
	static FString GetSkeletalMeshPackagePathForFbx(const FString& FbxFilePath, const FString& MeshNodeName, bool bSingleMesh);
	static FString GetSkeletonPackagePathForFbx(const FString& FbxFilePath);
	static FString GetAnimSequencePackagePathForFbx(const FString& FbxFilePath);
	static FString GetAnimSequencePackagePathForFbx(const FString& FbxFilePath, const FString& AnimStackName, bool bSingleStack);
	static bool DiscoverSkeletalMeshSourcesFromFbx(const FString& FbxFilePath, TArray<FString>& OutPackagePaths);
	static bool DiscoverAnimSequenceSourcesFromFbx(const FString& FbxFilePath, TArray<FString>& OutPackagePaths);
	static bool InspectFbxSource(const FString& FbxFilePath, FFbxImportSourceInfo& OutInfo);
	static bool ImportFromRequest(const FFbxImportRequest& Request, ID3D11Device* Device, FFbxImportResult& OutResult);

	static bool ImportStaticMeshFromFbx(const FString& FbxFilePath, ID3D11Device* Device, UStaticMesh*& OutStaticMesh, bool bRefreshAssetLists = true);
	static bool ImportStaticMeshFromFbx(const FString& FbxFilePath, const FImportOptions& Options, ID3D11Device* Device, UStaticMesh*& OutStaticMesh, bool bRefreshAssetLists = true);
	static bool ImportSkeletalMeshesFromFbx(const FString& FbxFilePath, ID3D11Device* Device, TArray<USkeletalMesh*>& OutSkeletalMeshes, bool bRefreshAssetLists = true);
	static bool ImportSkeletalMeshFromFbx(const FString& FbxFilePath, ID3D11Device* Device, USkeletalMesh*& OutSkeletalMesh, bool bRefreshAssetLists = true);
	static bool ImportAnimSequencesFromFbx(const FString& FbxFilePath, TArray<UAnimSequence*>& OutAnimSequences, bool bRefreshAssetLists = true);
	static bool ImportAnimSequencesFromFbx(const FString& FbxFilePath, const FString& TargetSkeletonPath, TArray<UAnimSequence*>& OutAnimSequences, bool bRefreshAssetLists = true);

	static void ScanFbxSourceFiles();
	static const TArray<FMeshAssetListItem>& GetAvailableFbxFiles() { return AvailableFbxFiles; }

private:
	static TArray<FMeshAssetListItem> AvailableFbxFiles;
};
