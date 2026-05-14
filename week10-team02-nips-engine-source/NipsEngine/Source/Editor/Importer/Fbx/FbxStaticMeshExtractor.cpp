#include "Editor/Importer/Fbx/FbxStaticMeshExtractor.h"

#include "Core/Logger.h"
#include "Editor/Importer/Fbx/FbxMaterialExtractor.h"
#include "Editor/Importer/Fbx/FbxSceneDocument.h"
#include "Editor/Importer/Fbx/FbxSceneUtils.h"
#include "Math/Utils.h"

#include <fbxsdk.h>

#include <algorithm>
#include <functional>

namespace
{
	size_t CombineHash(size_t Seed, size_t Value)
	{
		return Seed ^ (Value + 0x9e3779b9u + (Seed << 6) + (Seed >> 2));
	}

	size_t HashColor(const FColor& Color)
	{
		size_t Hash = std::hash<float>{}(Color.R);
		Hash = CombineHash(Hash, std::hash<float>{}(Color.G));
		Hash = CombineHash(Hash, std::hash<float>{}(Color.B));
		Hash = CombineHash(Hash, std::hash<float>{}(Color.A));
		return Hash;
	}

	struct FFbxStaticVertexKey
	{
		FVector Position;
		FColor Color;
		FVector Normal;
		FVector2 UVs;

		bool operator==(const FFbxStaticVertexKey& Other) const
		{
			return Position == Other.Position
				&& Color.R == Other.Color.R
				&& Color.G == Other.Color.G
				&& Color.B == Other.Color.B
				&& Color.A == Other.Color.A
				&& Normal == Other.Normal
				&& UVs == Other.UVs;
		}
	};

	struct FFbxStaticVertexKeyHasher
	{
		size_t operator()(const FFbxStaticVertexKey& Key) const noexcept
		{
			size_t Hash = std::hash<FVector>{}(Key.Position);
			Hash = CombineHash(Hash, HashColor(Key.Color));
			Hash = CombineHash(Hash, std::hash<FVector>{}(Key.Normal));
			Hash = CombineHash(Hash, std::hash<FVector2>{}(Key.UVs));
			return Hash;
		}
	};

	struct FFbxStaticMeshBuild
	{
		FString MeshNodeName;
		FString AssetStem;
		FStaticMesh Mesh;
		TArray<FString> BuiltSlotNames;
		TArray<TArray<uint32>> IndicesBySlot;
		TArray<FFbxMaterialSlotSource> SlotSources;
		TMap<FFbxStaticVertexKey, uint32, FFbxStaticVertexKeyHasher> VertexMap;
		TArray<FVector> TangentSums;
		TArray<FVector> BitangentSums;
	};

	int32 GetOrAddMaterialSlot(FFbxStaticMeshBuild& MeshBuild, const FString& SlotName, FbxSurfaceMaterial* Material)
	{
		const FString ResolvedSlotName = SlotName.empty() ? FString("DefaultWhite") : SlotName;

		for (int32 Index = 0; Index < static_cast<int32>(MeshBuild.BuiltSlotNames.size()); ++Index)
		{
			if (MeshBuild.BuiltSlotNames[Index] == ResolvedSlotName)
			{
				if (MeshBuild.SlotSources[Index].Material == nullptr && Material != nullptr)
				{
					MeshBuild.SlotSources[Index].Material = Material;
				}
				return Index;
			}
		}

		MeshBuild.BuiltSlotNames.push_back(ResolvedSlotName);
		MeshBuild.IndicesBySlot.emplace_back();

		FFbxMaterialSlotSource SlotSource;
		SlotSource.SlotName = ResolvedSlotName;
		SlotSource.Material = Material;
		MeshBuild.SlotSources.push_back(SlotSource);

		return static_cast<int32>(MeshBuild.BuiltSlotNames.size() - 1);
	}

