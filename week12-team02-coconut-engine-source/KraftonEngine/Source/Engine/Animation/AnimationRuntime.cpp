#include "AnimationRuntime.h"
#include "Animation/AnimDataModel.h"
#include "Mesh/SkeletonAsset.h"
#include <cmath>

namespace
{
	constexpr float TwoBoneIKEpsilon = 1.e-4f;

	/** 본 인덱스가 스켈레톤 범위 안에 있는지 검사합니다. */
	bool IsValidBoneIndex(const FSkeletonAsset* SkeletonAsset, int32 BoneIndex)
	{
		return SkeletonAsset
			&& BoneIndex >= 0
			&& BoneIndex < static_cast<int32>(SkeletonAsset->Bones.size());
	}

	/** 어떤 본이 특정 Root 본의 자식 계층인지 확인합니다. */
	bool IsDescendantOrSelf(const FSkeletonAsset* SkeletonAsset, int32 BoneIndex, int32 RootBoneIndex)
	{
		if (!IsValidBoneIndex(SkeletonAsset, BoneIndex) || !IsValidBoneIndex(SkeletonAsset, RootBoneIndex))
		{
			return false;
		}

		int32 CurrentIndex = BoneIndex;
		while (CurrentIndex >= 0 && CurrentIndex < static_cast<int32>(SkeletonAsset->Bones.size()))
		{
			if (CurrentIndex == RootBoneIndex)
			{
				return true;
			}

			CurrentIndex = SkeletonAsset->Bones[CurrentIndex].ParentIndex;
		}

		return false;
	}

	/** 벡터를 안전하게 정규화합니다. */
	FVector GetSafeNormal(const FVector& Vector, const FVector& Fallback)
	{
		if (Vector.IsNearlyZero(TwoBoneIKEpsilon))
		{
			return Fallback;
		}

		return Vector.Normalized();
	}

	/**
	 * 주어진 방향과 수직인 임의의 방향을 구합니다.
	 * Pole 방향이 애매할 때 fallback으로 씁니다.
	 */
	FVector GetAnyPerpendicularVector(const FVector& Direction)
	{
		FVector Axis = Direction.Cross(FVector::UpVector);
		if (Axis.IsNearlyZero(TwoBoneIKEpsilon))
		{
			Axis = Direction.Cross(FVector::RightVector);
		}

		return GetSafeNormal(Axis, FVector::ForwardVector);
	}

	/**
	 *  한 방향 벡터를 다른 방향 벡터로 돌리는 쿼터니언을 만듭니다.
	 *  Rott/Mid 본을 "현재 방향"에서 "원하는 방향"으로 회전시키는데 사용합니다.
	 */
	FQuat MakeRotationBetweenVectors(const FVector& From, const FVector& To)
	{
		const FVector FromNormal = GetSafeNormal(From, FVector::ForwardVector);
		const FVector ToNormal = GetSafeNormal(To, FVector::ForwardVector);
		const float Dot = Clamp(FromNormal.Dot(ToNormal), -1.0f, 1.0f);

		if (Dot > 1.0f - TwoBoneIKEpsilon)
		{
			return FQuat::Identity;
		}

		if (Dot < -1.0f + TwoBoneIKEpsilon)
		{
			return FQuat::FromAxisAngle(GetAnyPerpendicularVector(FromNormal), FMath::Pi);
		}

		FVector Axis = FromNormal.Cross(ToNormal);
		Axis.Normalize();

		FQuat Rotation = FQuat::FromAxisAngle(Axis, acosf(Dot));
		Rotation.Normalize();
		return Rotation;
	}

	/**
	 * 특정 Pivot 위치를 기준으로 회전하는 행렬을 만듭니다.
	 * EX: Root 위치를 기준으로 팔 전체를 회전.
	 */
	FMatrix MakePivotRotationMatrix(const FVector& Pivot, const FQuat& Rotation)
	{
		return FMatrix::MakeTranslationMatrix(Pivot * -1.0f)
			* Rotation.ToMatrix()
			* FMatrix::MakeTranslationMatrix(Pivot);
	}

	/**
	 * 특정 본과 그 모든 자식 본들의 글로벌 행렬에 회전을 적용합니다.
	 * Root를 돌릴 때는 Root 아래 전체 체인, Mid를 돌릴 때는 Mid 아래 체인이 같이 움직여야 해서 필요합니다.
	 */
	void ApplyRotationToSubtree(
		TArray<FMatrix>& GlobalMatrices,
		const FSkeletonAsset* SkeletonAsset,
		int32 RootBoneIndex,
		const FVector& Pivot,
		const FQuat& Rotation)
	{
		const FMatrix PivotRotation = MakePivotRotationMatrix(Pivot, Rotation);
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(GlobalMatrices.size()); ++BoneIndex)
		{
			if (IsDescendantOrSelf(SkeletonAsset, BoneIndex, RootBoneIndex))
			{
				GlobalMatrices[BoneIndex] *= PivotRotation;
			}
		}
	}
}

