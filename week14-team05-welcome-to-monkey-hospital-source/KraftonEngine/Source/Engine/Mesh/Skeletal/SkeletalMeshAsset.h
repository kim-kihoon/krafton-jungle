#pragma once

#include "Core/Types/CoreTypes.h"
#include "Render/Types/VertexTypes.h"
#include "Render/Resource/Buffer.h"
#include "Math/Matrix.h"
#include "Serialization/Archive.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"

#include <algorithm>
#include <memory>

inline void SerializeSkeletalMatrix(FArchive& Ar, FMatrix& Matrix)
{
	Ar.Serialize(Matrix.Data, sizeof(float) * 16);
}

struct FBone
{
	FString Name;
	int32 ParentIndex = -1;

	// Runtime pose semantics. The legacy LocalMatrix/GlobalMatrix slots are kept
	// for existing package layout, but code should use these named poses.
	FMatrix ReferenceLocalPose = FMatrix::Identity;
	FMatrix ReferenceGlobalPose = FMatrix::Identity;
	FMatrix SkinBindGlobalPose = FMatrix::Identity;
	FMatrix LocalMatrix = FMatrix::Identity;
	FMatrix GlobalMatrix = FMatrix::Identity;
	FMatrix InverseBindPoseMatrix = FMatrix::Identity;

	void SyncSeparatedPoseDataFromLegacy()
	{
		ReferenceLocalPose = LocalMatrix;
		ReferenceGlobalPose = GlobalMatrix;
		SkinBindGlobalPose = GlobalMatrix;
	}

	void SyncLegacyPoseDataFromSeparated()
	{
		LocalMatrix = ReferenceLocalPose;
		GlobalMatrix = SkinBindGlobalPose;
	}

	const FMatrix& GetReferenceLocalPose() const { return ReferenceLocalPose; }
	const FMatrix& GetReferenceGlobalPose() const { return ReferenceGlobalPose; }
	const FMatrix& GetSkinBindGlobalPose() const { return SkinBindGlobalPose; }
	const FMatrix& GetInverseBindPose() const { return InverseBindPoseMatrix; }

	friend FArchive& operator<<(FArchive& Ar, FBone& Bone)
	{
		if (Ar.IsSaving())
		{
			Bone.SyncLegacyPoseDataFromSeparated();
		}

		Ar << Bone.Name;
		Ar << Bone.ParentIndex;
		SerializeSkeletalMatrix(Ar, Bone.LocalMatrix);
		SerializeSkeletalMatrix(Ar, Bone.GlobalMatrix);
		SerializeSkeletalMatrix(Ar, Bone.InverseBindPoseMatrix);

		if (Ar.IsLoading())
		{
			Bone.SyncSeparatedPoseDataFromLegacy();
		}
		return Ar;
	}
};

struct FSkeletalMeshSection
{
	int32 MaterialIndex = -1;
	FString MaterialSlotName;
	uint32 FirstIndex;
	uint32 IndexCount;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalMeshSection& Section)
	{
		Ar << Section.MaterialSlotName;
		Ar << Section.FirstIndex;
		Ar << Section.IndexCount;
		return Ar;
	}
};

struct FSkeletalMaterial
{
	UMaterial* MaterialInterface = nullptr;
	FString MaterialSlotName = "None";
	FString MaterialPath;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalMaterial& Mat)
	{
		Ar << Mat.MaterialSlotName;

		// Material 포인터는 실행마다 달라질 수 있다.
		// .sketbin에는 다시 찾을 수 있는 .mat 경로만 저장한다.
		if (Ar.IsSaving() && Mat.MaterialInterface)
		{
			Mat.MaterialPath = Mat.MaterialInterface->GetAssetPathFileName();
		}
		Ar << Mat.MaterialPath;

		if (Ar.IsLoading())
		{
			if (!Mat.MaterialPath.empty())
			{
				Mat.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial(Mat.MaterialPath);
			}
			else
			{
				Mat.MaterialInterface = nullptr;
			}
		}

		return Ar;
	}
};

