#include "Cloth/ClothCollisionGatherer.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"
#include "Math/Quat.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsRuntime.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float ClothCollisionScaleTolerance = 1.0e-4f;
    constexpr float ClothWorldCollisionSectionExpansion = 50.0f;

    FTransform ComposeClothCollisionTransforms(const FTransform& Parent, const FTransform& Local)
    {
        FTransform Result = Local;
        Result.Location = Parent.Location + Parent.Rotation.RotateVector(Local.Location);
        Result.Rotation = (Parent.Rotation * Local.Rotation).GetNormalized();
        Result.Scale = FVector::OneVector;
        return Result;
    }

    bool IsFiniteFloat(float Value)
    {
        return std::isfinite(Value);
    }

    bool IsFiniteVector(const FVector& Value)
    {
        return IsFiniteFloat(Value.X) && IsFiniteFloat(Value.Y) && IsFiniteFloat(Value.Z);
    }

    bool IsUniformPositiveScale(const FVector& Scale)
    {
        return IsFiniteVector(Scale) &&
            Scale.X > ClothCollisionScaleTolerance &&
            Scale.Y > ClothCollisionScaleTolerance &&
            Scale.Z > ClothCollisionScaleTolerance &&
            std::fabs(Scale.X - Scale.Y) <= ClothCollisionScaleTolerance &&
            std::fabs(Scale.Y - Scale.Z) <= ClothCollisionScaleTolerance;
    }

    bool IsValidTransformForCollision(const FTransform& Transform)
    {
        return IsFiniteVector(Transform.Location) &&
            IsFiniteVector(Transform.Scale) &&
            IsFiniteFloat(Transform.Rotation.X) &&
            IsFiniteFloat(Transform.Rotation.Y) &&
            IsFiniteFloat(Transform.Rotation.Z) &&
            IsFiniteFloat(Transform.Rotation.W);
    }

    FBoundingBox ExpandBounds(const FBoundingBox& Bounds, float Margin)
    {
        if (!Bounds.IsValid())
        {
            return Bounds;
        }
        const FVector Expansion(Margin, Margin, Margin);
        return FBoundingBox(Bounds.Min - Expansion, Bounds.Max + Expansion);
    }

    float ComputeBoundsDistanceSquared(const FBoundingBox& A, const FBoundingBox& B)
    {
        const float DX = (A.Max.X < B.Min.X) ? (B.Min.X - A.Max.X) : ((B.Max.X < A.Min.X) ? (A.Min.X - B.Max.X) : 0.0f);
        const float DY = (A.Max.Y < B.Min.Y) ? (B.Min.Y - A.Max.Y) : ((B.Max.Y < A.Min.Y) ? (A.Min.Y - B.Max.Y) : 0.0f);
        const float DZ = (A.Max.Z < B.Min.Z) ? (B.Min.Z - A.Max.Z) : ((B.Max.Z < A.Min.Z) ? (A.Min.Z - B.Max.Z) : 0.0f);
        return DX * DX + DY * DY + DZ * DZ;
    }

    int32 GetClothCollisionSourcePriority(EClothCollisionSource Source)
    {
        switch (Source)
        {
        case EClothCollisionSource::PhysicsAsset:
            return 0;
        case EClothCollisionSource::WorldStatic:
            return 1;
        case EClothCollisionSource::WorldDynamic:
            return 2;
        default:
            return 3;
        }
    }

    bool IsWorldShapeBlocking(const FPhysicsClothCollisionShape& Shape)
    {
        return Shape.FilterData.CollisionEnabled != ECollisionEnabled::NoCollision &&
            !Shape.FilterData.bIsTrigger &&
            Shape.FilterData.BlockMask != 0;
    }

    ECollisionChannel ToCollisionChannel(uint32 ObjectType)
    {
        const uint32 Clamped = (std::min)(ObjectType, static_cast<uint32>(ECollisionChannel::ActiveCount) - 1u);
        return static_cast<ECollisionChannel>(Clamped);
    }

    FVector GetComponentCollisionScale(const USkeletalMeshComponent& Component)
    {
        const FVector Scale = Component.GetWorldMatrix().GetScale();
        if (!IsFiniteVector(Scale) ||
            Scale.X <= ClothCollisionScaleTolerance ||
            Scale.Y <= ClothCollisionScaleTolerance ||
            Scale.Z <= ClothCollisionScaleTolerance)
        {
            return FVector::OneVector;
        }
        return Scale;
    }

    float GetUniformCollisionScale(const FVector& Scale)
    {
        return IsUniformPositiveScale(Scale) ? Scale.X : 1.0f;
    }

    FVector DivideByComponentScale(const FVector& Value, const FVector& Scale)
    {
        return FVector(
            Value.X / Scale.X,
            Value.Y / Scale.Y,
            Value.Z / Scale.Z);
    }

    FTransform MakeComponentLocalTransform(const USkeletalMeshComponent& Component, const FTransform& WorldTransform)
    {
        const FMatrix ComponentWorldMatrix = Component.GetWorldMatrix();
        const FTransform ComponentWorld(ComponentWorldMatrix);
        const FQuat InverseRotation = ComponentWorld.Rotation.Inverse();

        FTransform Local;
        Local.Location = ComponentWorldMatrix.GetInverse().TransformPositionWithW(WorldTransform.Location);
        Local.Rotation = (InverseRotation * WorldTransform.Rotation).GetNormalized();
        Local.Scale = FVector::OneVector;
        return Local;
    }

    int32 FindBoneIndexInMesh(const FSkeletalMesh& MeshAsset, const FName& BoneName)
    {
        if (!BoneName.IsValid() || BoneName == FName::None)
        {
            return -1;
        }

        const FString BoneNameString = BoneName.ToString();
        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset.Bones.size()); ++BoneIndex)
        {
            if (MeshAsset.Bones[BoneIndex].Name == BoneNameString)
            {
                return BoneIndex;
            }
        }
        return -1;
    }

    void AccumulateGatheredStat(FClothCollisionDebugStats& Stats, EClothCollisionPrimitiveType Type)
    {
        switch (Type)
        {
        case EClothCollisionPrimitiveType::Sphere:
            ++Stats.GatheredSpheres;
            break;
        case EClothCollisionPrimitiveType::Capsule:
            ++Stats.GatheredCapsules;
            break;
        case EClothCollisionPrimitiveType::Box:
            ++Stats.GatheredBoxes;
            break;
        default:
            break;
        }
    }

    void AccumulateSelectedStat(FClothCollisionDebugStats& Stats, EClothCollisionPrimitiveType Type)
    {
        switch (Type)
        {
        case EClothCollisionPrimitiveType::Sphere:
            ++Stats.SelectedSpheres;
            break;
        case EClothCollisionPrimitiveType::Capsule:
            ++Stats.SelectedCapsules;
            break;
        case EClothCollisionPrimitiveType::Box:
            ++Stats.SelectedBoxes;
            break;
        default:
            break;
        }
    }
}

