#include "Editor/Importer/Fbx/FbxSceneInspector.h"

#include "Editor/Importer/Fbx/FbxSceneDocument.h"
#include "Editor/Importer/Fbx/FbxSceneUtils.h"

#include <fbxsdk.h>

FFbxSceneImportManifest FFbxSceneInspector::Inspect(const FFbxSceneDocument& Document) const
{
	FFbxSceneImportManifest Manifest;
	FbxScene* Scene = Document.GetScene();
	if (Scene == nullptr)
	{
		return Manifest;
	}

	FbxSceneUtils::TraverseFbxNodes(Document.GetRootNode(), [&Manifest](FbxNode* Node)
	{
		if (Node == nullptr)
		{
			return;
		}

		if (FbxSceneUtils::IsSkeletonNode(Node))
		{
			Manifest.bHasSkeleton = true;
			Manifest.SkeletonNodes.push_back(Node);
		}

		FbxMesh* Mesh = Node->GetMesh();
		if (Mesh == nullptr)
		{
			return;
		}

		Manifest.MeshNodes.push_back(Node);
		if (FbxSceneUtils::MeshHasSkin(Mesh))
		{
			Manifest.bHasSkin = true;
			Manifest.SkinnedMeshNodes.push_back(Node);
		}
		else
		{
			Manifest.StaticMeshNodes.push_back(Node);
		}
	});

	const int32 AnimStackCount = static_cast<int32>(Scene->GetSrcObjectCount<FbxAnimStack>());
	Manifest.bHasAnimation = AnimStackCount > 0;
	for (int32 AnimStackIndex = 0; AnimStackIndex < AnimStackCount; ++AnimStackIndex)
	{
		if (FbxAnimStack* AnimStack = Scene->GetSrcObject<FbxAnimStack>(AnimStackIndex))
		{
			Manifest.AnimationStacks.push_back(AnimStack);
		}
	}

	return Manifest;
}
