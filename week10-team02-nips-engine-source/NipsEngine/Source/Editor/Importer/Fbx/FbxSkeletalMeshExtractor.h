#pragma once

#include "Editor/Importer/Fbx/FbxImportTypes.h"

class FFbxSceneDocument;

class FFbxSkeletalMeshExtractor
{
public:
	bool Extract(
		const FFbxSceneDocument& Document,
		const FFbxSceneImportManifest& Manifest,
		FFbxSkeletonExtractResult& SkeletonResult,
		FFbxSkeletalMeshExtractResult& OutMesh) const;
};
