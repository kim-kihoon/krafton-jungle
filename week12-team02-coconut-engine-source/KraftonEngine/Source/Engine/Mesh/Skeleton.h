#pragma once

#include "Object/Object.h"
#include "Mesh/SkeletonAsset.h"
#include "Skeleton.generated.h"

UCLASS()
class USkeleton : public UObject
{
public:
	GENERATED_BODY(USkeleton)

	USkeleton() = default;
	~USkeleton() override = default;

	void Serialize(FArchive& Ar);

	const FString& GetAssetPathFileName() const override { return AssetPathFileName; }
	void SetAssetPathFileName(const FString& InPathFileName) { AssetPathFileName = InPathFileName; }

	void SetSkeletonAsset(FSkeletonAsset* InAsset);
	FSkeletonAsset* GetSkeletonAsset() const;

private:
	FString AssetPathFileName = "None";
	FSkeletonAsset* SkeletonAsset = nullptr;
};