FClothCollisionGatherResult FClothCollisionGatherer::GatherForSection(
    const USkeletalMeshComponent& Component,
    const USkeletalMesh& SkeletalMesh,
    const UPhysicsAsset* PhysicsAsset,
    const IPhysicsRuntime* PhysicsRuntime,
    const FSkeletalClothConfig& ClothConfig,
    const FBoundingBox& ComponentWorldBounds,
    const FBoundingBox& SectionWorldBounds,
    const FClothCollisionBudget& Budget) const
{
    FClothCollisionGatherResult Result;
    GatherPhysicsAssetCandidates(Result, Component, SkeletalMesh, PhysicsAsset, ClothConfig);
    if (PhysicsRuntime)
    {
        GatherWorldStaticCandidates(
            Result,
            *PhysicsRuntime,
            Component,
            ClothConfig,
            ComponentWorldBounds,
            SectionWorldBounds);
        GatherWorldDynamicCandidates(
            Result,
            *PhysicsRuntime,
            Component,
            ClothConfig,
            ComponentWorldBounds,
            SectionWorldBounds);
    }
    SelectWithinBudget(Result, Budget);
    return Result;
}

FClothCollisionGatherResult FClothCollisionGatherer::GatherPhysicsAsset(
    const USkeletalMeshComponent& Component,
    const USkeletalMesh& SkeletalMesh,
    const UPhysicsAsset* PhysicsAsset,
    const FSkeletalClothConfig& ClothConfig,
    const FClothCollisionBudget& Budget) const
{
    FClothCollisionGatherResult Result;
    GatherPhysicsAssetCandidates(Result, Component, SkeletalMesh, PhysicsAsset, ClothConfig);
    SelectWithinBudget(Result, Budget);
    return Result;
}