struct FSkeletalMeshRange
{
	uint32 VertexStart = 0;
	uint32 VertexEnd = 0;
	uint32 FirstIndex = 0;
	uint32 IndexCount = 0;
	// Legacy serialization slot. New imports bake mesh bind transforms into vertices.
	FMatrix MeshBindGlobal = FMatrix::Identity;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalMeshRange& Range)
	{
		Ar << Range.VertexStart;
		Ar << Range.VertexEnd;
		Ar << Range.FirstIndex;
		Ar << Range.IndexCount;
		SerializeSkeletalMatrix(Ar, Range.MeshBindGlobal);
		return Ar;
	}
};

struct FMorphTargetDelta
{
	uint32  VertexIndex   = 0;
	FVector PositionDelta = FVector::ZeroVector;

	friend FArchive& operator<<(FArchive& Ar, FMorphTargetDelta& Delta)
	{
		Ar << Delta.VertexIndex;
		Ar << Delta.PositionDelta;
		return Ar;
	}
};

struct FMorphTarget
{
	FString                   Name;
	TArray<FMorphTargetDelta> Deltas;

	friend FArchive& operator<<(FArchive& Ar, FMorphTarget& Target)
	{
		Ar << Target.Name;
		Ar << Target.Deltas;
		return Ar;
	}
};

struct FSkeletalClothSectionBinding
{
	uint32 LODIndex = 0;
	int32 SectionIndex = -1;
	FString MaterialSlotName;
	uint32 FirstIndex = 0;
	uint32 IndexCount = 0;
	uint32 SourceVertexCount = 0;
	uint32 SourceIndexCount = 0;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalClothSectionBinding& Binding)
	{
		Ar << Binding.LODIndex;
		Ar << Binding.SectionIndex;
		Ar << Binding.MaterialSlotName;
		Ar << Binding.FirstIndex;
		Ar << Binding.IndexCount;
		Ar << Binding.SourceVertexCount;
		Ar << Binding.SourceIndexCount;
		return Ar;
	}
};

struct FSkeletalClothPaintData
{
	TArray<float> MaxDistanceValues;
	float ViewMin = 0.0f;
	float ViewMax = 100.0f;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalClothPaintData& Paint)
	{
		Ar << Paint.MaxDistanceValues;
		Ar << Paint.ViewMin;
		Ar << Paint.ViewMax;
		return Ar;
	}
};

inline constexpr uint32 SkeletalMeshClothPayloadCurrentVersion = 6;

struct FSkeletalClothPhysicsAssetCollisionFilterEntry
{
	FName BoneName = FName::None;
	int32 BodyIndex = -1;
	int32 ShapeIndex = -1;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalClothPhysicsAssetCollisionFilterEntry& Entry)
	{
		Ar << Entry.BoneName;
		Ar << Entry.BodyIndex;
		Ar << Entry.ShapeIndex;
		return Ar;
	}
};

struct FSkeletalClothConfig
{
	float GravityScale = 1.0f;
	float SolverFrequency = 120.0f;
	float Stiffness = 1.0f;
	float Damping = 0.04f;
	float WindScale = 1.0f;
	float DragCoefficient = 0.1f;
	float LiftCoefficient = 0.0f;
	float FluidDensity = 1.0f;
	float InertiaLinearScale = 1.0f;
	float InertiaAngularScale = 1.0f;
	bool bEnablePhysicsAssetCollision = false;
	bool bEnableWorldStaticClothCollision = false;
	bool bEnableWorldDynamicClothCollision = false;
	TArray<FSkeletalClothPhysicsAssetCollisionFilterEntry> PhysicsAssetCollisionFilter;

