#pragma once

#include "Editor/Importer/Fbx/FbxImportTypes.h"

class FFbxSceneDocument;

class FFbxStaticMeshExtractor
{
public:
	bool Extract(
		const FFbxSceneDocument& Document,
		const FFbxSceneImportManifest& Manifest,
		TArray<FFbxStaticMeshExtractResult>& OutMeshes) const;
};