void FClothCollisionGatherer::GatherPhysicsAssetCandidates(
    FClothCollisionGatherResult& Result,
    const USkeletalMeshComponent& Component,
    const USkeletalMesh& SkeletalMesh,
    const UPhysicsAsset* PhysicsAsset,
    const FSkeletalClothConfig& ClothConfig) const
{
    if (!ClothConfig.bEnablePhysicsAssetCollision || !PhysicsAsset)
    {
        return;
    }

    FSkeletalMesh* MeshAsset = SkeletalMesh.GetSkeletalMeshAsset();
    if (!MeshAsset)
    {
        return;
    }

    TArray<FTransform> BoneComponentSpaceTransforms;
    Component.GetCurrentBoneGlobalTransforms(BoneComponentSpaceTransforms);
    if (BoneComponentSpaceTransforms.empty())
    {
        return;
    }

    const uint32 OwnerActorId = Component.GetOwner() ? Component.GetOwner()->GetUUID() : 0;
    const uint32 OwnerComponentId = Component.GetUUID();
    const TArray<FPhysicsAssetBodySetup>& BodySetups = PhysicsAsset->GetBodySetups();
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
    {
        const FPhysicsAssetBodySetup& BodySetup = BodySetups[BodyIndex];
        const int32 BoneIndex = FindBoneIndexInMesh(*MeshAsset, BodySetup.BoneName);
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(BoneComponentSpaceTransforms.size()))
        {
            continue;
        }

        const FTransform BodyComponentTransform =
            ComposeClothCollisionTransforms(BoneComponentSpaceTransforms[BoneIndex], BodySetup.BodyLocalFrame);
        const TArray<FPhysicsAssetShapeSetup>& Shapes = BodySetup.Shapes;
        for (int32 ShapeIndex = 0; ShapeIndex < static_cast<int32>(Shapes.size()); ++ShapeIndex)
        {
            const FPhysicsAssetShapeSetup& ShapeSetup = Shapes[ShapeIndex];
            const bool bIncludeShape = ClothConfig.PhysicsAssetCollisionFilter.empty() ||
                std::find_if(
                    ClothConfig.PhysicsAssetCollisionFilter.begin(),
                    ClothConfig.PhysicsAssetCollisionFilter.end(),
                    [BodyIndex, ShapeIndex](const FSkeletalClothPhysicsAssetCollisionFilterEntry& Entry)
                    {
                        return Entry.BodyIndex == BodyIndex && Entry.ShapeIndex == ShapeIndex;
                    }) != ClothConfig.PhysicsAssetCollisionFilter.end();
            if (!bIncludeShape)
            {
                FClothCollisionCandidate Rejected;
                Rejected.SourceId.Source = EClothCollisionSource::PhysicsAsset;
                Rejected.SourceId.OwnerActorId = OwnerActorId;
                Rejected.SourceId.OwnerComponentId = OwnerComponentId;
                Rejected.SourceId.BoneName = BodySetup.BoneName;
                Rejected.SourceId.BodyIndex = BodyIndex;
                Rejected.SourceId.ShapeIndex = ShapeIndex;
                Rejected.State = EClothCollisionSelectState::RejectedByParticipationFilter;
                Result.Candidates.push_back(Rejected);
                ++Result.Stats.Rejected;
                continue;
            }

            FClothCollisionCandidate Candidate;
            Candidate.SourceId.Source = EClothCollisionSource::PhysicsAsset;
            Candidate.SourceId.OwnerActorId = OwnerActorId;
            Candidate.SourceId.OwnerComponentId = OwnerComponentId;
            Candidate.SourceId.BoneName = BodySetup.BoneName;
            Candidate.SourceId.BodyIndex = BodyIndex;
            Candidate.SourceId.ShapeIndex = ShapeIndex;
            Candidate.StableTieBreaker =
                (static_cast<uint64>(BodyIndex) << 32) | static_cast<uint32>(ShapeIndex);

            switch (ShapeSetup.Type)
            {
            case EPhysicsAssetShapeType::Sphere:
                Candidate.Type = EClothCollisionPrimitiveType::Sphere;
                Candidate.Radius = ShapeSetup.SphereRadius;
                break;
            case EPhysicsAssetShapeType::Capsule:
                Candidate.Type = EClothCollisionPrimitiveType::Capsule;
                Candidate.Radius = ShapeSetup.CapsuleRadius;
                Candidate.CapsuleHalfHeight = ShapeSetup.CapsuleHalfHeight;
                break;
            case EPhysicsAssetShapeType::Box:
                Candidate.Type = EClothCollisionPrimitiveType::Box;
                Candidate.HalfExtent = ShapeSetup.BoxHalfExtent;
                break;
            default:
                Candidate.State = EClothCollisionSelectState::SkippedFilter;
                ++Result.Stats.Rejected;
                Result.Candidates.push_back(Candidate);
                continue;
            }

            Candidate.LocalTransform =
                ComposeClothCollisionTransforms(BodyComponentTransform, ShapeSetup.LocalTransform);
            Candidate.PreviousLocalTransform = Candidate.LocalTransform;
            Candidate.CurrentLocalTransform = Candidate.LocalTransform;

            if (!IsValidTransformForCollision(Candidate.LocalTransform))
            {
                Candidate.State = EClothCollisionSelectState::SkippedInvalidTransform;
                ++Result.Stats.Rejected;
                Result.Candidates.push_back(Candidate);
                continue;
            }

            if (!IsUniformPositiveScale(Candidate.LocalTransform.Scale))
            {
                Candidate.State = EClothCollisionSelectState::SkippedNonUniformScale;
                ++Result.Stats.SkippedNonUniformScale;
                Result.Candidates.push_back(Candidate);
                continue;
            }

            if ((Candidate.Type == EClothCollisionPrimitiveType::Sphere ||
                Candidate.Type == EClothCollisionPrimitiveType::Capsule) &&
                Candidate.Radius <= 0.0f)
            {
                Candidate.State = EClothCollisionSelectState::SkippedInvalidTransform;
                ++Result.Stats.Rejected;
                Result.Candidates.push_back(Candidate);
                continue;
            }

            if (Candidate.Type == EClothCollisionPrimitiveType::Capsule &&
                Candidate.CapsuleHalfHeight <= 0.0f)
            {
                Candidate.State = EClothCollisionSelectState::SkippedInvalidTransform;
                ++Result.Stats.Rejected;
                Result.Candidates.push_back(Candidate);
                continue;
            }

            if (Candidate.Type == EClothCollisionPrimitiveType::Box &&
                (Candidate.HalfExtent.X <= 0.0f ||
                    Candidate.HalfExtent.Y <= 0.0f ||
                    Candidate.HalfExtent.Z <= 0.0f))
            {
                Candidate.State = EClothCollisionSelectState::SkippedInvalidTransform;
                ++Result.Stats.Rejected;
                Result.Candidates.push_back(Candidate);
                continue;
            }

            AccumulateGatheredStat(Result.Stats, Candidate.Type);
            Result.Candidates.push_back(Candidate);
        }
    }
}

