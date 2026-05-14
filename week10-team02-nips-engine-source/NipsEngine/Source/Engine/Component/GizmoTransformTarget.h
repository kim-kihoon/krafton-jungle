#pragma once

#include "Engine/Core/CoreMinimal.h" // 프로젝트의 기본 Core 헤더 (FTransform, FVector 등)
#include <memory>

class AActor;
class USceneComponent;
class USkeletalMeshComponent;

// IGizmoTransformTarget (Interface)
class IGizmoTransformTarget
{
public:
    virtual ~IGizmoTransformTarget() = default;

    virtual bool IsValid() const = 0;
    virtual FTransform GetWorldTransform() const = 0;
    virtual void SetWorldTransform(const FTransform& NewWorldTransform) = 0;

    virtual bool SupportsTranslate() const { return true; }
    virtual bool SupportsRotate() const { return true; }
    virtual bool SupportsScale() const { return true; }

    virtual FVector GetPivotLocation() const
    {
        return GetWorldTransform().GetLocation();
    }
};

// FActorGizmoTarget
class FActorGizmoTarget : public IGizmoTransformTarget
{
public:
    explicit FActorGizmoTarget(AActor* InActor);

    bool IsValid() const override;
    FTransform GetWorldTransform() const override;
    void SetWorldTransform(const FTransform& NewWorldTransform) override;

private:
    AActor* TargetActor = nullptr;
};

// FSceneComponentGizmoTarget
class FSceneComponentGizmoTarget : public IGizmoTransformTarget
{
public:
    explicit FSceneComponentGizmoTarget(USceneComponent* InComponent);

    bool IsValid() const override;
    FTransform GetWorldTransform() const override;
    void SetWorldTransform(const FTransform& NewWorldTransform) override;

private:
    USceneComponent* TargetComponent = nullptr;
};

// FBoneGizmoTarget
class FBoneGizmoTarget : public IGizmoTransformTarget
{
public:
    FBoneGizmoTarget(USkeletalMeshComponent* InMeshComp, int32 InBoneIndex);

    bool IsValid() const override;
    FTransform GetWorldTransform() const override;
    void SetWorldTransform(const FTransform& NewWorldTransform) override;

    bool SupportsScale() const override { return true; } // Bone 스케일 조작 방지

private:
    USkeletalMeshComponent* MeshComp = nullptr;
    int32 BoneIndex = -1;
};