	FVector GetPolygonVertexNormal(
		FbxMesh* Mesh,
		const FbxAMatrix& MeshGlobalTransform,
		int32 PolygonIndex,
		int32 PolygonVertexIndex)
	{
		FbxVector4 FbxNormal(0.0, 0.0, 1.0, 0.0);
		if (Mesh != nullptr)
		{
			Mesh->GetPolygonVertexNormal(PolygonIndex, PolygonVertexIndex, FbxNormal);
		}

		FbxNormal = MeshGlobalTransform.MultR(FbxNormal);
		FVector Normal = FbxSceneUtils::ToEngineVector(FbxNormal).GetSafeNormal();
		return Normal.IsNearlyZero() ? FVector(0.0f, 0.0f, 1.0f) : Normal;
	}

	void BuildFallbackBasis(const FVector& InNormal, FVector& OutTangent, FVector& OutBitangent)
	{
		FVector Normal = InNormal.GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = FVector(0.0f, 0.0f, 1.0f);
		}

		Normal.FindBestAxisVectors(OutTangent, OutBitangent);
		OutTangent = OutTangent.GetSafeNormal();
		OutBitangent = OutBitangent.GetSafeNormal();
	}

	void CalculateTriangleTangent(FNormalVertex& V0, FNormalVertex& V1, FNormalVertex& V2)
	{
		const FVector Edge1 = V1.Position - V0.Position;
		const FVector Edge2 = V2.Position - V0.Position;
		const FVector2 DeltaUV1 = V1.UVs - V0.UVs;
		const FVector2 DeltaUV2 = V2.UVs - V0.UVs;

		const float Determinant = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;
		if (MathUtil::Abs(Determinant) <= MathUtil::Epsilon)
		{
			FVector Tangent;
			FVector Bitangent;
			BuildFallbackBasis(V0.Normal, Tangent, Bitangent);
			V0.Tangent = Tangent;
			V1.Tangent = Tangent;
			V2.Tangent = Tangent;
			V0.Bitangent = Bitangent;
			V1.Bitangent = Bitangent;
			V2.Bitangent = Bitangent;
			return;
		}

		const float InvDeterminant = 1.0f / Determinant;
		FVector Tangent = ((Edge1 * DeltaUV2.Y) - (Edge2 * DeltaUV1.Y)) * InvDeterminant;
		FVector Bitangent = ((Edge2 * DeltaUV1.X) - (Edge1 * DeltaUV2.X)) * InvDeterminant;

		if (!Tangent.Normalize())
		{
			BuildFallbackBasis(V0.Normal, Tangent, Bitangent);
		}
		else if (!Bitangent.Normalize())
		{
			Bitangent = FVector::CrossProduct(V0.Normal, Tangent);
			if (!Bitangent.Normalize())
			{
				BuildFallbackBasis(V0.Normal, Tangent, Bitangent);
			}
		}

		V0.Tangent = Tangent;
		V1.Tangent = Tangent;
		V2.Tangent = Tangent;
		V0.Bitangent = Bitangent;
		V1.Bitangent = Bitangent;
		V2.Bitangent = Bitangent;
	}

	FFbxStaticVertexKey MakeStaticVertexKey(const FNormalVertex& Vertex)
	{
		FFbxStaticVertexKey Key;
		Key.Position = Vertex.Position;
		Key.Color = Vertex.Color;
		Key.Normal = Vertex.Normal;
		Key.UVs = Vertex.UVs;
		return Key;
	}

	uint32 GetOrCreateStaticVertexIndex(FFbxStaticMeshBuild& MeshBuild, const FNormalVertex& Vertex)
	{
		const FFbxStaticVertexKey Key = MakeStaticVertexKey(Vertex);
		auto It = MeshBuild.VertexMap.find(Key);
		if (It != MeshBuild.VertexMap.end())
		{
			return It->second;
		}

		const uint32 NewIndex = static_cast<uint32>(MeshBuild.Mesh.Vertices.size());
		MeshBuild.Mesh.Vertices.push_back(Vertex);
		MeshBuild.TangentSums.push_back(FVector::ZeroVector);
		MeshBuild.BitangentSums.push_back(FVector::ZeroVector);
		MeshBuild.VertexMap.emplace(Key, NewIndex);
		return NewIndex;
	}

	void AccumulateStaticVertexBasis(FFbxStaticMeshBuild& MeshBuild, uint32 VertexIndex, const FNormalVertex& TriangleVertex)
	{
		if (VertexIndex >= MeshBuild.TangentSums.size() || VertexIndex >= MeshBuild.BitangentSums.size())
		{
			return;
		}

		MeshBuild.TangentSums[VertexIndex] += TriangleVertex.Tangent;
		MeshBuild.BitangentSums[VertexIndex] += TriangleVertex.Bitangent;
	}

	void FinalizeStaticVertexBasis(FFbxStaticMeshBuild& MeshBuild)
	{
		for (uint32 VertexIndex = 0; VertexIndex < static_cast<uint32>(MeshBuild.Mesh.Vertices.size()); ++VertexIndex)
		{
			FNormalVertex& Vertex = MeshBuild.Mesh.Vertices[VertexIndex];
			FVector Tangent = VertexIndex < MeshBuild.TangentSums.size()
				? MeshBuild.TangentSums[VertexIndex]
				: FVector::ZeroVector;
			FVector Bitangent = VertexIndex < MeshBuild.BitangentSums.size()
				? MeshBuild.BitangentSums[VertexIndex]
				: FVector::ZeroVector;

			if (!Tangent.Normalize())
			{
				BuildFallbackBasis(Vertex.Normal, Tangent, Bitangent);
			}
			else if (!Bitangent.Normalize())
			{
				Bitangent = FVector::CrossProduct(Vertex.Normal, Tangent);
				if (!Bitangent.Normalize())
				{
					BuildFallbackBasis(Vertex.Normal, Tangent, Bitangent);
				}
			}

			Vertex.Tangent = Tangent;
			Vertex.Bitangent = Bitangent;
		}
	}

	bool ExtractMeshNode(FFbxStaticMeshBuild& MeshBuild, FbxNode* MeshNode)
	{
		if (MeshNode == nullptr || MeshNode->GetMesh() == nullptr)
		{
			return false;
		}

		FbxMesh* Mesh = MeshNode->GetMesh();
		const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());
		if (ControlPointCount <= 0 || Mesh->GetControlPoints() == nullptr)
		{
			return false;
		}

		const int32 NodeMaterialCount = static_cast<int32>(MeshNode->GetMaterialCount());
		TArray<int32> NodeMaterialToSlot;
		if (NodeMaterialCount <= 0)
		{
			NodeMaterialToSlot.push_back(GetOrAddMaterialSlot(MeshBuild, "DefaultWhite", nullptr));
		}
		else
		{
			NodeMaterialToSlot.reserve(NodeMaterialCount);
			for (int32 MaterialIndex = 0; MaterialIndex < NodeMaterialCount; ++MaterialIndex)
			{
				FbxSurfaceMaterial* Material = MeshNode->GetMaterial(MaterialIndex);
				NodeMaterialToSlot.push_back(GetOrAddMaterialSlot(MeshBuild, FbxSceneUtils::GetFbxObjectName(Material, "Material"), Material));
			}
		}

		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		const char* UVSetName = UVSetNames.GetCount() > 0 ? UVSetNames.GetStringAt(0) : nullptr;
		const FbxAMatrix MeshGlobalTransform = MeshNode->EvaluateGlobalTransform();

		bool bExtractedAnyPolygon = false;
		const int32 PolygonCount = static_cast<int32>(Mesh->GetPolygonCount());
		MeshBuild.VertexMap.reserve(MeshBuild.VertexMap.size() + static_cast<size_t>(PolygonCount) * 3u);
		for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
		{
			const int32 PolygonSize = static_cast<int32>(Mesh->GetPolygonSize(PolygonIndex));
			if (PolygonSize != 3)
			{
				continue;
			}

			const int32 MaterialIndex = FbxSceneUtils::GetPolygonMaterialIndex(Mesh, PolygonIndex, static_cast<int32>(NodeMaterialToSlot.size()));
			const int32 SlotIndex = NodeMaterialToSlot[MaterialIndex];
			if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(MeshBuild.IndicesBySlot.size()))
			{
				continue;
			}

			FNormalVertex TriangleVertices[3] = {};
			bool bTriangleValid = true;
			for (int32 PolygonVertexIndex = 0; PolygonVertexIndex < 3; ++PolygonVertexIndex)
			{
				const int32 ControlPointIndex = static_cast<int32>(Mesh->GetPolygonVertex(PolygonIndex, PolygonVertexIndex));
				if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
				{
					bTriangleValid = false;
					break;
				}

				FNormalVertex& Vertex = TriangleVertices[PolygonVertexIndex];
				const FbxVector4 FbxPosition = MeshGlobalTransform.MultT(Mesh->GetControlPointAt(ControlPointIndex));
				Vertex.Position = FbxSceneUtils::ToEngineVector(FbxPosition);
				Vertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };
				Vertex.Normal = GetPolygonVertexNormal(Mesh, MeshGlobalTransform, PolygonIndex, PolygonVertexIndex);
				Vertex.UVs = FbxSceneUtils::GetPolygonVertexUV(Mesh, PolygonIndex, PolygonVertexIndex, UVSetName);
			}

			if (!bTriangleValid)
			{
				continue;
			}

			CalculateTriangleTangent(TriangleVertices[0], TriangleVertices[1], TriangleVertices[2]);

			uint32 TriangleIndices[3] = { 0, 0, 0 };
			for (int32 PolygonVertexIndex = 0; PolygonVertexIndex < 3; ++PolygonVertexIndex)
			{
				TriangleIndices[PolygonVertexIndex] = GetOrCreateStaticVertexIndex(MeshBuild, TriangleVertices[PolygonVertexIndex]);
				AccumulateStaticVertexBasis(MeshBuild, TriangleIndices[PolygonVertexIndex], TriangleVertices[PolygonVertexIndex]);
			}

			MeshBuild.IndicesBySlot[SlotIndex].push_back(TriangleIndices[0]);
			MeshBuild.IndicesBySlot[SlotIndex].push_back(TriangleIndices[1]);
			MeshBuild.IndicesBySlot[SlotIndex].push_back(TriangleIndices[2]);
			bExtractedAnyPolygon = true;
		}

		return bExtractedAnyPolygon;
	}

	FAABB BuildLocalBounds(const FStaticMesh& StaticMesh)
	{
		FAABB Bounds;
		Bounds.Reset();

		for (const FNormalVertex& Vertex : StaticMesh.Vertices)
		{
			Bounds.Expand(Vertex.Position);
		}

		return Bounds;
	}

	void FinalizeStaticMeshBuild(FFbxStaticMeshBuild& MeshBuild)
	{
		FinalizeStaticVertexBasis(MeshBuild);

		MeshBuild.Mesh.Slots.clear();
		MeshBuild.Mesh.Sections.clear();
		MeshBuild.Mesh.Indices.clear();

		for (const FString& SlotName : MeshBuild.BuiltSlotNames)
		{
			FStaticMeshMaterialSlot Slot;
			Slot.SlotName = SlotName;
			Slot.Material = nullptr;
			MeshBuild.Mesh.Slots.push_back(Slot);
		}

		for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(MeshBuild.IndicesBySlot.size()); ++SlotIndex)
		{
			const TArray<uint32>& SectionIndices = MeshBuild.IndicesBySlot[SlotIndex];
			if (SectionIndices.empty())
			{
				continue;
			}

			FStaticMeshSection Section;
			Section.StartIndex = static_cast<uint32>(MeshBuild.Mesh.Indices.size());
			Section.IndexCount = static_cast<uint32>(SectionIndices.size());
			Section.MaterialSlotIndex = SlotIndex;

			MeshBuild.Mesh.Indices.insert(
				MeshBuild.Mesh.Indices.end(),
				SectionIndices.begin(),
				SectionIndices.end());
			MeshBuild.Mesh.Sections.push_back(Section);
		}

		MeshBuild.Mesh.LocalBounds = BuildLocalBounds(MeshBuild.Mesh);
	}
}

