#pragma once
#include "MeshComponent.h"

#include "Core/UObject/TSoftObjectPtr.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Mesh/SkeletalMesh.h"
#include "SkinnedMeshComponent.generated.h"

class UMaterial;

// ==================================================================================
// SkeletalMesh의 런타임 상태를 소유하는 기본 컴포넌트.
// Mesh/Material 경로 관리, CPU skinning 결과, bone edit pose, bounds dirty 처리를
// 한 곳에 모아 USkeletalMeshComponent가 렌더 proxy용 얇은 wrapper로 남을 수 있게 한다.
// ==================================================================================
UCLASS(HiddenInComponentList)
class USkinnedMeshComponent : public UMeshComponent
{
public:
	GENERATED_BODY(USkinnedMeshComponent)

	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override = default;

	// Mesh assignment 섹션: SkeletalMesh 교체 시 필요한 캐시와 dirty 처리를 한 번의 흐름으로 끝낸다.
	virtual void SetSkeletalMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetSkeletalMesh() const;

	// Bounds 섹션: SkeletalMesh는 local asset bounds 대신 실제 skinned vertex 기준으로 culling bounds를 만든다.
	void UpdateWorldAABB() const override;

	// Material 섹션: editor slot 경로와 runtime override 포인터를 같이 유지한다.
	void SetMaterial(int32 ElementIndex, UMaterial* InMaterial);
	UMaterial* GetMaterial(int32 ElementIndex) const;
	const TArray<UMaterial*>& GetOverrideMaterials() const { return OverrideMaterials; }

	// asset pointer는 저장하지 않고 path를 저장한 뒤 로드 후 SetSkeletalMesh 흐름으로 복원한다.
	void PostDuplicate() override;

	void PostEditProperty(const char* PropertyName) override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;

	const FString& GetSkeletalMeshPath() const { return SkeletalMesh.GetPath().ToString(); }

	// Bone edit 섹션: bone getter/setter는 edit pose를 만들고 CPU skinning/cache revision까지 갱신해야 한다.
	void EnsureBoneEditPose();
	void ResetBoneEditPose();

	FVector GetBoneLocationByIndex(int32 BoneIndex) const;
	FRotator GetBoneRotationByIndex(int32 BoneIndex) const;
	FQuat GetBoneQuatByIndex(int32 BoneIndex) const;
	FVector GetBoneScaleByIndex(int32 BoneIndex) const;
	FTransform GetBoneLocalTransformByIndex(int32 BoneIndex) const;

	void SetBoneLocationByIndex(int32 BoneIndex, const FVector& NewLocation);
	void SetBoneRotationByIndex(int32 BoneIndex, const FRotator& NewRotation);
	void SetBoneRotationByIndex(int32 BoneIndex, const FQuat& NewQuat);
	void SetBoneScaleByIndex(int32 BoneIndex, const FVector& NewScale);
	void SetBoneLocalTransformByIndex(int32 BoneIndex, const FTransform& NewLocalTransform);
	void SetBoneLocalTransformByArray(const TArray<FMatrix>& NewLocalMatrices);

	void GetCurrentBoneGlobalTransforms(TArray<FTransform>& OutGlobals) const;
	void GetCurrentBoneGlobalMatrices(TArray<FMatrix>& OutGlobals) const;
	const TArray<FMatrix>& GetCurrentSkinMatrices() const;
	uint64 GetSkinMatrixRevision() const { return SkinMatrixRevision; }
	const TArray<FVertexPNCTBW>& GetSkinnedVertices() const { return SkinnedVertices; }
	uint64 GetSkinnedRevision() const { return SkinnedRevision; }
	void UpdateSkinMatrices() const;
	void EnsureCPUSkinnedVertices() const;
	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;

	bool GetIgnoreRootMotion() const { return bIgnoreRootMotion; }
	void SetIgnoreRootMotion(bool bIgnore) { bIgnoreRootMotion = bIgnore; }

protected:
	// Tick/skinning 섹션: skin matrix와 CPU vertex skinning을 분리해 필요한 경로만 계산한다.
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	void InitSkinningCache();
	void UpdateCPUSkinning();
	// --- SkeletalMesh AABB Section ---
	// Vertices로 하지 않고, Bone Local Bounds를 통해 근사한다.
	void ResetBoneBounds() const;
	void BuildBoneBounds() const;
	void UpdateWorldAABBFromBoneBounds() const;
	void ExpandWorldBounds(const FBoundingBox& LocalBounds, const FMatrix& LocalToWorld, FBoundingBox& WorldBounds) const;

	void BuildBoneEditGlobalTransforms(TArray<FTransform>& OutGlobals) const;
	void BuildBoneEditGlobalMatrices(TArray<FMatrix>& OutGlobals) const;

protected:
	// Mesh/material state는 SetSkeletalMesh와 PostEditProperty가 같은 경로를 쓰도록 여기서 소유한다.
	UPROPERTY(Edit, Category="Mesh", DisplayName="Skeletal Mesh", Type=SoftObject, Class=USkeletalMesh)
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
	TArray<UMaterial*> OverrideMaterials;
	UPROPERTY(Edit, FixedSize, Category="Materials", DisplayName="Materials")
	TArray<FMaterialSlot> MaterialSlots;

	// Bone edit pose는 asset 원본 bone을 직접 바꾸지 않고 component-local override로만 유지한다.
	TArray<FMatrix> BoneEditLocalMatrices;
	bool bUseBoneEditPose = false;

	// SceneProxy는 이 결과와 revision만 보고 dynamic vertex buffer를 갱신한다.
	mutable TArray<FVertexPNCTBW> SkinnedVertices;
	mutable uint64 SkinnedRevision = 0;
	mutable bool bSkinnedVerticesDirty = true;

	// GPU/CPU skinning이 공유하는 skin matrix cache.
	mutable TArray<FMatrix> CurrentSkinMatrices;
	mutable uint64 SkinMatrixRevision = 0;

	uint32 TargetRootBoneIndex = 0;
	bool bIgnoreRootMotion = true;

	// SkeletalMesh AABB는 전체 vertex CPU skinning 대신 bone별 influence bounds로 보수적으로 근사한다.
	mutable TArray<FBoundingBox> BoneBounds;
	mutable FBoundingBox UnweightedBounds;
	mutable bool bBoneBoundsDirty = true;
};
