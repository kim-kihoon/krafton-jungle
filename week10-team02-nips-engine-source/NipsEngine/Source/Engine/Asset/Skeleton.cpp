#include "Skeleton.h"

DEFINE_CLASS(USkeleton, UObject)

USkeleton::~USkeleton()
{
	delete SkeletonData;
	SkeletonData = nullptr;
}

void USkeleton::SetSkeletonData(FSkeleton* InSkeletonData)
{
	if (SkeletonData == InSkeletonData)
	{
		return;
	}

	delete SkeletonData;
	SkeletonData = InSkeletonData;
}

FSkeleton* USkeleton::GetSkeletonData()
{
	return SkeletonData;
}

const FSkeleton* USkeleton::GetSkeletonData() const
{
	return SkeletonData;
}

const FString& USkeleton::GetAssetPathFileName() const
{
	static const FString Empty;
	return SkeletonData ? SkeletonData->PathFileName : Empty;
}

const TArray<FSkeletalBone>& USkeleton::GetBones() const
{
	static const TArray<FSkeletalBone> Empty;
	return SkeletonData ? SkeletonData->Bones : Empty;
}

const TArray<FMatrix>& USkeleton::GetInverseBindPoseMatrices() const
{
	static const TArray<FMatrix> Empty;
	return SkeletonData ? SkeletonData->InverseBindPoseMatrices : Empty;
}

bool USkeleton::HasValidSkeleton() const
{
	return SkeletonData != nullptr &&
		!SkeletonData->Bones.empty() &&
		SkeletonData->Bones.size() == SkeletonData->InverseBindPoseMatrices.size();
}
