#include "Editor/Importer/Fbx/FbxSkeletalMeshExtractor.h"

#include "Core/Logger.h"
#include "Editor/Importer/Fbx/FbxMaterialExtractor.h"
#include "Editor/Importer/Fbx/FbxSceneDocument.h"
#include "Editor/Importer/Fbx/FbxSceneUtils.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
	struct FBoneInfluence
	{
		int32 BoneIndex = -1;
		float Weight = 0.0f;
	};

	struct FControlPointInfluenceList
	{
		TArray<FBoneInfluence> Influences;
	};

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

	struct FFbxSkeletalVertexKey
	{
		FVector Position;
		FColor Color;
		FVector Normal;
		FVector2 UVs;
		int32 BoneIndices[MAX_SKELETAL_BONE_INFLUENCES] = { -1, -1, -1, -1 };
		float BoneWeights[MAX_SKELETAL_BONE_INFLUENCES] = { 0.0f, 0.0f, 0.0f, 0.0f };

		bool operator==(const FFbxSkeletalVertexKey& Other) const
		{
			if (Position != Other.Position
				|| Color.R != Other.Color.R
				|| Color.G != Other.Color.G
				|| Color.B != Other.Color.B
				|| Color.A != Other.Color.A
				|| Normal != Other.Normal
				|| UVs != Other.UVs)
			{
				return false;
			}

			for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
			{
				if (BoneIndices[InfluenceIndex] != Other.BoneIndices[InfluenceIndex]
					|| BoneWeights[InfluenceIndex] != Other.BoneWeights[InfluenceIndex])
				{
					return false;
				}
			}

			return true;
		}
	};

	struct FFbxSkeletalVertexKeyHasher
	{
		size_t operator()(const FFbxSkeletalVertexKey& Key) const noexcept
		{
			size_t Hash = std::hash<FVector>{}(Key.Position);
			Hash = CombineHash(Hash, HashColor(Key.Color));
			Hash = CombineHash(Hash, std::hash<FVector>{}(Key.Normal));
			Hash = CombineHash(Hash, std::hash<FVector2>{}(Key.UVs));

			for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
			{
				Hash = CombineHash(Hash, std::hash<int32>{}(Key.BoneIndices[InfluenceIndex]));
				Hash = CombineHash(Hash, std::hash<float>{}(Key.BoneWeights[InfluenceIndex]));
			}

			return Hash;
		}
	};

	struct FFbxSkeletalNodeVertexBuild
	{
		uint32 BaseVertexIndex = 0;
		TMap<FFbxSkeletalVertexKey, uint32, FFbxSkeletalVertexKeyHasher> VertexMap;
		TArray<FVector> TangentSums;
		TArray<FVector> BitangentSums;
	};

	void AddMaterialSlotsForNode(
		const FString& SourcePath,
		FbxNode* MeshNode,
		FSkeletalMesh& Mesh,
		TArray<FFbxMaterialSlotSource>& SlotSources,
		int32& OutMaterialBaseIndex,
		int32& OutMaterialCount)
	{
		OutMaterialBaseIndex = static_cast<int32>(Mesh.MaterialSlots.size());
		OutMaterialCount = 0;

		if (MeshNode == nullptr)
		{
			return;
		}

		FFbxMaterialExtractor MaterialExtractor;
		const int32 NodeMaterialCount = static_cast<int32>(MeshNode->GetMaterialCount());
		if (NodeMaterialCount <= 0)
		{
			FSkeletalMeshMaterialSlot Slot;
			Slot.SlotName = FString("DefaultWhite");
			Slot.Material = nullptr;
			Mesh.MaterialSlots.push_back(Slot);

			FFbxMaterialSlotSource SlotSource;
			SlotSource.SlotName = Slot.SlotName;
			SlotSource.Material = nullptr;
			SlotSources.push_back(SlotSource);

			OutMaterialCount = 1;
			return;
		}

		for (int32 MaterialIndex = 0; MaterialIndex < NodeMaterialCount; ++MaterialIndex)
		{
			FbxSurfaceMaterial* FbxMaterial = MeshNode->GetMaterial(MaterialIndex);
			const FFbxExtractedMaterial ExtractedMaterial = MaterialExtractor.ExtractMaterial(SourcePath, FbxMaterial, "Material");

			FSkeletalMeshMaterialSlot Slot;
			Slot.SlotName = ExtractedMaterial.SlotName;
			Slot.ExtractedDiffusePath = ExtractedMaterial.DiffuseTexturePath;
			Slot.ExtractedNormalPath = ExtractedMaterial.NormalTexturePath;
			Slot.ExtractedSpecularPath = ExtractedMaterial.SpecularTexturePath;
			Slot.Material = ExtractedMaterial.Material;
			Mesh.MaterialSlots.push_back(Slot);

			FFbxMaterialSlotSource SlotSource;
			SlotSource.SlotName = ExtractedMaterial.SlotName;
			SlotSource.Material = FbxMaterial;
			SlotSources.push_back(SlotSource);
		}

		OutMaterialCount = NodeMaterialCount;
	}

	void BuildControlPointInfluences(
		FFbxSkeletonExtractResult& SkeletonResult,
		FbxMesh* Mesh,
		TArray<FControlPointInfluenceList>& OutControlPointInfluences)
	{
		OutControlPointInfluences.clear();

		if (Mesh == nullptr)
		{
			return;
		}

		const int32 ControlPointCount = static_cast<int32>(Mesh->GetControlPointsCount());
		OutControlPointInfluences.resize(ControlPointCount);

		const int32 SkinCount = static_cast<int32>(Mesh->GetDeformerCount(FbxDeformer::eSkin));
		for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
		{
			FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
			if (Skin == nullptr)
			{
				continue;
			}

			const int32 ClusterCount = static_cast<int32>(Skin->GetClusterCount());
			for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
				if (Cluster == nullptr || Cluster->GetLink() == nullptr)
				{
					continue;
				}

				auto BoneIt = SkeletonResult.BoneIndexMap.find(Cluster->GetLink());
				if (BoneIt == SkeletonResult.BoneIndexMap.end())
				{
					continue;
				}

				const int32 BoneIndex = BoneIt->second;
				if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(SkeletonResult.Skeleton.InverseBindPoseMatrices.size()))
				{
					FbxAMatrix FbxMeshMatrix;
					Cluster->GetTransformMatrix(FbxMeshMatrix);
					FbxAMatrix FbxLinkMatrix;
					Cluster->GetTransformLinkMatrix(FbxLinkMatrix);

					const FMatrix MeshBindMatrix = FbxSceneUtils::ToEngineMatrix(FbxMeshMatrix);
					const FMatrix BoneBindMatrix = FbxSceneUtils::ToEngineMatrix(FbxLinkMatrix);
					SkeletonResult.Skeleton.InverseBindPoseMatrices[BoneIndex] = MeshBindMatrix * BoneBindMatrix.GetInverse();
				}

				const int* ControlPointIndices = Cluster->GetControlPointIndices();
				const double* ControlPointWeights = Cluster->GetControlPointWeights();

				const int32 InfluenceCount = static_cast<int32>(Cluster->GetControlPointIndicesCount());
				for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
				{
					const int32 ControlPointIndex = static_cast<int32>(ControlPointIndices[InfluenceIndex]);
					if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
					{
						continue;
					}

					const float Weight = static_cast<float>(ControlPointWeights[InfluenceIndex]);
					if (Weight <= 0.0f)
					{
						continue;
					}

					FBoneInfluence Influence;
					Influence.BoneIndex = BoneIndex;
					Influence.Weight = Weight;
					OutControlPointInfluences[ControlPointIndex].Influences.push_back(Influence);
				}
			}
		}
	}

	void AssignBoneInfluencesToVertex(
		const TArray<FBoneInfluence>& SourceInfluences,
		bool bHasSkeleton,
		FSkeletalMeshVertex& OutVertex)
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
		{
			OutVertex.BoneIndices[InfluenceIndex] = -1;
			OutVertex.BoneWeights[InfluenceIndex] = 0.0f;
		}

		TArray<FBoneInfluence> Influences = SourceInfluences;
		std::sort(Influences.begin(), Influences.end(), [](const FBoneInfluence& Left, const FBoneInfluence& Right)
		{
			return Left.Weight > Right.Weight;
		});

		const int32 UsedInfluenceCount = static_cast<int32>(std::min<size_t>(Influences.size(), MAX_SKELETAL_BONE_INFLUENCES));
		float WeightSum = 0.0f;
		for (int32 InfluenceIndex = 0; InfluenceIndex < UsedInfluenceCount; ++InfluenceIndex)
		{
			WeightSum += Influences[InfluenceIndex].Weight;
		}

		if (WeightSum <= 0.0f)
		{
			if (bHasSkeleton)
			{
				OutVertex.BoneIndices[0] = 0;
				OutVertex.BoneWeights[0] = 1.0f;
			}
			return;
		}

		const float InvWeightSum = 1.0f / WeightSum;
		for (int32 InfluenceIndex = 0; InfluenceIndex < UsedInfluenceCount; ++InfluenceIndex)
		{
			OutVertex.BoneIndices[InfluenceIndex] = Influences[InfluenceIndex].BoneIndex;
			OutVertex.BoneWeights[InfluenceIndex] = Influences[InfluenceIndex].Weight * InvWeightSum;
		}
	}

	FVector GetPolygonVertexNormal(FbxMesh* Mesh, int32 PolygonIndex, int32 PolygonVertexIndex)
	{
		FbxVector4 FbxNormal(0.0, 0.0, 1.0, 0.0);
		if (Mesh != nullptr)
		{
			Mesh->GetPolygonVertexNormal(PolygonIndex, PolygonVertexIndex, FbxNormal);
		}

		return FbxSceneUtils::ToEngineVector(FbxNormal).GetSafeNormal();
	}

	void BuildFallbackBasis(const FVector& InNormal, FVector& OutTangent, FVector& OutBitangent)
	{
		FVector Normal = InNormal.GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = FVector::UpVector;
		}

		const FVector FallbackTangent = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
		OutTangent = FallbackTangent.IsNearlyZero() ? FVector::ForwardVector : FallbackTangent;
		OutBitangent = FVector::CrossProduct(Normal, OutTangent).GetSafeNormal();
		if (OutBitangent.IsNearlyZero())
		{
			Normal.FindBestAxisVectors(OutTangent, OutBitangent);
			OutTangent = OutTangent.GetSafeNormal();
			OutBitangent = OutBitangent.GetSafeNormal();
		}
	}

	void CalculateTriangleTangent(FSkeletalMeshVertex& V0, FSkeletalMeshVertex& V1, FSkeletalMeshVertex& V2)
	{
		const FVector Edge1 = V1.Position - V0.Position;
		const FVector Edge2 = V2.Position - V0.Position;
		const FVector2 DeltaUV1 = V1.UVs - V0.UVs;
		const FVector2 DeltaUV2 = V2.UVs - V0.UVs;

		const float Determinant = DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X;
		if (std::fabs(Determinant) <= 1.e-8f)
		{
			FVector SafeTangent;
			FVector SafeBitangent;
			BuildFallbackBasis(V0.Normal, SafeTangent, SafeBitangent);

			V0.Tangent = SafeTangent;
			V1.Tangent = SafeTangent;
			V2.Tangent = SafeTangent;
			V0.Bitangent = SafeBitangent;
			V1.Bitangent = SafeBitangent;
			V2.Bitangent = SafeBitangent;
			return;
		}

		const float InvDeterminant = 1.0f / Determinant;
		const FVector Tangent = ((Edge1 * DeltaUV2.Y) - (Edge2 * DeltaUV1.Y)) * InvDeterminant;
		const FVector Bitangent = ((Edge2 * DeltaUV1.X) - (Edge1 * DeltaUV2.X)) * InvDeterminant;

		const FVector SafeTangent = Tangent.GetSafeNormal();
		const FVector SafeBitangent = Bitangent.GetSafeNormal();

		V0.Tangent = SafeTangent;
		V1.Tangent = SafeTangent;
		V2.Tangent = SafeTangent;
		V0.Bitangent = SafeBitangent;
		V1.Bitangent = SafeBitangent;
		V2.Bitangent = SafeBitangent;
	}

	FFbxSkeletalVertexKey MakeSkeletalVertexKey(const FSkeletalMeshVertex& Vertex)
	{
		FFbxSkeletalVertexKey Key;
		Key.Position = Vertex.Position;
		Key.Color = Vertex.Color;
		Key.Normal = Vertex.Normal;
		Key.UVs = Vertex.UVs;

		for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
		{
			Key.BoneIndices[InfluenceIndex] = Vertex.BoneIndices[InfluenceIndex];
			Key.BoneWeights[InfluenceIndex] = Vertex.BoneWeights[InfluenceIndex];
		}

		return Key;
	}

	uint32 GetOrCreateSkeletalVertexIndex(
		FFbxSkeletalNodeVertexBuild& VertexBuild,
		FSkeletalMesh& MeshData,
		const FSkeletalMeshVertex& Vertex)
	{
		const FFbxSkeletalVertexKey Key = MakeSkeletalVertexKey(Vertex);
		auto It = VertexBuild.VertexMap.find(Key);
		if (It != VertexBuild.VertexMap.end())
		{
			return It->second;
		}

		const uint32 NewIndex = static_cast<uint32>(MeshData.Vertices.size());
		MeshData.Vertices.push_back(Vertex);
		VertexBuild.TangentSums.push_back(FVector::ZeroVector);
		VertexBuild.BitangentSums.push_back(FVector::ZeroVector);
		VertexBuild.VertexMap.emplace(Key, NewIndex);
		return NewIndex;
	}

	void AccumulateSkeletalVertexBasis(
		FFbxSkeletalNodeVertexBuild& VertexBuild,
		uint32 VertexIndex,
		const FSkeletalMeshVertex& TriangleVertex)
	{
		if (VertexIndex < VertexBuild.BaseVertexIndex)
		{
			return;
		}

		const uint32 LocalIndex = VertexIndex - VertexBuild.BaseVertexIndex;
		if (LocalIndex >= VertexBuild.TangentSums.size() || LocalIndex >= VertexBuild.BitangentSums.size())
		{
			return;
		}

		VertexBuild.TangentSums[LocalIndex] += TriangleVertex.Tangent;
		VertexBuild.BitangentSums[LocalIndex] += TriangleVertex.Bitangent;
	}

	void FinalizeSkeletalVertexBasis(const FFbxSkeletalNodeVertexBuild& VertexBuild, FSkeletalMesh& MeshData)
	{
		for (uint32 LocalIndex = 0; LocalIndex < static_cast<uint32>(VertexBuild.TangentSums.size()); ++LocalIndex)
		{
			const uint32 VertexIndex = VertexBuild.BaseVertexIndex + LocalIndex;
			if (VertexIndex >= MeshData.Vertices.size())
			{
				continue;
			}

			FSkeletalMeshVertex& Vertex = MeshData.Vertices[VertexIndex];
			FVector Tangent = VertexBuild.TangentSums[LocalIndex];
			FVector Bitangent = LocalIndex < VertexBuild.BitangentSums.size()
				? VertexBuild.BitangentSums[LocalIndex]
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

	bool ExtractMeshNode(
		const FString& SourcePath,
		FFbxSkeletonExtractResult& SkeletonResult,
		FSkeletalMesh& MeshData,
		TArray<FFbxMaterialSlotSource>& SlotSources,
		FbxNode* MeshNode)
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

		int32 MaterialBaseIndex = 0;
		int32 MaterialCount = 0;
		AddMaterialSlotsForNode(SourcePath, MeshNode, MeshData, SlotSources, MaterialBaseIndex, MaterialCount);
		if (MaterialCount <= 0)
		{
			return false;
		}

		TArray<TArray<uint32>> IndicesByMaterial;
		IndicesByMaterial.resize(MaterialCount);

		TArray<FControlPointInfluenceList> ControlPointInfluences;
		BuildControlPointInfluences(SkeletonResult, Mesh, ControlPointInfluences);

		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		const char* UVSetName = UVSetNames.GetCount() > 0 ? UVSetNames.GetStringAt(0) : nullptr;
		const bool bHasSkeleton = !SkeletonResult.Skeleton.Bones.empty();

		FFbxSkeletalNodeVertexBuild VertexBuild;
		VertexBuild.BaseVertexIndex = static_cast<uint32>(MeshData.Vertices.size());

		const int32 PolygonCount = static_cast<int32>(Mesh->GetPolygonCount());
		VertexBuild.VertexMap.reserve(static_cast<size_t>(PolygonCount) * 3u);
		for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
		{
			const int32 PolygonSize = static_cast<int32>(Mesh->GetPolygonSize(PolygonIndex));
			if (PolygonSize != 3)
			{
				continue;
			}

			const int32 MaterialIndex = FbxSceneUtils::GetPolygonMaterialIndex(Mesh, PolygonIndex, MaterialCount);

			FSkeletalMeshVertex TriangleVertices[3] = {};
			bool bTriangleValid = true;
			for (int32 PolygonVertexIndex = 0; PolygonVertexIndex < 3; ++PolygonVertexIndex)
			{
				const int32 ControlPointIndex = static_cast<int32>(Mesh->GetPolygonVertex(PolygonIndex, PolygonVertexIndex));
				if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
				{
					bTriangleValid = false;
					break;
				}

				FSkeletalMeshVertex& Vertex = TriangleVertices[PolygonVertexIndex];
				Vertex.Position = FbxSceneUtils::ToEngineVector(Mesh->GetControlPointAt(ControlPointIndex));
				Vertex.Normal = GetPolygonVertexNormal(Mesh, PolygonIndex, PolygonVertexIndex);
				Vertex.UVs = FbxSceneUtils::GetPolygonVertexUV(Mesh, PolygonIndex, PolygonVertexIndex, UVSetName);

				if (ControlPointIndex < static_cast<int32>(ControlPointInfluences.size()))
				{
					AssignBoneInfluencesToVertex(ControlPointInfluences[ControlPointIndex].Influences, bHasSkeleton, Vertex);
				}
				else
				{
					AssignBoneInfluencesToVertex(TArray<FBoneInfluence>(), bHasSkeleton, Vertex);
				}
			}

			if (!bTriangleValid)
			{
				continue;
			}

			CalculateTriangleTangent(
				TriangleVertices[0],
				TriangleVertices[1],
				TriangleVertices[2]);

			uint32 TriangleIndices[3] = { 0, 0, 0 };
			for (int32 PolygonVertexIndex = 0; PolygonVertexIndex < 3; ++PolygonVertexIndex)
			{
				TriangleIndices[PolygonVertexIndex] = GetOrCreateSkeletalVertexIndex(VertexBuild, MeshData, TriangleVertices[PolygonVertexIndex]);
				AccumulateSkeletalVertexBasis(VertexBuild, TriangleIndices[PolygonVertexIndex], TriangleVertices[PolygonVertexIndex]);
			}

			IndicesByMaterial[MaterialIndex].push_back(TriangleIndices[0]);
			IndicesByMaterial[MaterialIndex].push_back(TriangleIndices[1]);
			IndicesByMaterial[MaterialIndex].push_back(TriangleIndices[2]);
		}

		FinalizeSkeletalVertexBasis(VertexBuild, MeshData);

		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			const TArray<uint32>& SectionIndices = IndicesByMaterial[MaterialIndex];
			if (SectionIndices.empty())
			{
				continue;
			}

			FSkeletalMeshSection Section;
			Section.StartIndex = static_cast<uint32>(MeshData.Indices.size());
			Section.IndexCount = static_cast<uint32>(SectionIndices.size());
			Section.MaterialSlotIndex = MaterialBaseIndex + MaterialIndex;

			MeshData.Indices.insert(MeshData.Indices.end(), SectionIndices.begin(), SectionIndices.end());
			MeshData.Sections.push_back(Section);
		}

		return true;
	}
}

