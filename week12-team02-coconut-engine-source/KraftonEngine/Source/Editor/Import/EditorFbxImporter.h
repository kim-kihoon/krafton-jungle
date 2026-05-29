#pragma once

#include "Core/CoreTypes.h"
#include "Animation/AnimTypes.h"
#include "Math/Matrix.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/SkeletonAsset.h"
#include "Mesh/StaticMeshAsset.h"
#include "Render/Types/VertexTypes.h"

#include <fbxsdk.h>

struct FImportOptions;

class FEditorFbxImporter
{
public:
	struct FImportedSkeletalMesh
	{
		FString MeshName;
		FMatrix MeshBindGlobal = FMatrix::Identity;
		TArray<FVertexPNCTBW> Vertices;
		TArray<uint32> Indices;
		TArray<FSkeletalMeshSection> Sections;
		TArray<FSkeletalMaterial> Materials;
	};

	/**
	 * FBX SDK로 읽은 AnimStack 하나를 .uasset으로 저장하기 전까지 들고 있는
	 * editor-only 중간 결과입니다.
	 * Runtime package 타입이 아닌 import 작업용 임시 구조체입니다.
	 */
	struct FImportedAnimSequence
	{
		FString StackName;
		float PlayLength = 0.0f;
		float FrameRate = 30.0f;
		int32 NumberOfFrames = 0;
		int32 NumberOfKeys = 0;
		TArray<FBoneAnimationTrack> Tracks;
	};

private:
	struct FMaterialInfo
	{
		FString Name;
		FVector DiffuseColor;
		FString TexturePath;
		FString NormalTexturePath;
	};
public:
	static bool Import(const FString& FilePath);
	static bool ImportStatic(const FString& FilePath, const FImportOptions* Options, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials, const TArray<int32>* MeshNodeIndexFilter = nullptr);
	static bool DiscoverMeshNames(const FString& FilePath, TArray<FString>& OutMeshNames);
	static bool DiscoverAnimStackNames(const FString& FilePath, TArray<FString>& OutStackNames);
	static bool ImportAnimations(const FString& FilePath, const FSkeletonAsset* TargetSkeleton, FSkeletonAsset& OutParsedSkeleton, TArray<FImportedAnimSequence>& OutSequences);

private:
	static bool Parse(FbxScene* Scene);
	static bool Convert();

	static void CollectNodes(FbxNode* Node, int32 depth, TArray<FbxNode*>& OutNodes);
	static void CollectMaterials(FbxScene* Scene);

	static void ParseBone(FbxScene* Scene, TArray<FbxNode*>& Nodes, TMap<FbxNode*, int32>& OutNodeToIndex);
	static void ParseSkin(TArray<FbxNode*>& Nodes, TMap<FbxNode*, int32>& NodeToIndex);
	static void ParseAnimations(FbxScene* Scene, const FSkeletonAsset& TargetSkeleton, TArray<FImportedAnimSequence>& OutSequences);

	// Helper
	static int32 GetMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex);
	static int32 FindNearestParentBoneIndex(FbxNode* Node, const TMap<FbxNode*, int32>& NodeToIndex);
	static int32 FindBoneIndexByName(const FSkeletonAsset& SkeletonAsset, const FString& BoneName);

	static void TriangulateScene(FbxScene* Scene);
	static FString ConvertToMat(const FMaterialInfo* MaterialInfo);

	static void GenerateTangents(uint32 TriIndices[]);
	static void BuildTangentsForVertexRange(const uint32 VertexStart);

public:
	// Temporary data structures for parsing
	static TArray<FVertexPNCTBW> Vertices;
	static TArray<uint32> Indices;
	static TArray<FBone> Bones;
	static TArray<FSkeletalMeshSection> Sections;
	static TArray<FImportedSkeletalMesh> ImportedSkeletalMeshes;
	static TArray<FMaterialInfo> MtlInfos;
	static TMap<FbxSurfaceMaterial*, int32> MaterialToSlotIndex;
	static TArray<FVector> TangentSums;
	static TArray<FVector> BitangentSums;
	static FString CurrentSourcePath;

};
