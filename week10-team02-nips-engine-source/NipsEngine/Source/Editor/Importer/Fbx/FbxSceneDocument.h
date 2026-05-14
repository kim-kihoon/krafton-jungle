#pragma once

#include "Core/CoreMinimal.h"

#include <fbxsdk.h>

class FFbxSceneDocument
{
public:
	FFbxSceneDocument() = default;
	~FFbxSceneDocument();

	FFbxSceneDocument(const FFbxSceneDocument&) = delete;
	FFbxSceneDocument& operator=(const FFbxSceneDocument&) = delete;

	bool Load(const FString& InSourcePath);
	void Reset();

	const FString& GetSourcePath() const { return SourcePath; }
	FbxManager* GetManager() const { return Manager; }
	FbxScene* GetScene() const { return Scene; }
	FbxNode* GetRootNode() const;

private:
	bool CreateFbxSdkContext();
	bool ImportFbxScene();
	void ConvertSceneToEngineSpace();
	bool TriangulateFbxScene();

private:
	FString SourcePath;
	FbxManager* Manager = nullptr;
	FbxIOSettings* IOSettings = nullptr;
	FbxScene* Scene = nullptr;
};