void FAnimationRuntime::GetPoseAtTime(const UAnimDataModel* DataModel, float Time, FPoseContext& OutPose)
{
	if (!DataModel)
		return;

	const TArray<FBoneAnimationTrack>& Tracks = DataModel->GetBoneAnimationTracks();
	OutPose.BoneLocalTransforms.resize(Tracks.size());

	for (uint32 Idx = 0; Idx < static_cast<uint32>(Tracks.size()); ++Idx)
	{
		FTransform Result;
		DataModel->EvaluateBoneTrackTransform(Tracks[Idx], Time, Result, FTransform());
		OutPose.BoneLocalTransforms[Idx] = Result;
	}
}

void FAnimationRuntime::BlendTwoPoses(const FPoseContext& A, const FPoseContext& B, float Alpha, FPoseContext& OutPose)
{
	const uint32 NumBones = static_cast<uint32>(A.BoneLocalTransforms.size());
	if (NumBones != static_cast<uint32>(B.BoneLocalTransforms.size()))
		return;

	OutPose.BoneLocalTransforms.resize(NumBones);

	for (uint32 i = 0; i < NumBones; ++i)
	{
		const FTransform& TA = A.BoneLocalTransforms[i];
		const FTransform& TB = B.BoneLocalTransforms[i];

		OutPose.BoneLocalTransforms[i] = FTransform(
			FVector::Lerp(TA.Location, TB.Location, Alpha),
			FQuat::Slerp(TA.Rotation, TB.Rotation, Alpha),
			FVector::Lerp(TA.Scale, TB.Scale, Alpha)
		);
	}
}

void FAnimationRuntime::BuildPoseGlobalMatrices(
	const FPoseContext& Pose,
	const FSkeletonAsset* SkeletonAsset,
	TArray<FMatrix>& OutGlobalMatrices)
{
	OutGlobalMatrices.clear();

	if (!SkeletonAsset)
	{
		return;
	}

	const int32 BoneCount = static_cast<int32>(SkeletonAsset->Bones.size());
	OutGlobalMatrices.resize(BoneCount);

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FMatrix LocalMatrix = BoneIndex < static_cast<int32>(Pose.BoneLocalTransforms.size())
			? Pose.BoneLocalTransforms[BoneIndex].ToMatrix()
			: SkeletonAsset->Bones[BoneIndex].LocalMatrix;

		const int32 ParentIndex = SkeletonAsset->Bones[BoneIndex].ParentIndex;
		OutGlobalMatrices[BoneIndex] =
			(ParentIndex >= 0 && ParentIndex < BoneCount)
				? LocalMatrix * OutGlobalMatrices[ParentIndex]
				: LocalMatrix;
	}
}

void FAnimationRuntime::ConvertGlobalMatricesToLocalPose(
	const TArray<FMatrix>& GlobalMatrices,
	const FSkeletonAsset* SkeletonAsset,
	FPoseContext& OutPose)
{
	OutPose.Reset();

	if (!SkeletonAsset)
	{
		return;
	}

	const int32 BoneCount = static_cast<int32>(SkeletonAsset->Bones.size());
	OutPose.BoneLocalTransforms.resize(BoneCount);

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		FMatrix LocalMatrix = SkeletonAsset->Bones[BoneIndex].LocalMatrix;

		if (BoneIndex < static_cast<int32>(GlobalMatrices.size()))
		{
			const int32 ParentIndex = SkeletonAsset->Bones[BoneIndex].ParentIndex;
			if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(GlobalMatrices.size()))
			{
				LocalMatrix = GlobalMatrices[BoneIndex] * GlobalMatrices[ParentIndex].GetInverse();
			}
			else
			{
				LocalMatrix = GlobalMatrices[BoneIndex];
			}
		}

		OutPose.BoneLocalTransforms[BoneIndex] = FTransform(LocalMatrix);
	}
}

