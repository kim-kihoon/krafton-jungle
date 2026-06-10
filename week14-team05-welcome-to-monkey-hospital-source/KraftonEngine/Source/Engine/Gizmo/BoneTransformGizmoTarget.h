#pragma once

#include "GizmoTransformTarget.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Math/Transform.h"

class USkeletalMeshComponent;
class USkeleton;

class FBoneTransformGizmoTarget : public IGizmoTransformTarget
{
public:
	FBoneTransformGizmoTarget();
	FBoneTransformGizmoTarget(USkeletalMeshComponent* InMeshComp, int32 InBoneIndex);
	~FBoneTransformGizmoTarget() override = default;

public:
	void SetBone(USkeletalMeshComponent* MeshComp, int32 BoneIndex) { MeshComponent = MeshComp; this->BoneIndex = BoneIndex; }

	bool IsValid() const override;
	UWorld* GetWorld() const override;

	FVector GetWorldLocation() const override;
	FRotator GetWorldRotation() const override;
	FQuat GetWorldQuat() const override;
	FVector GetWorldScale() const override;

	void SetWorldLocation(const FVector& NewLocation) override;
	void SetWorldRotation(const FRotator& NewRotation) override;
	void SetWorldRotation(const FQuat& NewQuat) override;
	void SetWorldScale(const FVector& NewScale) override;

	void AddWorldOffset(const FVector& Delta) override;
	void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override;
	void AddScaleDelta(const FVector& Delta) override;

private:
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
	int32 BoneIndex = -1;
};

class FSocketTransformGizmoTarget : public IGizmoTransformTarget
{
public:
	FSocketTransformGizmoTarget();
	FSocketTransformGizmoTarget(USkeletalMeshComponent* InMeshComp, USkeleton* InSkeleton, int32 InSocketIndex);
	~FSocketTransformGizmoTarget() override = default;

	void SetSocket(USkeletalMeshComponent* MeshComp, USkeleton* InSkeleton, int32 InSocketIndex);
	bool ConsumeModified();

	bool IsValid() const override;
	UWorld* GetWorld() const override;

	FVector GetWorldLocation() const override;
	FRotator GetWorldRotation() const override;
	FQuat GetWorldQuat() const override;
	FVector GetWorldScale() const override;

	void SetWorldLocation(const FVector& NewLocation) override;
	void SetWorldRotation(const FRotator& NewRotation) override;
	void SetWorldRotation(const FQuat& NewQuat) override;
	void SetWorldScale(const FVector& NewScale) override;

	void AddWorldOffset(const FVector& Delta) override;
	void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override;
	void AddScaleDelta(const FVector& Delta) override;

private:
	bool GetSocketWorldTransform(FTransform& OutTransform) const;
	bool ApplySocketWorldTransform(const FTransform& NewWorldTransform);

private:
	// 비소유 에디터 캐시 — 메시/스켈레톤이 파괴돼도 dangling 되지 않도록 weak
	// (FBoneTransformGizmoTarget::MeshComponent 와 동일한 GC 정책).
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
	TWeakObjectPtr<USkeleton> Skeleton;
	int32 SocketIndex = -1;
	bool bModified = false;
};
