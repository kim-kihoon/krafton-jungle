#include "PhysicsAssetGizmoTarget.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/World.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetPreviewUtils.h"
#include "Physics/PhysicsAssetTypes.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float MinPhysicsGizmoExtent = 0.001f;

    bool HasMeaningfulBoneName(const FName& BoneName)
    {
        return BoneName.IsValid() && BoneName != FName::None;
    }

    FTransform ComposePreviewTransforms(const FTransform& ParentWorld, const FTransform& Local)
    {
        FTransform Result = Local;
        Result.Location = ParentWorld.Location + ParentWorld.Rotation.RotateVector(Local.Location);
        Result.Rotation = (ParentWorld.Rotation * Local.Rotation).GetNormalized();
        Result.Scale = FVector::OneVector;
        return Result;
    }

    FTransform MakeLocalTransformFromWorld(const FTransform& ParentWorld, const FTransform& World)
    {
        const FQuat ParentInv = ParentWorld.Rotation.GetNormalized().Inverse();
        FTransform Local;
        Local.Location = ParentInv.RotateVector(World.Location - ParentWorld.Location);
        Local.Rotation = (ParentInv * World.Rotation.GetNormalized()).GetNormalized();
        Local.Scale = World.Scale;
        return Local;
    }

    FTransform GetComponentWorldTransform(const USkeletalMeshComponent* Component)
    {
        FTransform Result;
        if (!Component)
        {
            return Result;
        }

        Result.Location = Component->GetWorldLocation();
        Result.Rotation = Component->GetWorldMatrix().ToQuat().GetNormalized();
        Result.Scale = FVector::OneVector;
        return Result;
    }

    int32 FindBoneIndexInMesh(const USkeletalMesh* SkeletalMesh, const FName& BoneName)
    {
        if (!SkeletalMesh || !HasMeaningfulBoneName(BoneName))
        {
            return -1;
        }

        const FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
        if (!MeshAsset)
        {
            return -1;
        }

        const FString BoneNameString = BoneName.ToString();
        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
        {
            if (MeshAsset->Bones[BoneIndex].Name == BoneNameString)
            {
                return BoneIndex;
            }
        }
        return -1;
    }

    bool ComputeBoneWorldTransform(
        const USkeletalMeshComponent* PreviewComponent,
        const FName& BoneName,
        FTransform& OutBoneWorldTransform)
    {
        if (!PreviewComponent || !PreviewComponent->GetSkeletalMesh())
        {
            return false;
        }

        const int32 BoneIndex = FindBoneIndexInMesh(PreviewComponent->GetSkeletalMesh(), BoneName);
        if (BoneIndex < 0)
        {
            return false;
        }

        TArray<FTransform> BoneComponentSpaceTransforms;
        PreviewComponent->GetCurrentBoneGlobalTransforms(BoneComponentSpaceTransforms);
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(BoneComponentSpaceTransforms.size()))
        {
            return false;
        }

        OutBoneWorldTransform = ComposePreviewTransforms(
            GetComponentWorldTransform(PreviewComponent),
            BoneComponentSpaceTransforms[BoneIndex]);
        return true;
    }

    FPhysicsAssetShapeSetup* GetMutableShape(UPhysicsAsset* PhysicsAsset, int32 BodyIndex, int32 ShapeIndex)
    {
        if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= static_cast<int32>(PhysicsAsset->GetMutableBodySetups().size()))
        {
            return nullptr;
        }

        FPhysicsAssetBodySetup& Body = PhysicsAsset->GetMutableBodySetups()[BodyIndex];
        if (ShapeIndex < 0 || ShapeIndex >= static_cast<int32>(Body.Shapes.size()))
        {
            return nullptr;
        }
        return &Body.Shapes[ShapeIndex];
    }

    void ClampShapeDimensions(FPhysicsAssetShapeSetup& Shape)
    {
        Shape.BoxHalfExtent.X = (std::max)(Shape.BoxHalfExtent.X, MinPhysicsGizmoExtent);
        Shape.BoxHalfExtent.Y = (std::max)(Shape.BoxHalfExtent.Y, MinPhysicsGizmoExtent);
        Shape.BoxHalfExtent.Z = (std::max)(Shape.BoxHalfExtent.Z, MinPhysicsGizmoExtent);
        Shape.SphereRadius = (std::max)(Shape.SphereRadius, MinPhysicsGizmoExtent);
        Shape.CapsuleRadius = (std::max)(Shape.CapsuleRadius, MinPhysicsGizmoExtent);
        Shape.CapsuleHalfHeight = (std::max)(Shape.CapsuleHalfHeight, Shape.CapsuleRadius);
    }

    FVector GetShapeScaleProxy(const FPhysicsAssetShapeSetup& Shape)
    {
        switch (Shape.Type)
        {
        case EPhysicsAssetShapeType::Box:
            return Shape.BoxHalfExtent;
        case EPhysicsAssetShapeType::Sphere:
            return FVector(Shape.SphereRadius, Shape.SphereRadius, Shape.SphereRadius);
        case EPhysicsAssetShapeType::Capsule:
            return FVector(Shape.CapsuleRadius, Shape.CapsuleRadius, Shape.CapsuleHalfHeight);
        default:
            return FVector::OneVector;
        }
    }

    void AddShapeScaleDelta(FPhysicsAssetShapeSetup& Shape, const FVector& Delta)
    {
        switch (Shape.Type)
        {
        case EPhysicsAssetShapeType::Box:
            Shape.BoxHalfExtent += Delta;
            break;
        case EPhysicsAssetShapeType::Sphere:
        {
            const float UniformDelta = (std::fabs(Delta.X) >= std::fabs(Delta.Y) && std::fabs(Delta.X) >= std::fabs(Delta.Z))
                ? Delta.X
                : (std::fabs(Delta.Y) >= std::fabs(Delta.Z) ? Delta.Y : Delta.Z);
            Shape.SphereRadius += UniformDelta;
            break;
        }
        case EPhysicsAssetShapeType::Capsule:
            Shape.CapsuleRadius += (Delta.X != 0.0f) ? Delta.X : Delta.Y;
            Shape.CapsuleHalfHeight += Delta.Z;
            break;
        default:
            break;
        }

        ClampShapeDimensions(Shape);
    }
}

