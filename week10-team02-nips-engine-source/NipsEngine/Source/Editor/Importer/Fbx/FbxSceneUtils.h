#pragma once

#include "Core/CoreMinimal.h"
#include "Math/Matrix.h"

#include <fbxsdk.h>

#include <functional>

namespace FbxSceneUtils
{
	FString ToLowerAscii(FString Value);
	bool IsFbxSourcePath(const FString& Path);
	FString SanitizeAssetFileToken(FString Value);
	FString GetFbxObjectName(const FbxObject* Object, const char* FallbackName);
	FString GetNodeAssetBaseName(const FbxNode* Node, const FString& SourcePath);
	FbxNode* GetMeshAssetOwnerNode(FbxNode* MeshNode, FbxNode* SceneRootNode);
	FString MakeUniqueAssetStem(const FString& BaseName, TMap<FString, int32>& AssetStemUseCounts);
	void TraverseFbxNodes(FbxNode* Node, const std::function<void(FbxNode*)>& Visitor);
	bool IsSkeletonNode(const FbxNode* Node);
	bool MeshHasSkin(FbxMesh* Mesh);
	int32 GetPolygonMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex, int32 MaterialCount);
	FVector ToEngineVector(const FbxVector4& Vector);
	FVector2 ToEngineVector2(const FbxVector2& Vector);
	FMatrix ToEngineMatrix(const FbxAMatrix& Matrix);
	FVector2 GetPolygonVertexUV(FbxMesh* Mesh, int32 PolygonIndex, int32 PolygonVertexIndex, const char* UVSetName);
}
