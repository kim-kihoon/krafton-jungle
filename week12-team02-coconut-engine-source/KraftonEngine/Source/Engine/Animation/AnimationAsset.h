#pragma once

#include "Object/Object.h"

class USkeleton;
struct FSkeletonAsset;

/**
 * 애니메이션 에셋 공통 기반입니다.
 * 애니메이션이 어떤 Skeleton을 기준으로 평가되는지 관리합니다.
 */
class UAnimationAsset : public UObject
{
public:
	DECLARE_CLASS(UAnimationAsset, UObject)

	UAnimationAsset() = default;
	~UAnimationAsset() override = default;

	void Serialize(FArchive& Ar) override;

	const FString& GetAssetPathFileName() const { return AssetPathFileName; }
	void SetAssetPathFileName(const FString& InPathFileName) { AssetPathFileName = InPathFileName; }

	void SetSkeleton(USkeleton* InSkeleton);
	USkeleton* GetSkeleton() const { return Skeleton; }
	FSkeletonAsset* GetSkeletonAsset() const;
	const FString& GetSkeletonPath() const { return SkeletonPath; }
	void SetSkeletonPath(const FString& InSkeletonPath);
	bool ResolveSkeleton();

	virtual float GetPlayLength() const { return 0.0f; }

private:
	FString AssetPathFileName = "None";
	FString SkeletonPath;
	USkeleton* Skeleton = nullptr;
};