void FPhysicsAssetBodyGizmoTarget::SetTarget(USkeletalMeshComponent* InPreviewComponent, UPhysicsAsset* InPhysicsAsset, int32 InBodyIndex)
{
    PreviewComponent = InPreviewComponent;
    PhysicsAsset = InPhysicsAsset;
    BodyIndex = InBodyIndex;
}

bool FPhysicsAssetBodyGizmoTarget::ConsumeModified()
{
    const bool bWasModified = bModified;
    bModified = false;
    return bWasModified;
}

bool FPhysicsAssetBodyGizmoTarget::IsValid() const
{
    return PreviewComponent &&
        PhysicsAsset &&
        BodyIndex >= 0 &&
        BodyIndex < static_cast<int32>(PhysicsAsset->GetBodySetups().size()) &&
        HasMeaningfulBoneName(PhysicsAsset->GetBodySetups()[BodyIndex].BoneName);
}

UWorld* FPhysicsAssetBodyGizmoTarget::GetWorld() const
{
    return PreviewComponent ? PreviewComponent->GetWorld() : nullptr;
}

FVector FPhysicsAssetBodyGizmoTarget::GetWorldLocation() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.Location : FVector::ZeroVector;
}

FRotator FPhysicsAssetBodyGizmoTarget::GetWorldRotation() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.GetRotator() : FRotator::ZeroRotator;
}

FQuat FPhysicsAssetBodyGizmoTarget::GetWorldQuat() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.Rotation : FQuat::Identity;
}

FVector FPhysicsAssetBodyGizmoTarget::GetWorldScale() const
{
    if (!IsValid())
    {
        return FVector::OneVector;
    }
    return PhysicsAsset->GetBodySetups()[BodyIndex].BodyLocalFrame.Scale;
}

void FPhysicsAssetBodyGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Location = NewLocation;
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetBodyGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
    SetWorldRotation(NewRotation.ToQuaternion());
}

void FPhysicsAssetBodyGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Rotation = NewQuat.GetNormalized();
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetBodyGizmoTarget::SetWorldScale(const FVector& NewScale)
{
    if (!IsValid())
    {
        return;
    }

    FPhysicsAssetBodySetup& Body = PhysicsAsset->GetMutableBodySetups()[BodyIndex];
    Body.BodyLocalFrame.Scale = FVector(
        (std::max)(NewScale.X, MinPhysicsGizmoExtent),
        (std::max)(NewScale.Y, MinPhysicsGizmoExtent),
        (std::max)(NewScale.Z, MinPhysicsGizmoExtent));
    bModified = true;
}

void FPhysicsAssetBodyGizmoTarget::AddWorldOffset(const FVector& Delta)
{
    SetWorldLocation(GetWorldLocation() + Delta);
}

void FPhysicsAssetBodyGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Rotation = bWorldSpace
            ? (Delta * WorldTransform.Rotation).GetNormalized()
            : (WorldTransform.Rotation * Delta).GetNormalized();
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetBodyGizmoTarget::AddScaleDelta(const FVector& Delta)
{
    if (!IsValid())
    {
        return;
    }

    FPhysicsAssetBodySetup& Body = PhysicsAsset->GetMutableBodySetups()[BodyIndex];
    Body.BodyLocalFrame.Scale += Delta;
    Body.BodyLocalFrame.Scale.X = (std::max)(Body.BodyLocalFrame.Scale.X, MinPhysicsGizmoExtent);
    Body.BodyLocalFrame.Scale.Y = (std::max)(Body.BodyLocalFrame.Scale.Y, MinPhysicsGizmoExtent);
    Body.BodyLocalFrame.Scale.Z = (std::max)(Body.BodyLocalFrame.Scale.Z, MinPhysicsGizmoExtent);
    bModified = true;
}

bool FPhysicsAssetBodyGizmoTarget::GetWorldTransform(FTransform& OutWorldTransform) const
{
    if (!IsValid())
    {
        return false;
    }
    return FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransform(PreviewComponent, PhysicsAsset, BodyIndex, OutWorldTransform);
}

bool FPhysicsAssetBodyGizmoTarget::ApplyWorldTransform(const FTransform& NewWorldTransform)
{
    if (!IsValid())
    {
        return false;
    }

    FPhysicsAssetBodySetup& Body = PhysicsAsset->GetMutableBodySetups()[BodyIndex];
    FTransform BoneWorld;
    if (!ComputeBoneWorldTransform(PreviewComponent, Body.BoneName, BoneWorld))
    {
        return false;
    }

    Body.BodyLocalFrame = MakeLocalTransformFromWorld(BoneWorld, NewWorldTransform);
    Body.BodyLocalFrame.Scale.X = (std::max)(Body.BodyLocalFrame.Scale.X, MinPhysicsGizmoExtent);
    Body.BodyLocalFrame.Scale.Y = (std::max)(Body.BodyLocalFrame.Scale.Y, MinPhysicsGizmoExtent);
    Body.BodyLocalFrame.Scale.Z = (std::max)(Body.BodyLocalFrame.Scale.Z, MinPhysicsGizmoExtent);
    bModified = true;
    return true;
}

void FPhysicsAssetShapeGizmoTarget::SetTarget(USkeletalMeshComponent* InPreviewComponent, UPhysicsAsset* InPhysicsAsset, int32 InBodyIndex, int32 InShapeIndex)
{
    PreviewComponent = InPreviewComponent;
    PhysicsAsset = InPhysicsAsset;
    BodyIndex = InBodyIndex;
    ShapeIndex = InShapeIndex;
}

bool FPhysicsAssetShapeGizmoTarget::ConsumeModified()
{
    const bool bWasModified = bModified;
    bModified = false;
    return bWasModified;
}

bool FPhysicsAssetShapeGizmoTarget::IsValid() const
{
    return PreviewComponent &&
        PhysicsAsset &&
        BodyIndex >= 0 &&
        BodyIndex < static_cast<int32>(PhysicsAsset->GetBodySetups().size()) &&
        HasMeaningfulBoneName(PhysicsAsset->GetBodySetups()[BodyIndex].BoneName) &&
        ShapeIndex >= 0 &&
        ShapeIndex < static_cast<int32>(PhysicsAsset->GetBodySetups()[BodyIndex].Shapes.size());
}

UWorld* FPhysicsAssetShapeGizmoTarget::GetWorld() const
{
    return PreviewComponent ? PreviewComponent->GetWorld() : nullptr;
}

FVector FPhysicsAssetShapeGizmoTarget::GetWorldLocation() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.Location : FVector::ZeroVector;
}

FRotator FPhysicsAssetShapeGizmoTarget::GetWorldRotation() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.GetRotator() : FRotator::ZeroRotator;
}

FQuat FPhysicsAssetShapeGizmoTarget::GetWorldQuat() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.Rotation : FQuat::Identity;
}

FVector FPhysicsAssetShapeGizmoTarget::GetWorldScale() const
{
    if (!IsValid())
    {
        return FVector::OneVector;
    }
    return GetShapeScaleProxy(PhysicsAsset->GetBodySetups()[BodyIndex].Shapes[ShapeIndex]);
}

void FPhysicsAssetShapeGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Location = NewLocation;
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetShapeGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
    SetWorldRotation(NewRotation.ToQuaternion());
}

void FPhysicsAssetShapeGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Rotation = NewQuat.GetNormalized();
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetShapeGizmoTarget::SetWorldScale(const FVector& NewScale)
{
    if (!IsValid())
    {
        return;
    }

    FPhysicsAssetShapeSetup* Shape = GetMutableShape(PhysicsAsset, BodyIndex, ShapeIndex);
    if (!Shape)
    {
        return;
    }

    switch (Shape->Type)
    {
    case EPhysicsAssetShapeType::Box:
        Shape->BoxHalfExtent = NewScale;
        break;
    case EPhysicsAssetShapeType::Sphere:
        Shape->SphereRadius = (NewScale.X + NewScale.Y + NewScale.Z) / 3.0f;
        break;
    case EPhysicsAssetShapeType::Capsule:
        Shape->CapsuleRadius = (NewScale.X + NewScale.Y) * 0.5f;
        Shape->CapsuleHalfHeight = NewScale.Z;
        break;
    default:
        break;
    }
    ClampShapeDimensions(*Shape);
    bModified = true;
}

void FPhysicsAssetShapeGizmoTarget::AddWorldOffset(const FVector& Delta)
{
    SetWorldLocation(GetWorldLocation() + Delta);
}

void FPhysicsAssetShapeGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Rotation = bWorldSpace
            ? (Delta * WorldTransform.Rotation).GetNormalized()
            : (WorldTransform.Rotation * Delta).GetNormalized();
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetShapeGizmoTarget::AddScaleDelta(const FVector& Delta)
{
    FPhysicsAssetShapeSetup* Shape = GetMutableShape(PhysicsAsset, BodyIndex, ShapeIndex);
    if (!Shape)
    {
        return;
    }

    AddShapeScaleDelta(*Shape, Delta);
    bModified = true;
}

bool FPhysicsAssetShapeGizmoTarget::GetWorldTransform(FTransform& OutWorldTransform) const
{
    if (!IsValid())
    {
        return false;
    }

    FTransform BodyWorld;
    if (!FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransform(PreviewComponent, PhysicsAsset, BodyIndex, BodyWorld))
    {
        return false;
    }

    OutWorldTransform = ComposePreviewTransforms(BodyWorld, PhysicsAsset->GetBodySetups()[BodyIndex].Shapes[ShapeIndex].LocalTransform);
    OutWorldTransform.Scale = GetShapeScaleProxy(PhysicsAsset->GetBodySetups()[BodyIndex].Shapes[ShapeIndex]);
    return true;
}

bool FPhysicsAssetShapeGizmoTarget::ApplyWorldTransform(const FTransform& NewWorldTransform)
{
    if (!IsValid())
    {
        return false;
    }

    FTransform BodyWorld;
    if (!FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransform(PreviewComponent, PhysicsAsset, BodyIndex, BodyWorld))
    {
        return false;
    }

    FPhysicsAssetShapeSetup& Shape = PhysicsAsset->GetMutableBodySetups()[BodyIndex].Shapes[ShapeIndex];
    const FVector PreviousScale = Shape.LocalTransform.Scale;
    Shape.LocalTransform = MakeLocalTransformFromWorld(BodyWorld, NewWorldTransform);
    Shape.LocalTransform.Scale = PreviousScale;
    bModified = true;
    return true;
}

void FPhysicsAssetConstraintFrameGizmoTarget::SetTarget(
    USkeletalMeshComponent* InPreviewComponent,
    UPhysicsAsset* InPhysicsAsset,
    int32 InConstraintIndex,
    EPhysicsAssetConstraintFrameTarget InFrameTarget)
{
    PreviewComponent = InPreviewComponent;
    PhysicsAsset = InPhysicsAsset;
    ConstraintIndex = InConstraintIndex;
    FrameTarget = InFrameTarget;
}

bool FPhysicsAssetConstraintFrameGizmoTarget::ConsumeModified()
{
    const bool bWasModified = bModified;
    bModified = false;
    return bWasModified;
}