	void Serialize(FArchive& Ar, uint32 PayloadVersion)
	{
		Ar << GravityScale;
		Ar << SolverFrequency;
		Ar << Stiffness;
		Ar << Damping;
		if (PayloadVersion >= 2)
		{
			Ar << WindScale;
			Ar << DragCoefficient;
			Ar << LiftCoefficient;
			Ar << FluidDensity;
		}
		else if (Ar.IsLoading())
		{
			WindScale = 1.0f;
			DragCoefficient = 0.1f;
			LiftCoefficient = 0.0f;
			FluidDensity = 1.0f;
		}
		if (PayloadVersion >= 3)
		{
			Ar << InertiaLinearScale;
			Ar << InertiaAngularScale;
		}
		else if (Ar.IsLoading())
		{
			InertiaLinearScale = 1.0f;
			InertiaAngularScale = 1.0f;
		}
		if (PayloadVersion >= 4)
		{
			Ar << bEnablePhysicsAssetCollision;
			Ar << PhysicsAssetCollisionFilter;
		}
		else if (Ar.IsLoading())
		{
			bEnablePhysicsAssetCollision = false;
			PhysicsAssetCollisionFilter.clear();
		}
		if (PayloadVersion >= 5)
		{
			Ar << bEnableWorldStaticClothCollision;
		}
		else if (Ar.IsLoading())
		{
			bEnableWorldStaticClothCollision = false;
		}
		if (PayloadVersion >= 6)
		{
			Ar << bEnableWorldDynamicClothCollision;
		}
		else if (Ar.IsLoading())
		{
			bEnableWorldDynamicClothCollision = false;
		}
	}

	friend FArchive& operator<<(FArchive& Ar, FSkeletalClothConfig& Config)
	{
		Config.Serialize(Ar, SkeletalMeshClothPayloadCurrentVersion);
		return Ar;
	}
};

struct FSkeletalClothData
{
	FString Name;
	bool bEnabled = true;
	FSkeletalClothSectionBinding Binding;
	TArray<uint32> RenderVertexIndices;
	TArray<uint32> ParticleToRenderVertex;
	TArray<uint32> ClothLocalIndices;
	FSkeletalClothPaintData Paint;
	FSkeletalClothConfig Config;
	TArray<uint8> CookedFabricData;	// TODO: Use Cooked Fabric data instead calculate in every initialize

	void Serialize(FArchive& Ar, uint32 PayloadVersion)
	{
		Ar << Name;
		Ar << bEnabled;
		Ar << Binding;
		Ar << RenderVertexIndices;
		Ar << ParticleToRenderVertex;
		Ar << ClothLocalIndices;
		Ar << Paint;
		Config.Serialize(Ar, PayloadVersion);
		Ar << CookedFabricData;
	}

	friend FArchive& operator<<(FArchive& Ar, FSkeletalClothData& Cloth)
	{
		Cloth.Serialize(Ar, SkeletalMeshClothPayloadCurrentVersion);
		return Ar;
	}
};

struct FSkeletalClothLODData
{
	uint32 LODIndex = 0;
	TArray<FSkeletalClothData> Cloths;

	void Serialize(FArchive& Ar, uint32 PayloadVersion)
	{
		Ar << LODIndex;

		uint32 ClothCount = static_cast<uint32>(Cloths.size());
		Ar << ClothCount;
		if (Ar.IsLoading())
		{
			Cloths.resize(ClothCount);
		}
		for (FSkeletalClothData& Cloth : Cloths)
		{
			Cloth.Serialize(Ar, PayloadVersion);
		}
	}

	friend FArchive& operator<<(FArchive& Ar, FSkeletalClothLODData& LODData)
	{
		LODData.Serialize(Ar, SkeletalMeshClothPayloadCurrentVersion);
		return Ar;
	}
};

struct FSkeletalMeshClothPayload
{
	static constexpr uint32 CurrentVersion = SkeletalMeshClothPayloadCurrentVersion;

	uint32 Version = CurrentVersion;
	TArray<FSkeletalClothLODData> LODs;

	friend FArchive& operator<<(FArchive& Ar, FSkeletalMeshClothPayload& Payload)
	{
		if (Ar.IsSaving())
		{
			Payload.Version = CurrentVersion;
		}
		Ar << Payload.Version;

		uint32 LODCount = static_cast<uint32>(Payload.LODs.size());
		Ar << LODCount;
		if (Ar.IsLoading())
		{
			Payload.LODs.resize(LODCount);
		}
		for (FSkeletalClothLODData& LODData : Payload.LODs)
		{
			LODData.Serialize(Ar, Payload.Version);
		}
		return Ar;
	}
};

