#pragma once

#include "MeshComponent.h"
#include "Render/Resource/VertexTypes.h"
#include "Render/Common/RenderTypes.h"
#include "Render/Resource/Buffer.h"

class USkeletalMesh;

/* ―――― USkinnedMeshComponent ―――― */
class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	USkinnedMeshComponent() = default;
	~USkinnedMeshComponent() override = default;

	// Bone hierarchy의 local transform을 부모에서 자식 방향으로 누적해
	// 각 bone의 component-space transform을 만든다.
	// 이 결과가 skinning matrix 계산의 "현재 pose" 입력이 된다.
	virtual void RefreshBoneTransforms();

	// 각 bone에 대해 "bind pose에서 현재 pose로 이동시키는 행렬"을 계산한다.
	void ComputeSkinningMatrices();

	// 원본 skeletal vertex에 bone influence를 적용해 CPU 쪽 skinned vertex를 만든다.
	// 현재 렌더링 경로는 이 결과를 dynamic vertex buffer에 업로드한다.
	void ComputeSkinnedVertices();

	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() { return SkeletalMesh; }
	const USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	const TArray<FNormalVertex>& GetSkinnedVertices() const;
	const TArray<FMatrix>& GetSkinningMatrices() const;
	const TArray<FTransform>& GetComponentSpaceBoneTransforms() const;
	void UpdateRenderBuffer();
	FMeshBuffer* GetRenderMeshBuffer();
	const FMeshBuffer* GetRenderMeshBuffer() const;

	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }
	const FAABB& GetWorldAABB() const override;
	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	bool HasValidMesh() const;

	void InitializePoseFromReference();
	void ResetPose();

	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& NewLocalTransform);

	FTransform GetBoneComponentTransform(int32 BoneIndex) const;

	FTransform GetBoneWorldTransform(int32 BoneIndex) const;
	void SetBoneWorldTransform(int32 BoneIndex, const FTransform& NewWorldTransform);

protected:
	// 상속받은 skeletal mesh componen에서도 접근
	void MarkBoneTransformsDirty();
	void MarkSkinningDirty();
	void MarkBoundsDirty();
	void MarkRenderStateDirty();
	void EnsureBoundsUpdated() const;

protected:
	FString SkeletalMeshAssetPath;
    
	// 공유 asset 데이터. 여러 component가 같은 USkeletalMesh를 참조할 수 있으므로,
	// pose처럼 instance마다 달라지는 값은 component 안의 배열에 따로 저장한다.
	USkeletalMesh* SkeletalMesh = nullptr;

	// Bone의 local transform은 parent bone 기준이다.
	// 예를 들어 손목 bone의 local transform은 팔꿈치 bone 기준 위치/회전이다.
	// 스키닝에서는 한 vertex가 여러 bone의 영향을 동시에 받기 때문에,
	// 모든 bone transform을 같은 기준 공간으로 맞춰야 한다.
	// ComponentSpaceBoneTransforms는 root부터 parent transform을 누적한 결과이며,
	// component local space에서 각 bone이 현재 어디에 있는지를 나타낸다.
	TArray<FTransform> ComponentSpaceBoneTransforms;

	// 현재 pose의 bone local transform이다.
	// 초기값은 reference pose이고, 에디터 gizmo나 추후 animation system이 이 값을 바꾼다.
	TArray<FTransform> LocalBoneTransforms;

	// Bone별 최종 skinning matrix.
	// 공식은 대략 VertexInComponent = VertexBindPose * InverseBindPose * CurrentBonePose 이다.
	// 이 코드의 행렬 곱 순서에 맞춰 InverseBindPose * CurrentBoneMatrix 형태로 저장한다.
	TArray<FMatrix> SkinningMatrices;

	// CPU skinning이 끝난 vertex 배열.
	// FSkeletalMeshVertex에는 bone index/weight가 있지만, FNormalVertex는 일반 렌더 패스가
	// 바로 사용할 수 있는 위치/노멀/탄젠트/UV만 가진다.
	TArray<FNormalVertex> SkinnedVertices;
	FAABB LocalSkinnedAABB;
	FDynamicMeshBuffer MeshBuffer;

	mutable bool bBoneTransformsDirty = true;
	mutable bool bSkinningDirty = true;
	mutable bool bBoundsDirty = true;
	bool bRenderStateDirty = true;
};
