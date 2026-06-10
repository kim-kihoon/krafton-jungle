#pragma once

#include "Core/Types/CoreTypes.h"

#include <fbxsdk.h>

struct FFbxSceneHandle
{
	// Non-owning pointer to the process-lifetime FBX SDK manager.
	// The imported scene is owned by this handle and must be destroyed explicitly.
	FbxManager* Manager = nullptr;
	FbxScene* Scene = nullptr;

	~FFbxSceneHandle();

	void Reset();

	FFbxSceneHandle() = default;
	FFbxSceneHandle(const FFbxSceneHandle&) = delete;
	FFbxSceneHandle& operator=(const FFbxSceneHandle&) = delete;
};

struct FFbxSceneLoadOptions
{
	bool bImportMaterials      = true;
	bool bImportTextures       = true;
	bool bImportLinks          = true;
	bool bImportShapes         = true;
	bool bImportGobos          = true;
	bool bImportAnimations     = true;
	bool bImportGlobalSettings = true;

	bool bAutoDetectMeterAuthoredCentimeterScene = true;

	bool bExtractEmbeddedTextures = false;
	FString EmbeddedTextureScratchDirectory;

	// Empty means keep the FBX SDK default, which imports every animation stack.
	TSet<int32> SelectedAnimationStackIndices;
};

struct FFbxSceneNormalizeOptions
{
	bool bAutoDetectMeterAuthoredCentimeterScene = true;
};

class FFbxSceneLoader
{
public:
	static bool Load(const FString& SourcePath, FFbxSceneHandle& OutScene, FString* OutMessage = nullptr);
	static bool Load(
		const FString&              SourcePath,
		const FFbxSceneLoadOptions& Options,
		FFbxSceneHandle&            OutScene,
		FString*                    OutMessage = nullptr
		);
	static void NormalizeScene(FbxScene* Scene);
	static void NormalizeScene(FbxScene* Scene, const FFbxSceneNormalizeOptions& Options);
	static void Triangulate(FbxManager* Manager, FbxScene* Scene);
};
