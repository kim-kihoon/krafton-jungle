#pragma once
#include "Component/MeshComponent.h"

#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Render/Types/ViewTypes.h"

#include "Source/Engine/Component/Primitive/SkinnedMeshComponent.generated.h"
struct FSkeletalMesh;
class USkeletalMesh;
class UMaterial;
class USkeleton;

// ==================================================================================
// SkeletalMesh의 런타임 상태를 소유하는 기본 컴포넌트.
// Mesh/Material 경로 관리, CPU skinning 결과, bone edit pose, bounds dirty 처리를
// 한 곳에 모아 USkeletalMeshComponent가 렌더 proxy용 얇은 wrapper로 남을 수 있게 한다.
// ==================================================================================
UCLASS()
class USkinnedMeshComponent : public UMeshComponent
{
public:
	GENERATED_BODY()
	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override = default;

	// Mesh assignment 섹션: SkeletalMesh 교체 시 필요한 캐시와 dirty 처리를 한 번의 흐름으로 끝낸다.
	UFUNCTION(Callable, Category="Mesh")
	virtual void SetSkeletalMesh(USkeletalMesh* InMesh);
	UFUNCTION(Callable, Exec, Category="Mesh")
	bool SetSkeletalMeshByPath(const FString& InPath);
	UFUNCTION(Callable, Exec, Category="Mesh")
	void ClearSkeletalMesh();
	UFUNCTION(Pure, Category="Mesh")
	USkeletalMesh* GetSkeletalMesh() const;
	UFUNCTION(Pure, Category="Mesh")
	FString GetSkeletalMeshPathValue() const { return SkeletalMeshPath.ToString(); }

	// Bounds 섹션: SkeletalMesh는 local asset bounds 대신 실제 skinned vertex 기준으로 culling bounds를 만든다.
	void UpdateWorldAABB() const override;
	bool HasSocket(const FName& SocketName) const override;
	FTransform GetSocketTransform(const FName& SocketName) const override;

	// Material 섹션: editor slot 경로와 runtime override 포인터를 같이 유지한다.
	UFUNCTION(Callable, Category="Materials")
	void SetMaterial(int32 ElementIndex, UMaterial* InMaterial);
	UFUNCTION(Callable, Exec, Category="Materials")
	bool SetMaterialByPath(int32 ElementIndex, const FString& MaterialPath);
	UFUNCTION(Pure, Category="Materials")
	UMaterial* GetMaterial(int32 ElementIndex) const;
	UFUNCTION(Pure, Category="Materials")
	FString GetMaterialPath(int32 ElementIndex) const;
	UFUNCTION(Pure, Category="Materials")
	int32 GetMaterialSlotCount() const { return static_cast<int32>(MaterialSlots.size()); }
	const TArray<UMaterial*>& GetOverrideMaterials() const { return OverrideMaterials; }

	// Serialization/editor 섹션: asset pointer는 저장하지 않고 path를 저장한 뒤 로드 후 SetSkeletalMesh 흐름으로 복원한다.
	void PostDuplicate() override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	void PostEditProperty(const char* PropertyName) override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;

	const FString& GetSkeletalMeshPath() const { return SkeletalMeshPath.ToString(); }

	// Bone edit 섹션: bone getter/setter는 edit pose를 만들고 CPU skinning/cache revision까지 갱신해야 한다.
	UFUNCTION(Callable, Category="Mesh|Bone")
	void EnsureBoneEditPose();
	UFUNCTION(Callable, Category="Mesh|Bone")
	void ResetBoneEditPose();