bool FFbxStaticMeshExtractor::Extract(
	const FFbxSceneDocument& Document,
	const FFbxSceneImportManifest& Manifest,
	TArray<FFbxStaticMeshExtractResult>& OutMeshes) const
{
	OutMeshes.clear();

	FbxNode* SceneRootNode = Document.GetRootNode();
	if (SceneRootNode == nullptr || Manifest.StaticMeshNodes.empty())
	{
		return false;
	}

	TArray<FFbxStaticMeshBuild> MeshBuilds;
	TMap<FString, int32> AssetStemUseCounts;
	TMap<FbxNode*, int32> MeshBuildIndexByOwnerNode;
	bool bExtractedAnyMesh = false;

	for (FbxNode* Node : Manifest.StaticMeshNodes)
	{
		if (Node == nullptr || Node->GetMesh() == nullptr)
		{
			continue;
		}

		FbxNode* OwnerNode = FbxSceneUtils::GetMeshAssetOwnerNode(Node, SceneRootNode);
		if (OwnerNode == nullptr)
		{
			continue;
		}

		auto BuildIndexIt = MeshBuildIndexByOwnerNode.find(OwnerNode);
		if (BuildIndexIt == MeshBuildIndexByOwnerNode.end())
		{
			FFbxStaticMeshBuild MeshBuild;
			MeshBuild.MeshNodeName = FbxSceneUtils::GetNodeAssetBaseName(OwnerNode, Document.GetSourcePath());
			MeshBuild.AssetStem = FbxSceneUtils::MakeUniqueAssetStem(MeshBuild.MeshNodeName, AssetStemUseCounts);

			const int32 NewBuildIndex = static_cast<int32>(MeshBuilds.size());
			MeshBuilds.push_back(MeshBuild);
			MeshBuildIndexByOwnerNode[OwnerNode] = NewBuildIndex;
			BuildIndexIt = MeshBuildIndexByOwnerNode.find(OwnerNode);
		}

		FFbxStaticMeshBuild& MeshBuild = MeshBuilds[BuildIndexIt->second];
		if (ExtractMeshNode(MeshBuild, Node))
		{
			bExtractedAnyMesh = true;
		}
	}

	MeshBuilds.erase(
		std::remove_if(
			MeshBuilds.begin(),
			MeshBuilds.end(),
			[](const FFbxStaticMeshBuild& MeshBuild)
			{
				return MeshBuild.Mesh.Vertices.empty();
			}),
		MeshBuilds.end());

	for (FFbxStaticMeshBuild& MeshBuild : MeshBuilds)
	{
		FinalizeStaticMeshBuild(MeshBuild);

		FFbxStaticMeshExtractResult Result;
		Result.MeshNodeName = MeshBuild.MeshNodeName;
		Result.AssetStem = MeshBuild.AssetStem;
		Result.Mesh = MeshBuild.Mesh;
		Result.SlotSources = MeshBuild.SlotSources;
		OutMeshes.push_back(Result);
	}

	UE_LOG("[FbxStaticMeshExtractor] FBX static meshes extracted. Meshes: %d, Path: %s",
		static_cast<int32>(OutMeshes.size()),
		Document.GetSourcePath().c_str());

	return bExtractedAnyMesh && !OutMeshes.empty();
}
