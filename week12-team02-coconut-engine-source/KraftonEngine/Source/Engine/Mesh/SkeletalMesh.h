#pragma once

#include "Object/Object.h"
#include "SkeletalMeshAsset.h"
#include "SkeletalMesh.generated.h"

class USkeleton;
struct FSkeletonAsset;

UCLASS()
class USkeletalMesh : public UObject
{
public:
	GENERATED_BODY(USkeletalMesh)

	USkeletalMesh() = default;
	~USkeletalMesh() override;

	void Serialize(FArchive& Ar);

	const FString& GetAssetPathFileName() const override { return AssetPathFileName; }
	void SetAssetPathFileName(const FString& InPathFileName) { AssetPathFileName = InPathFileName; }

	void SetSkeletalMeshAsset(FSkeletalMesh* InMesh);
	FSkeletalMesh* GetSkeletalMeshAsset() const;
	void SetSkeleton(USkeleton* InSkeleton);
	USkeleton* GetSkeleton() const;
	FSkeletonAsset* GetSkeletonAsset() const;
	bool ResolveSkeleton();
	void SetSkeletalMaterials(TArray<FSkeletalMaterial>&& InMaterials);
	const TArray<FSkeletalMaterial>& GetSkeletalMaterials() const;

	void InitResources(ID3D11Device* InDevice);

private:
	void CacheSectionMaterialIndices();

private:
	FString AssetPathFileName = "None";

	FSkeletalMesh* SkeletalMeshAsset = nullptr;
	USkeleton* Skeleton = nullptr;
	TArray<FSkeletalMaterial> SkeletalMaterials;
};