bool FFbxSkeletalMeshExtractor::Extract(
	const FFbxSceneDocument& Document,
	const FFbxSceneImportManifest& Manifest,
	FFbxSkeletonExtractResult& SkeletonResult,
	FFbxSkeletalMeshExtractResult& OutMesh) const
{
	OutMesh = FFbxSkeletalMeshExtractResult();
	OutMesh.Mesh.PathFileName = Document.GetSourcePath();

	if (SkeletonResult.Skeleton.Bones.empty() || Manifest.SkinnedMeshNodes.empty())
	{
		return false;
	}

	bool bExtractedAnyMesh = false;
	for (FbxNode* MeshNode : Manifest.SkinnedMeshNodes)
	{
		bExtractedAnyMesh |= ExtractMeshNode(
			Document.GetSourcePath(),
			SkeletonResult,
			OutMesh.Mesh,
			OutMesh.SlotSources,
			MeshNode);
	}

	UE_LOG("[FbxSkeletalMeshExtractor] FBX skeletal mesh extracted. Vertices: %d, Indices: %d, Sections: %d, Path: %s",
		static_cast<int32>(OutMesh.Mesh.Vertices.size()),
		static_cast<int32>(OutMesh.Mesh.Indices.size()),
		static_cast<int32>(OutMesh.Mesh.Sections.size()),
		Document.GetSourcePath().c_str());

	return bExtractedAnyMesh && !OutMesh.Mesh.Vertices.empty() && !OutMesh.Mesh.Indices.empty();
}
