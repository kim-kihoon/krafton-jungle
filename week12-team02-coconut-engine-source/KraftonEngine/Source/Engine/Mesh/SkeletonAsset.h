#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Serialization/Archive.h"

inline void SerializeSkeletonMatrix(FArchive& Ar, FMatrix& Matrix)
{
	Ar.Serialize(Matrix.Data, sizeof(float) * 16);
}

struct FBone
{
	FString Name;
	int32 ParentIndex = -1;

	FMatrix LocalMatrix = FMatrix::Identity;
	FMatrix GlobalMatrix = FMatrix::Identity;
	FMatrix InverseBindPoseMatrix = FMatrix::Identity;

	friend FArchive& operator<<(FArchive& Ar, FBone& Bone)
	{
		Ar << Bone.Name;
		Ar << Bone.ParentIndex;
		SerializeSkeletonMatrix(Ar, Bone.LocalMatrix);
		SerializeSkeletonMatrix(Ar, Bone.GlobalMatrix);
		SerializeSkeletonMatrix(Ar, Bone.InverseBindPoseMatrix);
		return Ar;
	}
};

struct FSkeletonAsset
{
	FString PathFileName;
	TArray<FBone> Bones;

	void Serialize(FArchive& Ar)
	{
		Ar << PathFileName;
		Ar << Bones;
	}
};
