#pragma once
#include "Animation/AnimTypes.h"

struct FSkeletonAsset;
class UAnimDataModel;

namespace FAnimationRuntime
{
	// FPoseContext 완성
	void GetPoseAtTime(
		const UAnimDataModel* DataModel,
		float Time,
		FPoseContext& OutPose);

	// 두 포즈 블렌딩
	void BlendTwoPoses(
		const FPoseContext& A,
		const FPoseContext& B,
		float Alpha,
		FPoseContext& OutPose);

	/** Skeleton 계층을 따라 local pose를 component-local global matrix 배열로 변환합니다. */
	void BuildPoseGlobalMatrices(
		const FPoseContext& Pose,
		const FSkeletonAsset* SkeletonAsset,
		TArray<FMatrix>& OutGlobalMatrices);

	/** component-local global matrix 배열을 다시 Skeleton bone 기준 local pose로 변환합니다. */
	void ConvertGlobalMatricesToLocalPose(
		const TArray<FMatrix>& GlobalMatrices,
		const FSkeletonAsset* SkeletonAsset,
		FPoseContext& OutPose);

	bool SolveTwoBoneIK(
		FPoseContext& Pose,
		const FSkeletonAsset* SkeletonAsset,
		const FTwoBoneIKChain& Chain);
};
