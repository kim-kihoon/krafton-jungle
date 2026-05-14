#include "GizmoTransformTarget.h"
#include "GameFramework/AActor.h"
#include "Component/SceneComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Asset/SkeletalMesh.h"

// FActorGizmoTarget
FActorGizmoTarget::FActorGizmoTarget(AActor* InActor)
    : TargetActor(InActor)
{
}

bool FActorGizmoTarget::IsValid() const
{
    return TargetActor != nullptr;
}

FTransform FActorGizmoTarget::GetWorldTransform() const
{
    if (TargetActor && TargetActor->GetRootComponent())
    {
        return TargetActor->GetRootComponent()->GetWorldTransform();
    }
    return FTransform::Identity;
}

void FActorGizmoTarget::SetWorldTransform(const FTransform& NewWorldTransform)
{
    if (TargetActor && TargetActor->GetRootComponent())
    {
        TargetActor->GetRootComponent()->SetWorldTransform(NewWorldTransform);
    }
}

// FSceneComponentGizmoTarget
FSceneComponentGizmoTarget::FSceneComponentGizmoTarget(USceneComponent* InComponent)
    : TargetComponent(InComponent)
{
}

bool FSceneComponentGizmoTarget::IsValid() const
{
    return TargetComponent != nullptr;
}

FTransform FSceneComponentGizmoTarget::GetWorldTransform() const
{
    if (TargetComponent)
    {
        return TargetComponent->GetWorldTransform();
    }
    return FTransform::Identity;
}

void FSceneComponentGizmoTarget::SetWorldTransform(const FTransform& NewWorldTransform)
{
    if (TargetComponent)
    {
        TargetComponent->SetWorldTransform(NewWorldTransform);
    }
}

// FBoneGizmoTarget
FBoneGizmoTarget::FBoneGizmoTarget(USkeletalMeshComponent* InMeshComp, int32 InBoneIndex)
    : MeshComp(InMeshComp), BoneIndex(InBoneIndex)
{
}

bool FBoneGizmoTarget::IsValid() const
{
    if (!MeshComp || !MeshComp->HasValidMesh())
    {
        return false;
    }

    if (const USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh())
    {
        return BoneIndex >= 0 && BoneIndex < static_cast<int32>(Mesh->GetBones().size());
    }

    return false;
}

FTransform FBoneGizmoTarget::GetWorldTransform() const
{
    if (MeshComp)
    {
        return MeshComp->GetBoneWorldTransform(BoneIndex);
    }
    return FTransform::Identity;
}

void FBoneGizmoTarget::SetWorldTransform(const FTransform& NewWorldTransform)
{
    if (MeshComp)
    {
        MeshComp->SetBoneWorldTransform(BoneIndex, NewWorldTransform);
    }
}