void FClothCollisionGatherer::GatherWorldStaticCandidates(
    FClothCollisionGatherResult& Result,
    const IPhysicsRuntime& PhysicsRuntime,
    const USkeletalMeshComponent& Component,
    const FSkeletalClothConfig& ClothConfig,
    const FBoundingBox& ComponentWorldBounds,
    const FBoundingBox& SectionWorldBounds) const
{
    if (!ClothConfig.bEnableWorldStaticClothCollision ||
        !ComponentWorldBounds.IsValid() ||
        !SectionWorldBounds.IsValid())
    {
        return;
    }

    GatherWorldCandidates(
        Result,
        PhysicsRuntime,
        Component,
        ComponentWorldBounds,
        SectionWorldBounds,
        EClothCollisionSource::WorldStatic,
        ObjectTypeBit(ECollisionChannel::WorldStatic));
}

void FClothCollisionGatherer::GatherWorldDynamicCandidates(
    FClothCollisionGatherResult& Result,
    const IPhysicsRuntime& PhysicsRuntime,
    const USkeletalMeshComponent& Component,
    const FSkeletalClothConfig& ClothConfig,
    const FBoundingBox& ComponentWorldBounds,
    const FBoundingBox& SectionWorldBounds) const
{
    if (!ClothConfig.bEnableWorldDynamicClothCollision ||
        !ComponentWorldBounds.IsValid() ||
        !SectionWorldBounds.IsValid())
    {
        return;
    }

    GatherWorldCandidates(
        Result,
        PhysicsRuntime,
        Component,
        ComponentWorldBounds,
        SectionWorldBounds,
        EClothCollisionSource::WorldDynamic,
        ObjectTypeBit(ECollisionChannel::WorldDynamic));
}

