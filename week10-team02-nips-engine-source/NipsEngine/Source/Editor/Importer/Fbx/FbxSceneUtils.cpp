#include "Editor/Importer/Fbx/FbxSceneUtils.h"

#include "Core/Paths.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace FbxSceneUtils
{
	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Value;
	}

	bool IsFbxSourcePath(const FString& Path)
	{
		return ToLowerAscii(std::filesystem::path(FPaths::ToWide(Path)).extension().string()) == ".fbx";
	}

	FString SanitizeAssetFileToken(FString Value)
	{
		if (Value.empty())
		{
			return "DefaultWhite";
		}

		for (char& Ch : Value)
		{
			const unsigned char Byte = static_cast<unsigned char>(Ch);
			if (!std::isalnum(Byte) &&
				Ch != '_' &&
				Ch != '-' &&
				Ch != '.')
			{
				Ch = '_';
			}
		}

		return Value;
	}

	FString GetFbxObjectName(const FbxObject* Object, const char* FallbackName)
	{
		if (Object == nullptr || Object->GetName() == nullptr || Object->GetName()[0] == '\0')
		{
			return FString(FallbackName ? FallbackName : "Unnamed");
		}

		return FString(Object->GetName());
	}

	FString GetNodeAssetBaseName(const FbxNode* Node, const FString& SourcePath)
	{
		if (Node != nullptr && Node->GetName() != nullptr && Node->GetName()[0] != '\0')
		{
			return SanitizeAssetFileToken(FString(Node->GetName()));
		}

		const std::filesystem::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
		return SanitizeAssetFileToken(FPaths::ToUtf8(SourceFsPath.stem().generic_wstring()));
	}

	FbxNode* GetMeshAssetOwnerNode(FbxNode* MeshNode, FbxNode* SceneRootNode)
	{
		if (MeshNode == nullptr)
		{
			return nullptr;
		}

		FbxNode* OwnerNode = MeshNode;
		FbxNode* ParentNode = OwnerNode->GetParent();
		while (ParentNode != nullptr && ParentNode != SceneRootNode)
		{
			OwnerNode = ParentNode;
			ParentNode = OwnerNode->GetParent();
		}

		return OwnerNode;
	}

	FString MakeUniqueAssetStem(const FString& BaseName, TMap<FString, int32>& AssetStemUseCounts)
	{
		const FString ResolvedBaseName = BaseName.empty() ? FString("UnnamedMesh") : BaseName;
		const FString StemKey = ToLowerAscii(ResolvedBaseName);

		int32& UseCount = AssetStemUseCounts[StemKey];
		if (UseCount == 0)
		{
			UseCount = 1;
			return ResolvedBaseName;
		}

		const FString UniqueStem = ResolvedBaseName + "_" + std::to_string(UseCount);
		++UseCount;
		return UniqueStem;
	}

	void TraverseFbxNodes(FbxNode* Node, const std::function<void(FbxNode*)>& Visitor)
	{
		if (Node == nullptr)
		{
			return;
		}

		Visitor(Node);

		const int32 ChildCount = static_cast<int32>(Node->GetChildCount());
		for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
		{
			TraverseFbxNodes(Node->GetChild(ChildIndex), Visitor);
		}
	}

	bool IsSkeletonNode(const FbxNode* Node)
	{
		if (Node == nullptr)
		{
			return false;
		}

		const FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
		return Attribute != nullptr && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton;
	}

	bool MeshHasSkin(FbxMesh* Mesh)
	{
		return Mesh != nullptr && Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;
	}

	int32 GetPolygonMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex, int32 MaterialCount)
	{
		if (Mesh == nullptr || MaterialCount <= 1)
		{
			return 0;
		}

		FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial();
		if (MaterialElement == nullptr)
		{
			return 0;
		}

		int32 MaterialIndex = 0;
		const FbxGeometryElement::EMappingMode MappingMode = MaterialElement->GetMappingMode();
		const FbxGeometryElement::EReferenceMode ReferenceMode = MaterialElement->GetReferenceMode();

		if (MappingMode == FbxGeometryElement::eByPolygon)
		{
			if (ReferenceMode == FbxGeometryElement::eIndexToDirect)
			{
				MaterialIndex = MaterialElement->GetIndexArray().GetAt(PolygonIndex);
			}
			else if (ReferenceMode == FbxGeometryElement::eDirect)
			{
				MaterialIndex = PolygonIndex;
			}
		}
		else if (MappingMode == FbxGeometryElement::eAllSame && MaterialElement->GetIndexArray().GetCount() > 0)
		{
			MaterialIndex = MaterialElement->GetIndexArray().GetAt(0);
		}

		if (MaterialIndex < 0 || MaterialIndex >= MaterialCount)
		{
			MaterialIndex = 0;
		}

		return MaterialIndex;
	}

	FVector ToEngineVector(const FbxVector4& Vector)
	{
		return FVector(
			static_cast<float>(Vector[0]),
			static_cast<float>(Vector[1]),
			static_cast<float>(Vector[2]));
	}

	FVector2 ToEngineVector2(const FbxVector2& Vector)
	{
		return FVector2(
			static_cast<float>(Vector[0]),
			static_cast<float>(Vector[1]));
	}

	FMatrix ToEngineMatrix(const FbxAMatrix& Matrix)
	{
		return FMatrix(
			FVector4(static_cast<float>(Matrix.Get(0, 0)), static_cast<float>(Matrix.Get(0, 1)), static_cast<float>(Matrix.Get(0, 2)), static_cast<float>(Matrix.Get(0, 3))),
			FVector4(static_cast<float>(Matrix.Get(1, 0)), static_cast<float>(Matrix.Get(1, 1)), static_cast<float>(Matrix.Get(1, 2)), static_cast<float>(Matrix.Get(1, 3))),
			FVector4(static_cast<float>(Matrix.Get(2, 0)), static_cast<float>(Matrix.Get(2, 1)), static_cast<float>(Matrix.Get(2, 2)), static_cast<float>(Matrix.Get(2, 3))),
			FVector4(static_cast<float>(Matrix.Get(3, 0)), static_cast<float>(Matrix.Get(3, 1)), static_cast<float>(Matrix.Get(3, 2)), static_cast<float>(Matrix.Get(3, 3))));
	}

	FVector2 GetPolygonVertexUV(FbxMesh* Mesh, int32 PolygonIndex, int32 PolygonVertexIndex, const char* UVSetName)
	{
		if (Mesh == nullptr || UVSetName == nullptr)
		{
			return FVector2(0.0f, 0.0f);
		}

		FbxVector2 FbxUV(0.0, 0.0);
		bool bUnmapped = false;
		const bool bHasUV = Mesh->GetPolygonVertexUV(PolygonIndex, PolygonVertexIndex, UVSetName, FbxUV, bUnmapped);
		if (!bHasUV || bUnmapped)
		{
			return FVector2(0.0f, 0.0f);
		}

		FVector2 UV = ToEngineVector2(FbxUV);
		UV.Y = 1.0f - UV.Y;
		return UV;
	}
}