struct FSkeletalMesh
{
	FString PathFileName;
	FString SkeletonPath = "None";
	FString SkeletonAssetGuid;
	FString SkeletonCompatibilitySignature;

	TArray<FVertexPNCTBW> Vertices;
	TArray<uint32> Indices;

	TArray<FSkeletalMeshSection> Sections;
	TArray<FSkeletalMeshRange> MeshRanges;

	TArray<FBone>        Bones;
	TArray<FMorphTarget> MorphTargets;
	FSkeletalMeshClothPayload ClothPayload;

	std::unique_ptr<FMeshBuffer> RenderBuffer;

	FVector BoundsCenter = FVector(0, 0, 0);
	FVector BoundsExtent = FVector(0, 0, 0);
	bool    bBoundsValid = false;

	FSkeletalClothLODData* FindClothLOD(uint32 LODIndex);
	const FSkeletalClothLODData* FindClothLOD(uint32 LODIndex) const;
	FSkeletalClothLODData& FindOrAddClothLOD(uint32 LODIndex);
	FSkeletalClothData* FindClothData(uint32 LODIndex, const FString& Name);
	const FSkeletalClothData* FindClothData(uint32 LODIndex, const FString& Name) const;
	FSkeletalClothData* AddOrReplaceClothData(FSkeletalClothData&& ClothData);
	bool RemoveClothDataForSection(uint32 LODIndex, int32 SectionIndex);

	bool BuildClothDataFromSection(uint32 LODIndex, int32 SectionIndex, const FString& Name, FSkeletalClothData& OutCloth, FString* OutError = nullptr) const;
	bool ValidateClothData(const FSkeletalClothData& ClothData, FString* OutError = nullptr) const;
	int32 FindSectionForClothBinding(const FSkeletalClothSectionBinding& Binding) const;

	int32 FindMorphTargetIndex(const FString& TargetName) const
	{
		for (int32 Index = 0; Index < static_cast<int32>(MorphTargets.size()); ++Index)
		{
			if (MorphTargets[Index].Name == TargetName)
			{
				return Index;
			}
		}
		return -1;
	}

	const FMorphTarget* FindMorphTarget(const FString& TargetName) const
	{
		const int32 Index = FindMorphTargetIndex(TargetName);
		return Index >= 0 ? &MorphTargets[Index] : nullptr;
	}

	void NormalizeBonePoseData()
	{
		for (FBone& Bone : Bones)
		{
			Bone.ReferenceLocalPose = Bone.LocalMatrix;
			Bone.SkinBindGlobalPose = Bone.GlobalMatrix;
		}

		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
		{
			FBone& Bone = Bones[BoneIndex];
			Bone.ReferenceGlobalPose = (Bone.ParentIndex >= 0 && Bone.ParentIndex < BoneIndex)
				? Bone.ReferenceLocalPose * Bones[Bone.ParentIndex].ReferenceGlobalPose
				: Bone.ReferenceLocalPose;
		}
	}

	void CacheBounds()
	{
		bBoundsValid = false;
		if (Vertices.empty()) return;

		FVector LocalMin = Vertices[0].Position;
		FVector LocalMax = Vertices[0].Position;
		for (const FVertexPNCTBW& Vertex : Vertices)
		{
			LocalMin.X = std::min<float>(LocalMin.X, Vertex.Position.X);
			LocalMin.Y = std::min<float>(LocalMin.Y, Vertex.Position.Y);
			LocalMin.Z = std::min<float>(LocalMin.Z, Vertex.Position.Z);
			LocalMax.X = std::max<float>(LocalMax.X, Vertex.Position.X);
			LocalMax.Y = std::max<float>(LocalMax.Y, Vertex.Position.Y);
			LocalMax.Z = std::max<float>(LocalMax.Z, Vertex.Position.Z);
		}

		BoundsCenter = (LocalMin + LocalMax) * 0.5f;
		BoundsExtent = (LocalMax - LocalMin) * 0.5f;
		bBoundsValid = true;
	}
};
