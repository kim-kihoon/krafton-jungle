#pragma once

#include "Gizmo/GizmoTransformTarget.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"

class UPhysicsAsset;
class USkeletalMeshComponent;

// Constraint gizmo editing chooses one of the two authored local frames.
enum class EPhysicsAssetConstraintFrameTarget : uint8
{
    Parent,
    Child
};

class FPhysicsAssetBodyGizmoTarget : public IGizmoTransformTarget
{
public:
    void SetTarget(USkeletalMeshComponent* InPreviewComponent, UPhysicsAsset* InPhysicsAsset, int32 InBodyIndex);
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
    bool GetWorldTransform(FTransform& OutWorldTransform) const;
    bool ApplyWorldTransform(const FTransform& NewWorldTransform);

private:
    TWeakObjectPtr<USkeletalMeshComponent> PreviewComponent;
    TWeakObjectPtr<UPhysicsAsset> PhysicsAsset;
    int32 BodyIndex = -1;
    bool bModified = false;
};

class FPhysicsAssetShapeGizmoTarget : public IGizmoTransformTarget
{
public:
    void SetTarget(USkeletalMeshComponent* InPreviewComponent, UPhysicsAsset* InPhysicsAsset, int32 InBodyIndex, int32 InShapeIndex);
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
    bool GetWorldTransform(FTransform& OutWorldTransform) const;
    bool ApplyWorldTransform(const FTransform& NewWorldTransform);

private:
    TWeakObjectPtr<USkeletalMeshComponent> PreviewComponent;
    TWeakObjectPtr<UPhysicsAsset> PhysicsAsset;
    int32 BodyIndex = -1;
    int32 ShapeIndex = -1;
    bool bModified = false;
};

class FPhysicsAssetConstraintFrameGizmoTarget : public IGizmoTransformTarget
{
public:
    void SetTarget(
        USkeletalMeshComponent* InPreviewComponent,
        UPhysicsAsset* InPhysicsAsset,
        int32 InConstraintIndex,
        EPhysicsAssetConstraintFrameTarget InFrameTarget);
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
    bool GetWorldTransform(FTransform& OutWorldTransform) const;
    bool ApplyWorldTransform(const FTransform& NewWorldTransform);

private:
    TWeakObjectPtr<USkeletalMeshComponent> PreviewComponent;
    TWeakObjectPtr<UPhysicsAsset> PhysicsAsset;
    int32 ConstraintIndex = -1;
    EPhysicsAssetConstraintFrameTarget FrameTarget = EPhysicsAssetConstraintFrameTarget::Child;
    bool bModified = false;
};