void FClothCollisionGatherer::GatherWorldCandidates(
    FClothCollisionGatherResult& Result,
    const IPhysicsRuntime& PhysicsRuntime,
    const USkeletalMeshComponent& Component,
    const FBoundingBox& ComponentWorldBounds,
    const FBoundingBox& SectionWorldBounds,
    EClothCollisionSource Source,
    uint32 ObjectTypeMask) const
{
    TArray<FPhysicsClothCollisionShape> Shapes;
    PhysicsRuntime.QueryClothCollisionShapes(
        ExpandBounds(ComponentWorldBounds, ClothWorldCollisionSectionExpansion),
        ObjectTypeMask,
        Shapes);

    const FBoundingBox ExpandedSectionWorldBounds =
        ExpandBounds(SectionWorldBounds, ClothWorldCollisionSectionExpansion);
    const uint32 OwnerComponentId = Component.GetUUID();
    const FVector ComponentCollisionScale = GetComponentCollisionScale(Component);
    const float UniformCollisionScale = GetUniformCollisionScale(ComponentCollisionScale);

    for (const FPhysicsClothCollisionShape& Shape : Shapes)
    {
        if (Shape.OwnerComponentId == OwnerComponentId)
        {
            continue;
        }

        if (Source == EClothCollisionSource::WorldStatic &&
            Shape.BodyType != EPhysicsBodyType::Static)
        {
            continue;
        }

        FClothCollisionCandidate Candidate;
        Candidate.SourceId.Source = Source;
        Candidate.SourceId.OwnerActorId = Shape.OwnerActorId;
        Candidate.SourceId.OwnerComponentId = Shape.OwnerComponentId;
        Candidate.SourceId.BodyIndex = static_cast<int32>(Shape.Body.Index);
        Candidate.SourceId.ShapeIndex = static_cast<int32>(Shape.Shape.Index);
        Candidate.SourceId.ObjectChannel = ToCollisionChannel(Shape.FilterData.ObjectType);
        Candidate.WorldBounds = Shape.WorldBounds;
        Candidate.StableTieBreaker =
            (static_cast<uint64>(Source == EClothCollisionSource::WorldDynamic ? 2u : 1u) << 60) ^
            (static_cast<uint64>(Shape.OwnerComponentId) << 32) ^
            (static_cast<uint64>(Shape.Body.Index) << 16) ^
            static_cast<uint64>(Shape.Shape.Index);

        switch (Shape.Type)
        {
        case EPhysicsShapeType::Sphere:
            Candidate.Type = EClothCollisionPrimitiveType::Sphere;
            Candidate.Radius = Shape.SphereRadius / UniformCollisionScale;
            Candidate.TypeCost = 1;
            break;
        case EPhysicsShapeType::Capsule:
            Candidate.Type = EClothCollisionPrimitiveType::Capsule;
            Candidate.Radius = Shape.CapsuleRadius / UniformCollisionScale;
            Candidate.CapsuleHalfHeight = Shape.CapsuleHalfHeight / UniformCollisionScale;
            Candidate.TypeCost = 1;
            break;
        case EPhysicsShapeType::Box:
            Candidate.Type = EClothCollisionPrimitiveType::Box;
            Candidate.HalfExtent = DivideByComponentScale(Shape.BoxHalfExtent, ComponentCollisionScale);
            Candidate.TypeCost = 6;
            break;
        default:
            Candidate.State = EClothCollisionSelectState::SkippedFilter;
            if (Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.RejectedWorldDynamic;
            }
            else
            {
                ++Result.Stats.RejectedWorldStatic;
            }
            ++Result.Stats.Rejected;
            Result.Candidates.push_back(Candidate);
            continue;
        }

        Candidate.LocalTransform = MakeComponentLocalTransform(Component, Shape.CurrentWorldTransform);
        Candidate.PreviousLocalTransform = MakeComponentLocalTransform(Component, Shape.PreviousWorldTransform);
        Candidate.CurrentLocalTransform = Candidate.LocalTransform;

        if (!IsWorldShapeBlocking(Shape))
        {
            Candidate.State = EClothCollisionSelectState::SkippedFilter;
            if (Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.RejectedWorldDynamic;
            }
            else
            {
                ++Result.Stats.RejectedWorldStatic;
            }
            ++Result.Stats.Rejected;
            Result.Candidates.push_back(Candidate);
            continue;
        }

        if (!Shape.WorldBounds.IsIntersected(ExpandedSectionWorldBounds))
        {
            Candidate.State = EClothCollisionSelectState::RejectedBySectionBounds;
            if (Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.RejectedWorldDynamic;
            }
            else
            {
                ++Result.Stats.RejectedWorldStatic;
            }
            ++Result.Stats.Rejected;
            Result.Candidates.push_back(Candidate);
            continue;
        }

        Candidate.OverlapRank = Shape.WorldBounds.IsIntersected(SectionWorldBounds) ? 0 : 1;
        Candidate.DistanceScore = ComputeBoundsDistanceSquared(Shape.WorldBounds, SectionWorldBounds);
        Candidate.CenterDistanceScore =
            (Shape.WorldBounds.GetCenter() - SectionWorldBounds.GetCenter()).Dot(
                Shape.WorldBounds.GetCenter() - SectionWorldBounds.GetCenter());

        if (!IsValidTransformForCollision(Candidate.LocalTransform))
        {
            Candidate.State = EClothCollisionSelectState::SkippedInvalidTransform;
            if (Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.RejectedWorldDynamic;
            }
            else
            {
                ++Result.Stats.RejectedWorldStatic;
            }
            ++Result.Stats.Rejected;
            Result.Candidates.push_back(Candidate);
            continue;
        }

        if ((Candidate.Type == EClothCollisionPrimitiveType::Sphere ||
            Candidate.Type == EClothCollisionPrimitiveType::Capsule) &&
            Candidate.Radius <= 0.0f)
        {
            Candidate.State = EClothCollisionSelectState::SkippedInvalidTransform;
            if (Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.RejectedWorldDynamic;
            }
            else
            {
                ++Result.Stats.RejectedWorldStatic;
            }
            ++Result.Stats.Rejected;
            Result.Candidates.push_back(Candidate);
            continue;
        }

        if (Candidate.Type == EClothCollisionPrimitiveType::Capsule &&
            Candidate.CapsuleHalfHeight <= 0.0f)
        {
            Candidate.State = EClothCollisionSelectState::SkippedInvalidTransform;
            if (Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.RejectedWorldDynamic;
            }
            else
            {
                ++Result.Stats.RejectedWorldStatic;
            }
            ++Result.Stats.Rejected;
            Result.Candidates.push_back(Candidate);
            continue;
        }

        if (Candidate.Type == EClothCollisionPrimitiveType::Box &&
            (Candidate.HalfExtent.X <= 0.0f ||
                Candidate.HalfExtent.Y <= 0.0f ||
                Candidate.HalfExtent.Z <= 0.0f))
        {
            Candidate.State = EClothCollisionSelectState::SkippedInvalidTransform;
            if (Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.RejectedWorldDynamic;
            }
            else
            {
                ++Result.Stats.RejectedWorldStatic;
            }
            ++Result.Stats.Rejected;
            Result.Candidates.push_back(Candidate);
            continue;
        }

        if (Source == EClothCollisionSource::WorldDynamic)
        {
            ++Result.Stats.GatheredWorldDynamic;
        }
        else
        {
            ++Result.Stats.GatheredWorldStatic;
        }
        AccumulateGatheredStat(Result.Stats, Candidate.Type);
        Result.Candidates.push_back(Candidate);
    }
}

