#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Serialization/Archive.h"

inline void SerializeReferenceSkeletonMatrix(FArchive& Ar, FMatrix& Matrix)
{
    Ar.Serialize(Matrix.Data, sizeof(float) * 16);
}

struct FReferenceBone
{
    FString Name;
    int32   ParentIndex = -1;

    FMatrix LocalBindPose   = FMatrix::Identity;
    FMatrix GlobalBindPose  = FMatrix::Identity;
    FMatrix InverseBindPose = FMatrix::Identity;

    friend FArchive& operator<<(FArchive& Ar, FReferenceBone& Bone)
    {
        Ar << Bone.Name;
        Ar << Bone.ParentIndex;
        SerializeReferenceSkeletonMatrix(Ar, Bone.LocalBindPose);
        SerializeReferenceSkeletonMatrix(Ar, Bone.GlobalBindPose);
        SerializeReferenceSkeletonMatrix(Ar, Bone.InverseBindPose);
        return Ar;
    }
};

struct FReferenceSkeleton
{
    TArray<FReferenceBone> Bones;

    int32 GetNumBones() const
    {
        return static_cast<int32>(Bones.size());
    }

    int32 FindBoneIndex(const FString& BoneName) const
    {
        for (int32 BoneIndex = 0; BoneIndex < GetNumBones(); ++BoneIndex)
        {
            if (Bones[BoneIndex].Name == BoneName)
            {
                return BoneIndex;
            }
        }

        return -1;
    }

    friend FArchive& operator<<(FArchive& Ar, FReferenceSkeleton& Skeleton)
    {
        Ar << Skeleton.Bones;
        return Ar;
    }
};

struct FSkeletalMeshSocket
{
    FName    Name = FName::None;
    FName    BoneName = FName::None;
    FVector  RelativeLocation = FVector::ZeroVector;
    FRotator RelativeRotation;
    FVector  RelativeScale = FVector(1.0f, 1.0f, 1.0f);

    FMatrix GetRelativeTransform() const
    {
        return FTransform(RelativeLocation, RelativeRotation, RelativeScale).ToMatrix();
    }

    friend FArchive& operator<<(FArchive& Ar, FSkeletalMeshSocket& Socket)
    {
        Ar << Socket.Name;
        Ar << Socket.BoneName;
        Ar << Socket.RelativeLocation;
        Ar << Socket.RelativeRotation;
        Ar << Socket.RelativeScale;
        return Ar;
    }
};

// Skeleton을 참조하는 모든 에셋이 공통으로 저장하는 연결 정보.
// Path는 로딩용, AssetGuid는 같은 Skeleton 에셋 판별용, CompatibilitySignature는 구조 호환성 판별용이다.
struct FSkeletonBinding
{
    FString SkeletonPath = "None";
    FString SkeletonAssetGuid;
    FString CompatibilitySignature;

    bool HasSkeletonPath() const
    {
        return !SkeletonPath.empty() && SkeletonPath != "None";
    }

    bool HasAssetGuid() const
    {
        return !SkeletonAssetGuid.empty();
    }

    bool HasCompatibilitySignature() const
    {
        return !CompatibilitySignature.empty();
    }

    void Reset()
    {
        SkeletonPath = "None";
        SkeletonAssetGuid.clear();
        CompatibilitySignature.clear();
    }

    friend FArchive& operator<<(FArchive& Ar, FSkeletonBinding& Binding)
    {
        Ar << Binding.SkeletonPath;
        Ar << Binding.SkeletonAssetGuid;
        Ar << Binding.CompatibilitySignature;
        return Ar;
    }
};

// FBX source skeleton의 bone index를 target USkeleton의 bone index로 변환하는 import-time remap 표.
// 저장 포맷에는 남기지 않는다. Mesh/Animation을 기존 Skeleton에 붙이는 동안만 사용한다.
struct FSkeletonBoneRemap
{
    TArray<int32> SourceToTargetBone;
    TArray<int32> TargetToSourceBone;

    void Reset()
    {
        SourceToTargetBone.clear();
        TargetToSourceBone.clear();
    }

    bool IsValidSourceBone(int32 SourceBoneIndex) const
    {
        return SourceBoneIndex >= 0 && SourceBoneIndex < static_cast<int32>(SourceToTargetBone.size()) && SourceToTargetBone[SourceBoneIndex] >= 0;
    }

    int32 GetTargetBoneIndex(int32 SourceBoneIndex) const
    {
        return IsValidSourceBone(SourceBoneIndex) ? SourceToTargetBone[SourceBoneIndex] : -1;
    }
};

enum class ESkeletonCompatibilityResult : uint8
{
    Incompatible = 0,
    ExactSkeleton,
    SameStructure,
    Retargetable
};

struct FSkeletonCompatibilityReport
{
    ESkeletonCompatibilityResult Result = ESkeletonCompatibilityResult::Incompatible;
    FString                      Reason;
    TArray<FString>              MissingBones;
    TArray<FString>              ExtraBones;
    TArray<FString>              ParentMismatchBones;

    bool IsExactCompatible() const
    {
        return Result == ESkeletonCompatibilityResult::ExactSkeleton;
    }

    bool IsCompatible(bool bAllowSameStructure = false) const
    {
        if (Result == ESkeletonCompatibilityResult::ExactSkeleton)
        {
            return true;
        }

        if (bAllowSameStructure && Result == ESkeletonCompatibilityResult::SameStructure)
        {
            return true;
        }

        return Result == ESkeletonCompatibilityResult::Retargetable;
    }
};