bool FPhysicsAssetConstraintFrameGizmoTarget::IsValid() const
{
    if (!PreviewComponent || !PhysicsAsset ||
        ConstraintIndex < 0 ||
        ConstraintIndex >= static_cast<int32>(PhysicsAsset->GetConstraintSetups().size()))
    {
        return false;
    }

    const FPhysicsAssetConstraintSetup& Constraint = PhysicsAsset->GetConstraintSetups()[ConstraintIndex];
    return HasMeaningfulBoneName(Constraint.ParentBoneName) &&
        HasMeaningfulBoneName(Constraint.ChildBoneName) &&
        PhysicsAsset->HasBodySetupForBone(Constraint.ParentBoneName) &&
        PhysicsAsset->HasBodySetupForBone(Constraint.ChildBoneName);
}

UWorld* FPhysicsAssetConstraintFrameGizmoTarget::GetWorld() const
{
    return PreviewComponent ? PreviewComponent->GetWorld() : nullptr;
}

FVector FPhysicsAssetConstraintFrameGizmoTarget::GetWorldLocation() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.Location : FVector::ZeroVector;
}

FRotator FPhysicsAssetConstraintFrameGizmoTarget::GetWorldRotation() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.GetRotator() : FRotator::ZeroRotator;
}

FQuat FPhysicsAssetConstraintFrameGizmoTarget::GetWorldQuat() const
{
    FTransform WorldTransform;
    return GetWorldTransform(WorldTransform) ? WorldTransform.Rotation : FQuat::Identity;
}

FVector FPhysicsAssetConstraintFrameGizmoTarget::GetWorldScale() const
{
    return FVector::OneVector;
}

void FPhysicsAssetConstraintFrameGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Location = NewLocation;
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetConstraintFrameGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
    SetWorldRotation(NewRotation.ToQuaternion());
}

void FPhysicsAssetConstraintFrameGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Rotation = NewQuat.GetNormalized();
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetConstraintFrameGizmoTarget::SetWorldScale(const FVector& NewScale)
{
    (void)NewScale;
}

void FPhysicsAssetConstraintFrameGizmoTarget::AddWorldOffset(const FVector& Delta)
{
    SetWorldLocation(GetWorldLocation() + Delta);
}

void FPhysicsAssetConstraintFrameGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
    FTransform WorldTransform;
    if (GetWorldTransform(WorldTransform))
    {
        WorldTransform.Rotation = bWorldSpace
            ? (Delta * WorldTransform.Rotation).GetNormalized()
            : (WorldTransform.Rotation * Delta).GetNormalized();
        ApplyWorldTransform(WorldTransform);
    }
}

void FPhysicsAssetConstraintFrameGizmoTarget::AddScaleDelta(const FVector& Delta)
{
    (void)Delta;
}

bool FPhysicsAssetConstraintFrameGizmoTarget::GetWorldTransform(FTransform& OutWorldTransform) const
{
    if (!IsValid())
    {
        return false;
    }

    FTransform ParentFrameWorld;
    FTransform ChildFrameWorld;
    if (!FPhysicsAssetPreviewUtils::ComputePreviewConstraintWorldFrames(
            PreviewComponent,
            PhysicsAsset,
            ConstraintIndex,
            ParentFrameWorld,
            ChildFrameWorld))
    {
        return false;
    }

    OutWorldTransform = (FrameTarget == EPhysicsAssetConstraintFrameTarget::Parent)
        ? ParentFrameWorld
        : ChildFrameWorld;
    OutWorldTransform.Scale = FVector::OneVector;
    return true;
}

bool FPhysicsAssetConstraintFrameGizmoTarget::ApplyWorldTransform(const FTransform& NewWorldTransform)
{
    if (!IsValid())
    {
        return false;
    }

    FPhysicsAssetConstraintSetup& Constraint = PhysicsAsset->GetMutableConstraintSetups()[ConstraintIndex];
    const FName BaseBoneName = (FrameTarget == EPhysicsAssetConstraintFrameTarget::Parent)
        ? Constraint.ParentBoneName
        : Constraint.ChildBoneName;

    FTransform BaseBodyWorld;
    if (!FPhysicsAssetPreviewUtils::ComputePreviewBodyWorldTransformByBoneName(
            PreviewComponent,
            PhysicsAsset,
            BaseBoneName,
            BaseBodyWorld))
    {
        return false;
    }

    FTransform NewLocalFrame = MakeLocalTransformFromWorld(BaseBodyWorld, NewWorldTransform);
    NewLocalFrame.Scale = FVector::OneVector;
    if (FrameTarget == EPhysicsAssetConstraintFrameTarget::Parent)
    {
        Constraint.ParentLocalFrame = NewLocalFrame;
    }
    else
    {
        Constraint.ChildLocalFrame = NewLocalFrame;
    }

    bModified = true;
    return true;
}