bool FAnimationRuntime::SolveTwoBoneIK(
	FPoseContext& Pose,
	const FSkeletonAsset* SkeletonAsset,
	const FTwoBoneIKChain& Chain)
{
	/**
	 * Root, Mid, End Bone이 전부 Skeleton에 있는지 검사
	 * 해당 로직은 Root, Mid, End 본을 등록할 때 수행하도록 추후 바꿔야함
	 */
	if (!IsValidBoneIndex(SkeletonAsset, Chain.RootBoneIndex)
		|| !IsValidBoneIndex(SkeletonAsset, Chain.MidBoneIndex)
		|| !IsValidBoneIndex(SkeletonAsset, Chain.EndBoneIndex))
	{
		return false;
	}

	/**
	 * Mid, End Bone이 각각 부모본의 계층인지 확인하는 검사 로직
	 * 해당 로직은 Root, Mid, End 본을 등록할 때 수행하도록 추후 바꿔야함.
	 */
	if (!IsDescendantOrSelf(SkeletonAsset, Chain.MidBoneIndex, Chain.RootBoneIndex)
		|| !IsDescendantOrSelf(SkeletonAsset, Chain.EndBoneIndex, Chain.MidBoneIndex))
	{
		return false;
	}

	TArray<FMatrix> GlobalMatrices;
	BuildPoseGlobalMatrices(Pose, SkeletonAsset, GlobalMatrices);

	const FVector RootPosition = GlobalMatrices[Chain.RootBoneIndex].GetLocation();
	const FVector MidPosition = GlobalMatrices[Chain.MidBoneIndex].GetLocation();
	const FVector EndPosition = GlobalMatrices[Chain.EndBoneIndex].GetLocation();

	const float UpperLength = FVector::Distance(RootPosition, MidPosition);
	const float LowerLength = FVector::Distance(MidPosition, EndPosition);
	if (UpperLength <= TwoBoneIKEpsilon || LowerLength <= TwoBoneIKEpsilon)
	{
		return false;
	}

	FVector RootToTarget = Chain.TargetPosition - RootPosition;
	FVector TargetDirection = GetSafeNormal(
		RootToTarget,
		GetSafeNormal(EndPosition - RootPosition, FVector::ForwardVector));

	float TargetDistance = RootToTarget.Length();
	if (TargetDistance <= TwoBoneIKEpsilon)
	{
		TargetDistance = TwoBoneIKEpsilon;
	}

	const float MinReach = std::fabsf(UpperLength - LowerLength) + TwoBoneIKEpsilon;
	const float MaxReach = UpperLength + LowerLength - TwoBoneIKEpsilon;
	TargetDistance = Clamp(TargetDistance, MinReach, MaxReach);

	const FVector ClampedTargetPosition = RootPosition + TargetDirection * TargetDistance;
	FVector PoleDirection = Chain.PolePosition - RootPosition;
	PoleDirection = PoleDirection - TargetDirection * PoleDirection.Dot(TargetDirection);

	if (PoleDirection.IsNearlyZero(TwoBoneIKEpsilon))
	{
		PoleDirection = MidPosition - RootPosition;
		PoleDirection = PoleDirection - TargetDirection * PoleDirection.Dot(TargetDirection);
	}

	PoleDirection = GetSafeNormal(PoleDirection, GetAnyPerpendicularVector(TargetDirection));

	const float TargetDistanceSq = TargetDistance * TargetDistance;
	const float UpperLengthSq = UpperLength * UpperLength;
	const float LowerLengthSq = LowerLength * LowerLength;
	const float JointDistanceOnTarget = (TargetDistanceSq + UpperLengthSq - LowerLengthSq) / (2.0f * TargetDistance);

	float JointHeightSq = UpperLengthSq - JointDistanceOnTarget * JointDistanceOnTarget;
	if (JointHeightSq < 0.0f)
	{
		JointHeightSq = 0.0f;
	}

	const FVector DesiredMidPosition =
		RootPosition
		+ TargetDirection * JointDistanceOnTarget
		+ PoleDirection * sqrtf(JointHeightSq);

	const FQuat RootDelta = MakeRotationBetweenVectors(
		MidPosition - RootPosition,
		DesiredMidPosition - RootPosition);
	ApplyRotationToSubtree(GlobalMatrices, SkeletonAsset, Chain.RootBoneIndex, RootPosition, RootDelta);

	const FVector RotatedMidPosition = GlobalMatrices[Chain.MidBoneIndex].GetLocation();
	const FVector RotatedEndPosition = GlobalMatrices[Chain.EndBoneIndex].GetLocation();
	const FQuat MidDelta = MakeRotationBetweenVectors(
		RotatedEndPosition - RotatedMidPosition,
		ClampedTargetPosition - RotatedMidPosition);
	ApplyRotationToSubtree(GlobalMatrices, SkeletonAsset, Chain.MidBoneIndex, RotatedMidPosition, MidDelta);

	ConvertGlobalMatricesToLocalPose(GlobalMatrices, SkeletonAsset, Pose);
	return true;
}
