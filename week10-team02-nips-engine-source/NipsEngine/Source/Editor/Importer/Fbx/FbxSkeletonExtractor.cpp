#include "Editor/Importer/Fbx/FbxSkeletonExtractor.h"

#include "Core/Logger.h"
#include "Editor/Importer/Fbx/FbxSceneDocument.h"
#include "Editor/Importer/Fbx/FbxSceneUtils.h"

#include <fbxsdk.h>

namespace
{
	int32 AddBoneRecursive(FFbxSkeletonExtractResult& SkeletonResult, FbxNode* BoneNode)
	{
		if (BoneNode == nullptr)
		{
			return -1;
		}

		auto ExistingIt = SkeletonResult.BoneIndexMap.find(BoneNode);
		if (ExistingIt != SkeletonResult.BoneIndexMap.end())
		{
			return ExistingIt->second;
		}

		int32 ParentIndex = -1;
		FbxNode* ParentNode = BoneNode->GetParent();
		if (FbxSceneUtils::IsSkeletonNode(ParentNode))
		{
			ParentIndex = AddBoneRecursive(SkeletonResult, ParentNode);
		}

		FSkeletalBone NewBone;
		NewBone.Name = FbxSceneUtils::GetFbxObjectName(BoneNode, "Bone");
		NewBone.ParentIndex = ParentIndex;

		const FMatrix BoneGlobalMatrix = FbxSceneUtils::ToEngineMatrix(BoneNode->EvaluateGlobalTransform());
		FMatrix ReferenceLocalMatrix = BoneGlobalMatrix;
		if (ParentIndex >= 0 && ParentNode != nullptr)
		{
			const FMatrix ParentGlobalMatrix = FbxSceneUtils::ToEngineMatrix(ParentNode->EvaluateGlobalTransform());
			ReferenceLocalMatrix = BoneGlobalMatrix * ParentGlobalMatrix.GetInverse();
		}

		NewBone.ReferenceLocalTransform = FTransform(ReferenceLocalMatrix);

		const int32 NewBoneIndex = static_cast<int32>(SkeletonResult.Skeleton.Bones.size());
		SkeletonResult.Skeleton.Bones.push_back(NewBone);
		SkeletonResult.BoneIndexMap[BoneNode] = NewBoneIndex;

		return NewBoneIndex;
	}

	void AppendClusterBonesFromMesh(FFbxSkeletonExtractResult& SkeletonResult, FbxMesh* Mesh)
	{
		if (Mesh == nullptr)
		{
			return;
		}

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

				AddBoneRecursive(SkeletonResult, Cluster->GetLink());
			}
		}
	}

	void BuildReferenceInverseBindPose(FFbxSkeletonExtractResult& SkeletonResult)
	{
		const int32 BoneCount = static_cast<int32>(SkeletonResult.Skeleton.Bones.size());
		SkeletonResult.Skeleton.InverseBindPoseMatrices.clear();
		SkeletonResult.Skeleton.InverseBindPoseMatrices.resize(BoneCount, FMatrix::Identity);

		TArray<FTransform> ComponentSpaceRefPose;
		ComponentSpaceRefPose.resize(BoneCount, FTransform::Identity);

		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const FSkeletalBone& Bone = SkeletonResult.Skeleton.Bones[BoneIndex];
			if (Bone.ParentIndex >= 0 && Bone.ParentIndex < BoneIndex)
			{
				ComponentSpaceRefPose[BoneIndex] = Bone.ReferenceLocalTransform * ComponentSpaceRefPose[Bone.ParentIndex];
			}
			else
			{
				ComponentSpaceRefPose[BoneIndex] = Bone.ReferenceLocalTransform;
			}

			SkeletonResult.Skeleton.InverseBindPoseMatrices[BoneIndex] = ComponentSpaceRefPose[BoneIndex].ToMatrixWithScale().GetInverse();
		}
	}
}

bool FFbxSkeletonExtractor::Extract(
	const FFbxSceneDocument& Document,
	const FFbxSceneImportManifest& Manifest,
	FFbxSkeletonExtractResult& OutSkeleton) const
{
	OutSkeleton = FFbxSkeletonExtractResult();

	for (FbxNode* MeshNode : Manifest.SkinnedMeshNodes)
	{
		AppendClusterBonesFromMesh(OutSkeleton, MeshNode ? MeshNode->GetMesh() : nullptr);
	}

	BuildReferenceInverseBindPose(OutSkeleton);

	UE_LOG("[FbxSkeletonExtractor] FBX skeleton collected. BoneCount: %d, Path: %s",
		static_cast<int32>(OutSkeleton.Skeleton.Bones.size()),
		Document.GetSourcePath().c_str());

	return !OutSkeleton.Skeleton.Bones.empty() &&
		OutSkeleton.Skeleton.InverseBindPoseMatrices.size() == OutSkeleton.Skeleton.Bones.size();
}
