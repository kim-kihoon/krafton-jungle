#pragma once

#include "Core/CoreMinimal.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Asset/Skeleton.h"
#include "Asset/StaticMeshTypes.h"

#include <fbxsdk.h>

struct FFbxSceneImportManifest
{
	bool bHasSkeleton = false;
	bool bHasSkin = false;
	bool bHasAnimation = false;

	TArray<FbxNode*> MeshNodes;
	TArray<FbxNode*> StaticMeshNodes;
	TArray<FbxNode*> SkinnedMeshNodes;
	TArray<FbxNode*> SkeletonNodes;
	TArray<FbxAnimStack*> AnimationStacks;
};

struct FFbxMaterialSlotSource
{
	FString SlotName;
	FbxSurfaceMaterial* Material = nullptr;
};

struct FFbxStaticMeshExtractResult
{
	FString MeshNodeName;
	FString AssetStem;
	FStaticMesh Mesh;
	TArray<FFbxMaterialSlotSource> SlotSources;
};

struct FFbxSkeletonExtractResult
{
	FSkeleton Skeleton;
	TMap<FbxNode*, int32> BoneIndexMap;
};

struct FFbxSkeletalMeshExtractResult
{
	FSkeletalMesh Mesh;
	TArray<FFbxMaterialSlotSource> SlotSources;
};
