#pragma once

#include "Asset/SkeletalMeshTypes.h"
#include "Object/Object.h"

struct FSkeleton
{
	FString PathFileName;
	TArray<FSkeletalBone> Bones;
	TArray<FMatrix> InverseBindPoseMatrices;
};

class USkeleton : public UObject
{
public:
	DECLARE_CLASS(USkeleton, UObject)

	USkeleton() = default;
	~USkeleton() override;

	void SetSkeletonData(FSkeleton* InSkeletonData);

	FSkeleton* GetSkeletonData();
	const FSkeleton* GetSkeletonData() const;

	const FString& GetAssetPathFileName() const;
	const TArray<FSkeletalBone>& GetBones() const;
	const TArray<FMatrix>& GetInverseBindPoseMatrices() const;

	bool HasValidSkeleton() const;

private:
	FSkeleton* SkeletonData = nullptr;
};