void FClothCollisionGatherer::SelectWithinBudget(
    FClothCollisionGatherResult& Result,
    const FClothCollisionBudget& Budget) const
{
    uint32 SelectedSphereCount = 0;
    uint32 SelectedCapsuleCount = 0;
    uint32 SelectedBoxCount = 0;

    std::sort(
        Result.Candidates.begin(),
        Result.Candidates.end(),
        [](const FClothCollisionCandidate& A, const FClothCollisionCandidate& B)
        {
            const int32 SourcePriorityA = GetClothCollisionSourcePriority(A.SourceId.Source);
            const int32 SourcePriorityB = GetClothCollisionSourcePriority(B.SourceId.Source);
            if (SourcePriorityA != SourcePriorityB)
            {
                return SourcePriorityA < SourcePriorityB;
            }
            if (A.OverlapRank != B.OverlapRank)
            {
                return A.OverlapRank < B.OverlapRank;
            }
            if (A.DistanceScore != B.DistanceScore)
            {
                return A.DistanceScore < B.DistanceScore;
            }
            if (A.CenterDistanceScore != B.CenterDistanceScore)
            {
                return A.CenterDistanceScore < B.CenterDistanceScore;
            }
            if (A.TypeCost != B.TypeCost)
            {
                return A.TypeCost < B.TypeCost;
            }
            return A.StableTieBreaker < B.StableTieBreaker;
        });

    for (FClothCollisionCandidate& Candidate : Result.Candidates)
    {
        if (Candidate.State != EClothCollisionSelectState::Gathered)
        {
            continue;
        }

        bool bSelect = false;
        switch (Candidate.Type)
        {
        case EClothCollisionPrimitiveType::Sphere:
            bSelect = SelectedSphereCount < Budget.MaxAuthoredSpheres;
            if (bSelect)
            {
                ++SelectedSphereCount;
                FClothCollisionSphere Sphere;
                Sphere.LocalCenter = Candidate.LocalTransform.Location;
                Sphere.Radius = Candidate.Radius * Candidate.LocalTransform.Scale.X;
                Result.SelectedPrimitives.Spheres.push_back(Sphere);
            }
            break;
        case EClothCollisionPrimitiveType::Capsule:
            bSelect = SelectedCapsuleCount < Budget.MaxAuthoredCapsules;
            if (bSelect)
            {
                ++SelectedCapsuleCount;
                const FVector LocalAxis = Candidate.LocalTransform.Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));
                const float HalfHeight = Candidate.CapsuleHalfHeight * Candidate.LocalTransform.Scale.X;
                FClothCollisionCapsule Capsule;
                Capsule.LocalPoint0 = Candidate.LocalTransform.Location - LocalAxis * HalfHeight;
                Capsule.LocalPoint1 = Candidate.LocalTransform.Location + LocalAxis * HalfHeight;
                Capsule.Radius = Candidate.Radius * Candidate.LocalTransform.Scale.X;
                Result.SelectedPrimitives.Capsules.push_back(Capsule);
            }
            break;
        case EClothCollisionPrimitiveType::Box:
            bSelect = SelectedBoxCount < Budget.MaxAuthoredBoxes &&
                (SelectedBoxCount + 1) * 6 <= Budget.MaxNvClothPlanes &&
                SelectedBoxCount < Budget.MaxNvClothConvexes;
            if (bSelect)
            {
                ++SelectedBoxCount;
                FClothCollisionBox Box;
                Box.LocalTransform = Candidate.LocalTransform;
                Box.HalfExtent = Candidate.HalfExtent * Candidate.LocalTransform.Scale.X;
                Result.SelectedPrimitives.Boxes.push_back(Box);
            }
            break;
        default:
            break;
        }

        if (bSelect)
        {
            Candidate.State = EClothCollisionSelectState::Selected;
            AccumulateSelectedStat(Result.Stats, Candidate.Type);
            if (Candidate.SourceId.Source == EClothCollisionSource::WorldStatic)
            {
                ++Result.Stats.SelectedWorldStatic;
            }
            else if (Candidate.SourceId.Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.SelectedWorldDynamic;
            }
        }
        else
        {
            Candidate.State = EClothCollisionSelectState::TruncatedByBudget;
            ++Result.Stats.Truncated;
            if (Candidate.SourceId.Source == EClothCollisionSource::WorldStatic)
            {
                ++Result.Stats.TruncatedWorldStatic;
            }
            else if (Candidate.SourceId.Source == EClothCollisionSource::WorldDynamic)
            {
                ++Result.Stats.TruncatedWorldDynamic;
            }
        }
    }
}
