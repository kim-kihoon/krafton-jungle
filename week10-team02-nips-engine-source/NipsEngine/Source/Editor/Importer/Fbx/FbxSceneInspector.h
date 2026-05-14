#pragma once

#include "Editor/Importer/Fbx/FbxImportTypes.h"

class FFbxSceneDocument;

class FFbxSceneInspector
{
public:
	FFbxSceneImportManifest Inspect(const FFbxSceneDocument& Document) const;
};
