#pragma once

#include "GizmoTransformTarget.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Object/FName.h"
#include "Math/Transform.h"
#include <functional>

class USkeletalMeshComponent;
struct FSkeletonSocket;

class FSocketTransformGizmoTarget : public IGizmoTransformTarget
{
public:
    FSocketTransformGizmoTarget();
    FSocketTransformGizmoTarget(USkeletalMeshComponent* InMeshComp, const FName& InSocketName);
    ~FSocketTransformGizmoTarget() override = default;

public:
    void SetSocket(USkeletalMeshComponent* InMeshComp, const FName& InSocketName);
    void SetOnSocketChanged(std::function<void(const FName&)> InCallback);

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
    FSkeletonSocket* GetMutableSocket() const;
    int32 ResolveSocketBoneIndex(const FSkeletonSocket& Socket) const;
    FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;
    FTransform GetWorldTransform() const;
    void ApplyWorldTransform(const FTransform& DesiredWorldTransform);
    void NotifySocketChanged() const;

private:
    TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
    FName SocketName = FName::None;
    std::function<void(const FName&)> OnSocketChanged;
};
