#pragma once

#include "Editor/Importer/Fbx/FbxImportTypes.h"

class FFbxSceneDocument;

class FFbxSkeletonExtractor
{
public:
	bool Extract(
		const FFbxSceneDocument& Document,
		const FFbxSceneImportManifest& Manifest,
		FFbxSkeletonExtractResult& OutSkeleton) const;
};