	UFUNCTION(Pure, Category="Mesh|Bone")
	FVector GetBoneLocationByIndex(int32 BoneIndex) const;
	UFUNCTION(Pure, Category="Mesh|Bone")
	FRotator GetBoneRotationByIndex(int32 BoneIndex) const;
	FQuat GetBoneQuatByIndex(int32 BoneIndex) const;
	UFUNCTION(Pure, Category="Mesh|Bone")
	FVector GetBoneScaleByIndex(int32 BoneIndex) const;
	UFUNCTION(Pure, Category="Mesh|Bone")
	FTransform GetBoneLocalTransformByIndex(int32 BoneIndex) const;
	UFUNCTION(Pure, Category="Mesh|Bone")
	FTransform GetBoneEditBaseLocalTransformByIndex(int32 BoneIndex) const;
	UFUNCTION(Pure, Category="Mesh|Bone")
	int32 FindBoneIndex(const FName& BoneName) const;
	UFUNCTION(Pure, Category="Mesh|Bone")
	FVector GetBoneLocationByName(const FName& BoneName) const { return GetBoneLocationByIndex(FindBoneIndex(BoneName)); }
	UFUNCTION(Pure, Category="Mesh|Bone")
	FRotator GetBoneRotationByName(const FName& BoneName) const { return GetBoneRotationByIndex(FindBoneIndex(BoneName)); }
	UFUNCTION(Pure, Category="Mesh|Bone")
	FVector GetBoneScaleByName(const FName& BoneName) const { return GetBoneScaleByIndex(FindBoneIndex(BoneName)); }
	UFUNCTION(Callable, Category="Mesh|Bone")
	bool SetBoneWorldTransformByName(const FName& BoneName, const FTransform& WorldTransform);

	UFUNCTION(Callable, Category="Mesh|Bone")
	void SetBoneLocationByIndex(int32 BoneIndex, const FVector& NewLocation);
	UFUNCTION(Callable, Category="Mesh|Bone")
	void SetBoneLocationByName(const FName& BoneName, const FVector& NewLocation) { SetBoneLocationByIndex(FindBoneIndex(BoneName), NewLocation); }
	UFUNCTION(Callable, Category="Mesh|Bone")
	void SetBoneRotationByIndex(int32 BoneIndex, const FRotator& NewRotation);
	UFUNCTION(Callable, Category="Mesh|Bone")
	void SetBoneRotationByName(const FName& BoneName, const FRotator& NewRotation) { SetBoneRotationByIndex(FindBoneIndex(BoneName), NewRotation); }
	void SetBoneRotationByIndex(int32 BoneIndex, const FQuat& NewQuat);
	UFUNCTION(Callable, Category="Mesh|Bone")
	void SetBoneScaleByIndex(int32 BoneIndex, const FVector& NewScale);
	UFUNCTION(Callable, Category="Mesh|Bone")
	void SetBoneScaleByName(const FName& BoneName, const FVector& NewScale) { SetBoneScaleByIndex(FindBoneIndex(BoneName), NewScale); }
	UFUNCTION(Callable, Category="Mesh|Bone")
	void SetBoneLocalTransformByIndex(int32 BoneIndex, const FTransform& NewLocalTransform);
	UFUNCTION(Callable, Category="Mesh|Bone")
	void SetBoneEditBaseLocalTransformByIndex(int32 BoneIndex, const FTransform& NewLocalTransform);

	void SetBoneLocalTransforms(const TArray<FTransform>& LocalPose);
	void SetAnimationPose(const TArray<FTransform>& LocalPose, const TArray<float>& InMorphTargetWeights);
	UFUNCTION(Callable, Category="Mesh|Bone")
	void ApplyBoneEditBasePose();

	UFUNCTION(Pure, Category="Mesh|MorphTarget")
	int32 FindMorphTargetIndex(const FString& TargetName) const;
	UFUNCTION(Callable, Category="Mesh|MorphTarget")
	void  SetMorphTargetWeight(const FString& TargetName, float Weight);
	UFUNCTION(Callable, Category="Mesh|MorphTarget")
	void  SetMorphTargetWeightByIndex(int32 TargetIndex, float Weight);
	UFUNCTION(Callable, Category="Mesh|MorphTarget")
	void  SetMorphTargetWeights(const TArray<float>& Weights);
	UFUNCTION(Callable, Category="Mesh|MorphTarget")
	void  ClearMorphTargetWeights();
	UFUNCTION(Pure, Category="Mesh|MorphTarget")
	float GetMorphTargetWeight(const FString& TargetName) const;
	UFUNCTION(Pure, Category="Mesh|MorphTarget")
	float GetMorphTargetWeightByIndex(int32 TargetIndex) const;
	UFUNCTION(Pure, Category="Mesh|MorphTarget")
	bool  HasActiveMorphTargets() const;

	const TArray<float>& GetMorphTargetWeights() const
	{
		return MorphTargetWeights;
	}

	void GetCurrentBoneGlobalTransforms(TArray<FTransform>& OutGlobals) const;
	void GetCurrentBoneGlobalMatrices(TArray<FMatrix>& OutGlobals) const;
	void BuildSkinMatrices(TArray<FMatrix>& OutSkinMatrices) const;
	virtual ESkinningMode GetEffectiveSkinningMode() const;
	const TArray<FVertexPNCTT>& GetSkinnedVertices() const { return SkinnedVertices; }
	uint64 GetSkinnedRevision() const { return SkinnedRevision; }
	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	void RefreshSocketAttachedChildren();
	static void RefreshSocketAttachedChildrenForSkeleton(const USkeleton* Skeleton);

protected:
	// Tick/skinning 섹션: animation system 없이 현재 bone edit pose를 매 frame CPU skinning 결과로 반영한다.
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	void InitSkinningCache();
	void UpdateCPUSkinning();
	TArray<FVertexPNCTT>& GetMutableSkinnedVerticesForCloth() { return SkinnedVertices; }
	void MarkSkinnedVerticesModifiedByCloth();
	void RefreshSkinningAfterPoseChanged();
	void RefreshSkinningAfterMorphChanged();
	void MarkSocketAttachedChildrenDirty();
	void InitMorphTargetWeights();
	void ApplyMorphTargetWeightsNoRefresh(const TArray<float>& Weights);
	void BuildMorphedVertexData(
		const FSkeletalMesh& Asset,
		TArray<FVector>&     OutPositions,
		TArray<FVector>&     OutNormals
		) const;
	void EnsureBoneEditBasePose();
	void BuildBoneEditGlobalTransforms(TArray<FTransform>& OutGlobals) const;
	void BuildBoneEditGlobalMatrices(TArray<FMatrix>& OutGlobals) const;

protected:
	// Mesh/material state는 SetSkeletalMesh와 PostEditProperty가 같은 경로를 쓰도록 여기서 소유한다.
	TObjectPtr<USkeletalMesh> SkeletalMesh;
	UPROPERTY(Edit, Save, Category="Mesh", DisplayName="Skeletal Mesh", AssetType="SkeletalMesh")
	FSoftObjectPtr SkeletalMeshPath;
	TArray<UMaterial*> OverrideMaterials;
	UPROPERTY(Edit, Save, EditFixedSize, Category="Materials", DisplayName="Materials", AssetType="Material")
	TArray<FSoftObjectPtr> MaterialSlots;

	// BoneEditLocalMatrices is the current evaluated pose. BoneEditBaseLocalMatrices is the
	// edited animation base pose that survives animation evaluation and is not used as skin bind.
	TArray<FMatrix> BoneEditLocalMatrices;
	bool bUseBoneEditPose = false;
	TArray<FMatrix> BoneEditBaseLocalMatrices;
	bool bUseBoneEditBasePose = false;

	// Component-local morph runtime state.
	// 메인 Actor/Component Details에는 노출하지 않는다.
	// Mesh Editor preview, Animation curve, Lua override가 public API를 통해서만 제어한다.
	TArray<float> MorphTargetWeights;

	// SceneProxy는 이 결과와 revision만 보고 dynamic vertex buffer를 갱신한다.
	TArray<FVertexPNCTT> SkinnedVertices;
	uint64 SkinnedRevision = 0;
};
