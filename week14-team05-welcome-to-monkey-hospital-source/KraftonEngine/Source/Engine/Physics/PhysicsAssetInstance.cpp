#include "Physics/PhysicsAssetInstance.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsRuntime.h"

#include <algorithm>
#include <cmath>

namespace
{
    FVector AbsVector(const FVector& Value)
    {
        return FVector(
            std::fabs(Value.X),
            std::fabs(Value.Y),
            std::fabs(Value.Z));
    }

    FVector MultiplyVectors(const FVector& A, const FVector& B)
    {
        return FVector(A.X * B.X, A.Y * B.Y, A.Z * B.Z);
    }

    float MaxComponent(const FVector& Value)
    {
        return (std::max)(Value.X, (std::max)(Value.Y, Value.Z));
    }

    // PhysicsAsset body/constraint local frames are authored relative to bones.
    // Runtime creation and pose sync both use the same composition rule so the two
    // directions stay mathematically symmetric.
    FTransform ComposePhysicsTransforms(const FTransform& ParentWorld, const FTransform& Local)
    {
        FTransform Result = Local;
        Result.Location = ParentWorld.Location + ParentWorld.Rotation.RotateVector(MultiplyVectors(Local.Location, ParentWorld.Scale));
        Result.Rotation = ParentWorld.Rotation * Local.Rotation;
        Result.Scale = MultiplyVectors(ParentWorld.Scale, Local.Scale);
        return Result;
    }

    FTransform ComputeParentWorldTransformFromChild(const FTransform& ChildWorld, const FTransform& ChildLocalToParent)
    {
        FTransform Result;
        Result.Rotation = ChildWorld.Rotation * ChildLocalToParent.Rotation.Inverse();
        Result.Scale = FVector(
            std::fabs(ChildLocalToParent.Scale.X) > 1.0e-6f ? ChildWorld.Scale.X / ChildLocalToParent.Scale.X : ChildWorld.Scale.X,
            std::fabs(ChildLocalToParent.Scale.Y) > 1.0e-6f ? ChildWorld.Scale.Y / ChildLocalToParent.Scale.Y : ChildWorld.Scale.Y,
            std::fabs(ChildLocalToParent.Scale.Z) > 1.0e-6f ? ChildWorld.Scale.Z / ChildLocalToParent.Scale.Z : ChildWorld.Scale.Z);
        Result.Location = ChildWorld.Location - Result.Rotation.RotateVector(MultiplyVectors(ChildLocalToParent.Location, Result.Scale));
        return Result;
    }

    FTransform GetComponentWorldTransform(const USkeletalMeshComponent* Component)
    {
        FTransform Result;
        if (!Component)
        {
            return Result;
        }

        return FTransform(Component->GetWorldMatrix());
    }

    FTransform ScaleLocalFrame(const FTransform& Frame, const FVector& Scale)
    {
        FTransform Result = Frame;
        Result.Location = MultiplyVectors(Frame.Location, Scale);
        Result.Scale = FVector::OneVector;
        return Result;
    }

    IPhysicsRuntime* GetPhysicsRuntime(USkeletalMeshComponent* Component)
    {
        if (!Component)
        {
            return nullptr;
        }

        UWorld* World = Component->GetWorld();
        if (!World)
        {
            return nullptr;
        }

        IPhysicsScene* PhysicsScene = World->GetPhysicsScene();
        return PhysicsScene ? PhysicsScene->GetRuntime() : nullptr;
    }

    const IPhysicsRuntime* GetPhysicsRuntime(const USkeletalMeshComponent* Component)
    {
        if (!Component)
        {
            return nullptr;
        }

        UWorld* World = Component->GetWorld();
        if (!World)
        {
            return nullptr;
        }

        const IPhysicsScene* PhysicsScene = World->GetPhysicsScene();
        return PhysicsScene ? PhysicsScene->GetRuntime() : nullptr;
    }

    void FillShapeFilterDataFromComponent(
        FPhysicsFilterData& OutFilterData,
        const USkeletalMeshComponent* Component,
        bool bForceQueryAndPhysicsCollision,
        bool bUseIndependentRagdollCollision,
        ECollisionEnabled IndependentCollisionEnabled,
        bool bIndependentGenerateOverlapEvents,
        bool bIsPartialRagdollBody,
        bool bSuppressSameActorPrimitiveCollisionForPartial,
        bool bSuppressSameActorPrimitiveOverlapForPartial)
    {
        if (!Component)
        {
            return;
        }

        OutFilterData.ObjectType = static_cast<uint32>(Component->GetCollisionObjectType());
        OutFilterData.BlockMask = 0;
        OutFilterData.OverlapMask = 0;
        // Keep ragdoll-vs-ragdoll self-collision decisions at the PhysicsAsset constraint
        // layer, but still stamp the owning actor so filter policy can reject ragdoll
        // body vs same-actor character primitive pairs.
        OutFilterData.IgnoreGroup =
            (bUseIndependentRagdollCollision && Component->GetOwner())
                ? Component->GetOwner()->GetUUID()
                : 0;
        OutFilterData.CollisionEnabled = bUseIndependentRagdollCollision
            ? IndependentCollisionEnabled
            : (bForceQueryAndPhysicsCollision
                ? ECollisionEnabled::QueryAndPhysics
                : Component->GetCollisionEnabled());
        OutFilterData.bIsTrigger = false;
        OutFilterData.bGenerateHitEvents = true;
        OutFilterData.bGenerateOverlapEvents = bUseIndependentRagdollCollision
            ? bIndependentGenerateOverlapEvents
            : (bForceQueryAndPhysicsCollision
                ? false
                : Component->GetGenerateOverlapEvents());
        OutFilterData.bIsIndependentRagdoll = bUseIndependentRagdollCollision;
        OutFilterData.bIsPartialRagdoll = bIsPartialRagdollBody;
        OutFilterData.CollisionRole = bIsPartialRagdollBody
            ? EPhysicsCollisionRole::PartialReactionBody
            : (bUseIndependentRagdollCollision
                ? EPhysicsCollisionRole::FullRagdollBody
                : EPhysicsCollisionRole::None);
        // Full ragdoll also must not fight the owning character primitive. The high-level
        // policy turns capsule/mesh collision down, while this filter flag protects the
        // frame where physics commands may still observe stale primitive shapes.
        OutFilterData.bSuppressSameActorPrimitivePairs =
            bUseIndependentRagdollCollision &&
            (!bIsPartialRagdollBody ||
                bSuppressSameActorPrimitiveCollisionForPartial ||
                bSuppressSameActorPrimitiveOverlapForPartial);
        OutFilterData.GameplayOverlapOwnership =
            GetDefaultGameplayOverlapOwnershipForRole(OutFilterData.CollisionRole);

        for (int32 ChannelIndex = 0; ChannelIndex < static_cast<int32>(ECollisionChannel::ActiveCount); ++ChannelIndex)
        {
            const ECollisionResponse Response =
                (bForceQueryAndPhysicsCollision && !bUseIndependentRagdollCollision)
                    ? ECollisionResponse::Block
                    : Component->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(ChannelIndex));

            if (Response == ECollisionResponse::Block)
            {
                OutFilterData.BlockMask |= (1u << ChannelIndex);
            }
            else if (Response == ECollisionResponse::Overlap)
            {
                OutFilterData.OverlapMask |= (1u << ChannelIndex);
            }
        }
    }

    void BuildShapeDescs(
        const FPhysicsAssetBodySetup& BodySetup,
        const USkeletalMeshComponent* OwnerComponent,
        const FPhysicsAssetSimulationOptions& Options,
        const FVector& BodyWorldScale,
        TArray<FPhysicsShapeDesc>& OutShapes
    )
    {
        OutShapes.clear();
        const FVector ShapeScale = AbsVector(BodyWorldScale);
        const float UniformShapeScale = MaxComponent(ShapeScale);

        for (const FPhysicsAssetShapeSetup& ShapeSetup : BodySetup.Shapes)
        {
            FPhysicsShapeDesc ShapeDesc;
            ShapeDesc.LocalTransform = ScaleLocalFrame(ShapeSetup.LocalTransform, BodyWorldScale);
            ShapeDesc.CollisionEnabled = Options.bUseIndependentRagdollCollision
                ? Options.IndependentCollisionEnabled
                : (Options.bForceQueryAndPhysicsCollision
                    ? ECollisionEnabled::QueryAndPhysics
                    : (OwnerComponent ? OwnerComponent->GetCollisionEnabled() : ECollisionEnabled::QueryAndPhysics));
            ShapeDesc.bIsTrigger = false;
            FillShapeFilterDataFromComponent(
                ShapeDesc.FilterData,
                OwnerComponent,
                Options.bForceQueryAndPhysicsCollision,
                Options.bUseIndependentRagdollCollision,
                Options.IndependentCollisionEnabled,
                Options.bIndependentGenerateOverlapEvents,
                Options.bPartialSimulation,
                Options.bSuppressSameActorPrimitiveCollisionForPartial,
                Options.bSuppressSameActorPrimitiveOverlapForPartial);
            ShapeDesc.FilterData.bEnableCCD = BodySetup.bEnableCCD;
            ShapeDesc.QueryIgnoreGroup = (OwnerComponent && OwnerComponent->GetOwner())
                ? OwnerComponent->GetOwner()->GetUUID()
                : 0;

            switch (ShapeSetup.Type)
            {
            case EPhysicsAssetShapeType::Box:
                ShapeDesc.Type = EPhysicsShapeType::Box;
                ShapeDesc.BoxHalfExtent = MultiplyVectors(ShapeSetup.BoxHalfExtent, ShapeScale);
                break;
            case EPhysicsAssetShapeType::Sphere:
                ShapeDesc.Type = EPhysicsShapeType::Sphere;
                ShapeDesc.SphereRadius = ShapeSetup.SphereRadius * UniformShapeScale;
                break;
            case EPhysicsAssetShapeType::Capsule:
                ShapeDesc.Type = EPhysicsShapeType::Capsule;
                ShapeDesc.CapsuleRadius = ShapeSetup.CapsuleRadius * UniformShapeScale;
                ShapeDesc.CapsuleHalfHeight = ShapeSetup.CapsuleHalfHeight * UniformShapeScale;
                break;
            default:
                continue;
            }

            OutShapes.push_back(ShapeDesc);
        }
    }

    bool BuildBodyCreationDesc(
        USkeletalMeshComponent* OwnerComponent,
        const FPhysicsAssetBodySetup& BodySetup,
        const FTransform& BoneWorldTransform,
        const FPhysicsAssetSimulationOptions& Options,
        FBodyCreationDesc& OutDesc
    )
    {
        if (!OwnerComponent)
        {
            return false;
        }

        OutDesc                          = FBodyCreationDesc {};
        OutDesc.OwnerActorId             = OwnerComponent->GetOwner() ? OwnerComponent->GetOwner()->GetUUID() : 0;
        OutDesc.OwnerComponentId         = OwnerComponent->GetUUID();
        OutDesc.OwnerComponentGeneration = 1;
        OutDesc.Domain                   = EPhysicsBodyDomain::Ragdoll;
        OutDesc.BoneName                 = BodySetup.BoneName;
        OutDesc.BodyType                 = EPhysicsBodyType::Dynamic;
        // Ragdoll bodies stay manual because skeletal pose sync is driven explicitly by
        // the component rather than by generic component/world transform mirroring.
        OutDesc.SyncMode = EPhysicsSyncMode::Manual;
        const FTransform BodyWorldTransform = ComposePhysicsTransforms(BoneWorldTransform, BodySetup.BodyLocalFrame);
        OutDesc.WorldTransform = BodyWorldTransform;
        OutDesc.WorldTransform.Scale = FVector::OneVector;

        BuildShapeDescs(BodySetup, OwnerComponent, Options, BodyWorldTransform.Scale, OutDesc.Shapes);
        if (OutDesc.Shapes.empty())
        {
            return false;
        }

        OutDesc.Mass = BodySetup.Mass;
        OutDesc.CenterOfMassLocalOffset = BodySetup.CenterOfMassLocalOffset;
        OutDesc.LinearDamping = BodySetup.LinearDamping;
        OutDesc.AngularDamping = BodySetup.AngularDamping;
        OutDesc.MaxAngularVelocity = BodySetup.MaxAngularVelocity;
        OutDesc.PositionSolverIterationCount = BodySetup.PositionSolverIterationCount;
        OutDesc.VelocitySolverIterationCount = BodySetup.VelocitySolverIterationCount;
        OutDesc.bEnableGravity = BodySetup.bEnableGravity;
        OutDesc.bEnableCCD = BodySetup.bEnableCCD;
        OutDesc.bGenerateHitEvents = true;
        OutDesc.bGenerateOverlapEvents = Options.bUseIndependentRagdollCollision
            ? Options.bIndependentGenerateOverlapEvents
            : OwnerComponent->GetGenerateOverlapEvents();
        OutDesc.bLockLinearX = BodySetup.bLockLinearX;
        OutDesc.bLockLinearY = BodySetup.bLockLinearY;
        OutDesc.bLockLinearZ = BodySetup.bLockLinearZ;
        OutDesc.bLockAngularX = BodySetup.bLockAngularX;
        OutDesc.bLockAngularY = BodySetup.bLockAngularY;
        OutDesc.bLockAngularZ = BodySetup.bLockAngularZ;
        return true;
    }

    bool BuildConstraintCreationDesc(
        const FPhysicsAssetConstraintSetup& ConstraintSetup,
        const FVector& ParentBodyScale,
        const FVector& ChildBodyScale,
        FConstraintCreationDesc& OutDesc
    )
    {
        OutDesc = FConstraintCreationDesc{};
        OutDesc.ParentLocalFrame = ScaleLocalFrame(ConstraintSetup.ParentLocalFrame, ParentBodyScale);
        OutDesc.ChildLocalFrame = ScaleLocalFrame(ConstraintSetup.ChildLocalFrame, ChildBodyScale);
        OutDesc.Limits = ConstraintSetup.Limits;
        OutDesc.bDisableCollisionBetweenBodies = ConstraintSetup.bDisableCollisionBetweenBodies;
        return true;
    }

    bool ComputeBoneWorldTransformFromBody(
        const FPhysicsAssetBodySetup& BodySetup,
        const FVector& BodyWorldScale,
        const FTransform& BodyWorld,
        FTransform& OutBoneWorld
    )
    {
        // BodyLocalFrame is the authored offset from a bone to its rigid body. Pose sync
        // reverses that offset so the simulated body can become the final bone transform.
        const FQuat InverseBodyLocalRotation = BodySetup.BodyLocalFrame.Rotation.Inverse().GetNormalized();
        OutBoneWorld = BodyWorld;
        OutBoneWorld.Scale = FVector(
            std::fabs(BodySetup.BodyLocalFrame.Scale.X) > 1.0e-6f ? BodyWorldScale.X / BodySetup.BodyLocalFrame.Scale.X : BodyWorldScale.X,
            std::fabs(BodySetup.BodyLocalFrame.Scale.Y) > 1.0e-6f ? BodyWorldScale.Y / BodySetup.BodyLocalFrame.Scale.Y : BodyWorldScale.Y,
            std::fabs(BodySetup.BodyLocalFrame.Scale.Z) > 1.0e-6f ? BodyWorldScale.Z / BodySetup.BodyLocalFrame.Scale.Z : BodyWorldScale.Z);
        OutBoneWorld.Rotation = (BodyWorld.Rotation * InverseBodyLocalRotation).GetNormalized();
        OutBoneWorld.Location =
            BodyWorld.Location - OutBoneWorld.Rotation.RotateVector(MultiplyVectors(BodySetup.BodyLocalFrame.Location, OutBoneWorld.Scale));
        return true;
    }


    bool IsSameOrDescendantBone(const FSkeletalMesh* MeshAsset, int32 BoneIndex, int32 PossibleAncestorIndex)
    {
        if (!MeshAsset || BoneIndex < 0 || PossibleAncestorIndex < 0 ||
            BoneIndex >= static_cast<int32>(MeshAsset->Bones.size()) ||
            PossibleAncestorIndex >= static_cast<int32>(MeshAsset->Bones.size()))
        {
            return false;
        }

        int32 Cursor = BoneIndex;
        while (Cursor >= 0 && Cursor < static_cast<int32>(MeshAsset->Bones.size()))
        {
            if (Cursor == PossibleAncestorIndex)
            {
                return true;
            }
            Cursor = MeshAsset->Bones[Cursor].ParentIndex;
        }
        return false;
    }
}

bool FPhysicsAssetInstance::Initialize(USkeletalMeshComponent* InOwner, UPhysicsAsset* InAsset)
{
    Shutdown();

    if (!InOwner || !InAsset)
    {
        return false;
    }

    USkeletalMesh* Mesh = InOwner->GetSkeletalMesh();
    FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset)
    {
        return false;
    }

    OwnerComponent = InOwner;
    SourceAsset = InAsset;

    const TArray<FPhysicsAssetBodySetup>& BodySetups = InAsset->GetBodySetups();
    BodiesByBone.resize(BodySetups.size());
    Constraints.resize(InAsset->GetConstraintSetups().size());

    // Bone name resolution is cached up front so runtime creation and pose sync do not
    // need to repeatedly scan the skeletal mesh bone list.
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
    {
        const FName& BoneName = BodySetups[BodyIndex].BoneName;
        int32 BoneIndex = -1;

        for (int32 MeshBoneIndex = 0; MeshBoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++MeshBoneIndex)
        {
            if (MeshAsset->Bones[MeshBoneIndex].Name == BoneName.ToString())
            {
                BoneIndex = MeshBoneIndex;
                break;
            }
        }

        BoneNameToIndex[BoneName.ToString()] = BoneIndex;
        if (BodyIndex == 0)
        {
            RagdollRootBoneIndex = BoneIndex;
        }
    }

    ResetRuntimeState();
    bInitialized = true;
    return true;
}

bool FPhysicsAssetInstance::CreateBodiesAndConstraints()
{
    return CreateBodiesAndConstraints(FPhysicsAssetSimulationOptions{});
}

bool FPhysicsAssetInstance::CreateBodiesAndConstraints(const FPhysicsAssetSimulationOptions& Options)
{
    if (!bInitialized)
    {
        return false;
    }

    if (HasLivePhysicsObjects())
    {
        return true;
    }

    USkeletalMeshComponent* Owner = GetOwnerComponent();
    UPhysicsAsset* Asset = GetAsset();
    IPhysicsRuntime* Runtime = GetPhysicsRuntime(Owner);
    if (!Owner || !Asset || !Runtime)
    {
        return false;
    }

    USkeletalMesh* Mesh = Owner->GetSkeletalMesh();
    FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset)
    {
        return false;
    }

    TArray<FTransform> BoneComponentSpaceTransforms;
    Owner->GetCurrentBoneGlobalTransforms(BoneComponentSpaceTransforms);
    if (BoneComponentSpaceTransforms.size() < MeshAsset->Bones.size())
    {
        UE_LOG("CreateBodiesAndConstraints failed: skeletal pose is incomplete. Component=%s",
            Owner->GetName().c_str());
        return false;
    }

    const FTransform ComponentWorldTransform = GetComponentWorldTransform(Owner);
    const TArray<FPhysicsAssetBodySetup>& BodySetups = Asset->GetBodySetups();
    const TArray<FPhysicsAssetConstraintSetup>& ConstraintSetups = Asset->GetConstraintSetups();

    const bool bStrictPartialSimulation = Options.bPartialSimulation;
    const bool bRestrictSimulationScope = Options.bPartialSimulation || Options.bSelectedOnly;
    const FName ScopeRootBoneName =
        Options.bPartialSimulation
            ? Options.PartialRootBoneName
            : Options.SelectedBoneName;
    const bool bIncludeScopeDescendants =
        Options.bPartialSimulation
            ? Options.bIncludePartialDescendants
            : true;

    TArray<uint8> SimulatedBodyMask;
    SimulatedBodyMask.resize(BodySetups.size(), 1);
    if (bRestrictSimulationScope)
    {
        SimulatedBodyMask.assign(BodySetups.size(), 0);
        const int32 SelectedBoneIndex = FindBoneIndexForBody(ScopeRootBoneName);
        if (SelectedBoneIndex < 0)
        {
            UE_LOG("CreateBodiesAndConstraints failed: partial/selected simulation has no valid root body. Component=%s Bone=%s",
                Owner->GetName().c_str(),
                ScopeRootBoneName.ToString().c_str());
            return false;
        }

        int32 SelectedDynamicBodyCount = 0;
        for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
        {
            const int32 BodyBoneIndex = FindBoneIndexForBody(BodySetups[BodyIndex].BoneName);
            const bool bInScope = bIncludeScopeDescendants
                ? IsSameOrDescendantBone(MeshAsset, BodyBoneIndex, SelectedBoneIndex)
                : (BodyBoneIndex == SelectedBoneIndex);
            if (bInScope)
            {
                SimulatedBodyMask[BodyIndex] = 1;
                ++SelectedDynamicBodyCount;
            }
        }

        if (SelectedDynamicBodyCount <= 0)
        {
            UE_LOG("CreateBodiesAndConstraints failed: selected body chain is empty. Component=%s Bone=%s",
                Owner->GetName().c_str(),
                ScopeRootBoneName.ToString().c_str());
            return false;
        }
    }

    if (BodiesByBone.size() != BodySetups.size())
    {
        BodiesByBone.resize(BodySetups.size());
    }
    if (Constraints.size() != ConstraintSetups.size())
    {
        Constraints.resize(ConstraintSetups.size());
    }
    ResetRuntimeState();
    const bool bRequestedPartialSameActorPrimitiveSuppression =
        Options.bPartialSimulation &&
        (Options.bSuppressSameActorPrimitiveCollisionForPartial ||
            Options.bSuppressSameActorPrimitiveOverlapForPartial);

    int32 CreatedBodyCount = 0;
    int32 CreatedConstraintCount = 0;
    TArray<FVector> BodyWorldScales;
    BodyWorldScales.resize(BodySetups.size(), FVector::OneVector);

    // Bodies must exist before constraints because joints are expressed in terms of
    // two already-created rigid bodies.
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
    {
        const FPhysicsAssetBodySetup& BodySetup = BodySetups[BodyIndex];
        const int32 BoneIndex = FindBoneIndexForBody(BodySetup.BoneName);
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(BoneComponentSpaceTransforms.size()))
        {
            UE_LOG("Skipped PhysicsAsset body: bone not found. Component=%s Bone=%s",
                Owner->GetName().c_str(),
                BodySetup.BoneName.ToString().c_str());
            continue;
        }

        const FTransform BoneWorldTransform =
            ComposePhysicsTransforms(ComponentWorldTransform, BoneComponentSpaceTransforms[BoneIndex]);
        const FTransform BodyWorldTransform =
            ComposePhysicsTransforms(BoneWorldTransform, BodySetup.BodyLocalFrame);

        FBodyCreationDesc BodyDesc;
        if (!BuildBodyCreationDesc(
                Owner,
                BodySetup,
                BoneWorldTransform,
                Options,
                BodyDesc))
        {
            UE_LOG("Skipped PhysicsAsset body: invalid body setup. Component=%s Bone=%s",
                Owner->GetName().c_str(),
                BodySetup.BoneName.ToString().c_str());
            continue;
        }

        const bool bSimulateThisBody =
            BodyIndex >= 0 && BodyIndex < static_cast<int32>(SimulatedBodyMask.size())
                ? SimulatedBodyMask[BodyIndex] != 0
                : true;
        if (bStrictPartialSimulation && !bSimulateThisBody)
        {
            continue;
        }

        if (Options.bSelectedOnly && !Options.bPartialSimulation && !bSimulateThisBody)
        {
            BodyDesc.BodyType = EPhysicsBodyType::Kinematic;
            BodyDesc.bEnableGravity = false;
        }
        else if (Options.bNoGravity)
        {
            BodyDesc.bEnableGravity = false;
        }

        FPhysicsBodyHandle BodyHandle = Runtime->CreateRigidBody(BodyDesc);
        if (!BodyHandle.IsValid())
        {
            UE_LOG("Failed to create PhysicsAsset body. Component=%s Bone=%s",
                Owner->GetName().c_str(),
                BodySetup.BoneName.ToString().c_str());
            continue;
        }

        BodiesByBone[BodyIndex] = BodyHandle;
        BodyWorldScales[BodyIndex] = BodyWorldTransform.Scale;
        ++CreatedBodyCount;
    }

    for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(ConstraintSetups.size()); ++ConstraintIndex)
    {
        const FPhysicsAssetConstraintSetup& ConstraintSetup = ConstraintSetups[ConstraintIndex];
        const int32 ParentBodyIndex = Asset->FindBodySetupIndexByBoneName(ConstraintSetup.ParentBoneName);
        const int32 ChildBodyIndex = Asset->FindBodySetupIndexByBoneName(ConstraintSetup.ChildBoneName);
        const FPhysicsBodyHandle ParentHandle = GetBodyHandleByBoneName(ConstraintSetup.ParentBoneName);
        const FPhysicsBodyHandle ChildHandle = GetBodyHandleByBoneName(ConstraintSetup.ChildBoneName);

        if (bStrictPartialSimulation)
        {
            const bool bParentSimulated =
                ParentBodyIndex >= 0 && ParentBodyIndex < static_cast<int32>(SimulatedBodyMask.size()) &&
                SimulatedBodyMask[ParentBodyIndex] != 0;
            const bool bChildSimulated =
                ChildBodyIndex >= 0 && ChildBodyIndex < static_cast<int32>(SimulatedBodyMask.size()) &&
                SimulatedBodyMask[ChildBodyIndex] != 0;
            if (!bParentSimulated || !bChildSimulated)
            {
                continue;
            }
        }
        else if (Options.bSelectedOnly)
        {
            const bool bParentSimulated =
                ParentBodyIndex >= 0 && ParentBodyIndex < static_cast<int32>(SimulatedBodyMask.size()) &&
                SimulatedBodyMask[ParentBodyIndex] != 0;
            const bool bChildSimulated =
                ChildBodyIndex >= 0 && ChildBodyIndex < static_cast<int32>(SimulatedBodyMask.size()) &&
                SimulatedBodyMask[ChildBodyIndex] != 0;
            if (!bParentSimulated && !bChildSimulated)
            {
                continue;
            }
        }

        if (!ParentHandle.IsValid() || !ChildHandle.IsValid())
        {
            UE_LOG("Skipped PhysicsAsset constraint: missing body handle. Component=%s Parent=%s Child=%s",
                Owner->GetName().c_str(),
                ConstraintSetup.ParentBoneName.ToString().c_str(),
                ConstraintSetup.ChildBoneName.ToString().c_str());
            continue;
        }

        FConstraintCreationDesc ConstraintDesc;
        const FVector ParentBodyScale =
            ParentBodyIndex >= 0 && ParentBodyIndex < static_cast<int32>(BodyWorldScales.size())
                ? BodyWorldScales[ParentBodyIndex]
                : FVector::OneVector;
        const FVector ChildBodyScale =
            ChildBodyIndex >= 0 && ChildBodyIndex < static_cast<int32>(BodyWorldScales.size())
                ? BodyWorldScales[ChildBodyIndex]
                : FVector::OneVector;
        if (!BuildConstraintCreationDesc(ConstraintSetup, ParentBodyScale, ChildBodyScale, ConstraintDesc))
        {
            UE_LOG("Skipped PhysicsAsset constraint: invalid constraint setup. Component=%s Parent=%s Child=%s",
                Owner->GetName().c_str(),
                ConstraintSetup.ParentBoneName.ToString().c_str(),
                ConstraintSetup.ChildBoneName.ToString().c_str());
            continue;
        }

        FPhysicsConstraintHandle ConstraintHandle =
            Runtime->CreateConstraint(ParentHandle, ChildHandle, ConstraintDesc);
        if (!ConstraintHandle.IsValid())
        {
            UE_LOG("Failed to create PhysicsAsset constraint. Component=%s Parent=%s Child=%s",
                Owner->GetName().c_str(),
                ConstraintSetup.ParentBoneName.ToString().c_str(),
                ConstraintSetup.ChildBoneName.ToString().c_str());
            continue;
        }

        Constraints[ConstraintIndex] = ConstraintHandle;
        ++CreatedConstraintCount;
    }

    bPartialSameActorPrimitiveSuppressionActive =
        CreatedBodyCount > 0 && bRequestedPartialSameActorPrimitiveSuppression;

    UE_LOG("Created PhysicsAsset runtime objects. Component=%s Bodies=%d Constraints=%d Partial=%s RootBone=%s PartialSelfSuppression=%s",
        Owner->GetName().c_str(),
        CreatedBodyCount,
        CreatedConstraintCount,
        Options.bPartialSimulation ? "true" : "false",
        ScopeRootBoneName.ToString().c_str(),
        bPartialSameActorPrimitiveSuppressionActive ? "true" : "false");

    return CreatedBodyCount > 0;
}

void FPhysicsAssetInstance::DestroyBodiesAndConstraints()
{
    IPhysicsRuntime* Runtime = GetPhysicsRuntime(GetOwnerComponent());
    int32 DestroyedConstraintCount = 0;
    int32 DestroyedBodyCount = 0;

    // Constraints go first so joints never reference a body that has already been released.
    for (FPhysicsConstraintHandle& ConstraintHandle : Constraints)
    {
        if (!ConstraintHandle.IsValid())
        {
            continue;
        }

        if (Runtime)
        {
            Runtime->DestroyConstraint(ConstraintHandle);
        }
        ConstraintHandle = FPhysicsConstraintHandle{};
        ++DestroyedConstraintCount;
    }

    for (FPhysicsBodyHandle& BodyHandle : BodiesByBone)
    {
        if (!BodyHandle.IsValid())
        {
            continue;
        }

        if (Runtime)
        {
            Runtime->DestroyRigidBody(BodyHandle);
        }
        BodyHandle = FPhysicsBodyHandle{};
        ++DestroyedBodyCount;
    }

    if (DestroyedBodyCount > 0 || DestroyedConstraintCount > 0)
    {
        const USkeletalMeshComponent* Owner = GetOwnerComponent();
        UE_LOG("Destroyed PhysicsAsset runtime objects. Component=%s Bodies=%d Constraints=%d",
            Owner ? Owner->GetName().c_str() : "None",
            DestroyedBodyCount,
            DestroyedConstraintCount);
    }
}

void FPhysicsAssetInstance::Shutdown()
{
    // Shutdown is the strongest cleanup path: drop live runtime objects first, then
    // release cached ownership/binding state so the instance becomes fully inert.
    DestroyBodiesAndConstraints();
    BodiesByBone.clear();
    Constraints.clear();
    OwnerComponent.Reset();
    SourceAsset.Reset();
    BoneNameToIndex.clear();
    RagdollRootBoneIndex = -1;
    bPartialSameActorPrimitiveSuppressionActive = false;
    bInitialized = false;
}

void FPhysicsAssetInstance::ResetRuntimeState()
{
    // Reset keeps the instance attached to the same asset/component pairing while
    // discarding live runtime objects that may no longer match the current state.
    DestroyBodiesAndConstraints();
    bPartialSameActorPrimitiveSuppressionActive = false;
}

bool FPhysicsAssetInstance::HasLivePhysicsObjects() const
{
    return GetLiveBodyCount() > 0 || GetLiveConstraintCount() > 0;
}

int32 FPhysicsAssetInstance::GetLiveBodyCount() const
{
    int32 LiveBodyCount = 0;
    for (const FPhysicsBodyHandle& BodyHandle : BodiesByBone)
    {
        if (BodyHandle.IsValid())
        {
            ++LiveBodyCount;
        }
    }

    return LiveBodyCount;
}

int32 FPhysicsAssetInstance::GetLiveConstraintCount() const
{
    int32 LiveConstraintCount = 0;
    for (const FPhysicsConstraintHandle& ConstraintHandle : Constraints)
    {
        if (ConstraintHandle.IsValid())
        {
            ++LiveConstraintCount;
        }
    }

    return LiveConstraintCount;
}

bool FPhysicsAssetInstance::PullPhysicsPose(
    TArray<FTransform>& OutBoneWorldTransforms,
    const TArray<FTransform>* ReferenceBoneComponentSpaceTransforms,
    const TArray<FTransform>* ReferenceBoneLocalTransforms) const
{
    const USkeletalMeshComponent* Owner = GetOwnerComponent();
    const UPhysicsAsset* Asset = GetAsset();
    const IPhysicsRuntime* Runtime = GetPhysicsRuntime(Owner);
    if (!Owner || !Asset || !Runtime || GetLiveBodyCount() <= 0)
    {
        return false;
    }

    std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot = Runtime->AcquireLatestSnapshotRef();
    if (!Snapshot)
    {
        return false;
    }
    const uint32 SkeletalMeshComponentId = Owner->GetUUID();

    const USkeletalMesh* Mesh = Owner->GetSkeletalMesh();
    const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset)
    {
        return false;
    }

    TArray<FTransform> CurrentBoneComponentSpaceTransforms;
    const bool bUseReferenceComponentSpacePose =
        ReferenceBoneComponentSpaceTransforms &&
        ReferenceBoneComponentSpaceTransforms->size() >= MeshAsset->Bones.size();
    if (bUseReferenceComponentSpacePose)
    {
        CurrentBoneComponentSpaceTransforms = *ReferenceBoneComponentSpaceTransforms;
    }
    else
    {
        Owner->GetCurrentBoneGlobalTransforms(CurrentBoneComponentSpaceTransforms);
    }
    if (CurrentBoneComponentSpaceTransforms.size() < MeshAsset->Bones.size())
    {
        return false;
    }

    TArray<FTransform> CurrentBoneLocalTransforms;
    const bool bUseReferenceLocalPose =
        ReferenceBoneLocalTransforms &&
        ReferenceBoneLocalTransforms->size() >= MeshAsset->Bones.size();
    if (bUseReferenceLocalPose)
    {
        CurrentBoneLocalTransforms = *ReferenceBoneLocalTransforms;
    }
    else
    {
        CurrentBoneLocalTransforms.resize(MeshAsset->Bones.size());
        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
        {
            CurrentBoneLocalTransforms[BoneIndex] = Owner->GetBoneLocalTransformByIndex(BoneIndex);
        }
    }

    const FTransform ComponentWorldTransform = GetComponentWorldTransform(Owner);
    OutBoneWorldTransforms.resize(MeshAsset->Bones.size());
    // Start from the current animated pose so bones without bodies remain stable instead
    // of collapsing when a PhysicsAsset only covers part of the skeleton.
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
    {
        OutBoneWorldTransforms[BoneIndex] =
            ComposePhysicsTransforms(ComponentWorldTransform, CurrentBoneComponentSpaceTransforms[BoneIndex]);
    }

    TArray<uint8> AppliedBodyBoneMask;
    AppliedBodyBoneMask.resize(MeshAsset->Bones.size(), 0);

    const TArray<FPhysicsAssetBodySetup>& BodySetups = Asset->GetBodySetups();
    int32 AppliedBodyCount = 0;

    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
    {
        if (BodyIndex >= static_cast<int32>(BodiesByBone.size()))
        {
            continue;
        }

        const FPhysicsBodyHandle BodyHandle = BodiesByBone[BodyIndex];
        if (!BodyHandle.IsValid())
        {
            continue;
        }

        const FPhysicsAssetBodySetup& BodySetup = BodySetups[BodyIndex];
        const int32 BoneIndex = FindBoneIndexForBody(BodySetup.BoneName);
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(OutBoneWorldTransforms.size()))
        {
            continue;
        }

        const FPhysicsBodySnapshot* BodySnapshot =
                Snapshot->FindRagdollBone(SkeletalMeshComponentId, BodySetup.BoneName);
        if (!BodySnapshot)
        {
            continue;
        }
        const FTransform BodyWorld = BodySnapshot->CurrentTransform;
        const FVector BodyWorldScale =
            ComposePhysicsTransforms(OutBoneWorldTransforms[BoneIndex], BodySetup.BodyLocalFrame).Scale;
        FTransform       BoneWorld = OutBoneWorldTransforms[BoneIndex];
        if (!ComputeBoneWorldTransformFromBody(BodySetup, BodyWorldScale, BodyWorld, BoneWorld))
        {
            continue;
        }

        OutBoneWorldTransforms[BoneIndex] = BoneWorld;
        AppliedBodyBoneMask[BoneIndex] = 1;
        ++AppliedBodyCount;
    }

    if (AppliedBodyCount <= 0)
    {
        return false;
    }

    if (RagdollRootBoneIndex >= 0 &&
        RagdollRootBoneIndex < static_cast<int32>(OutBoneWorldTransforms.size()) &&
        AppliedBodyBoneMask[RagdollRootBoneIndex] != 0)
    {
        int32 ChildBoneIndex = RagdollRootBoneIndex;
        int32 ParentBoneIndex = MeshAsset->Bones[ChildBoneIndex].ParentIndex;
        while (ParentBoneIndex >= 0 &&
               ParentBoneIndex < static_cast<int32>(OutBoneWorldTransforms.size()) &&
               AppliedBodyBoneMask[ParentBoneIndex] == 0)
        {
            FTransform ParentWorld = ComputeParentWorldTransformFromChild(
                OutBoneWorldTransforms[ChildBoneIndex],
                CurrentBoneLocalTransforms[ChildBoneIndex]);

            // Reconstruct uncovered ancestors from the simulated ragdoll-root body so the
            // skeleton root follows ragdoll movement without applying a coarse world-space
            // translation delta that can make the whole mesh float above the floor.
            ParentWorld.Scale = OutBoneWorldTransforms[ParentBoneIndex].Scale;
            OutBoneWorldTransforms[ParentBoneIndex] = ParentWorld;

            ChildBoneIndex = ParentBoneIndex;
            ParentBoneIndex = MeshAsset->Bones[ChildBoneIndex].ParentIndex;
        }
    }

    // Only bones that have PhysicsAsset bodies are sampled directly from PhysX.
    // Descendant bones without bodies must keep their authored/current local offset
    // under the updated parent; otherwise they remain pinned in their pre-simulation
    // world position and vertices weighted to them look frozen outside the body shape.
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
    {
        if (AppliedBodyBoneMask[BoneIndex] != 0)
        {
            continue;
        }

        const int32 ParentIndex = MeshAsset->Bones[BoneIndex].ParentIndex;
        if (ParentIndex < 0 || ParentIndex >= static_cast<int32>(OutBoneWorldTransforms.size()))
        {
            continue;
        }

        OutBoneWorldTransforms[BoneIndex] = ComposePhysicsTransforms(
            OutBoneWorldTransforms[ParentIndex],
            CurrentBoneLocalTransforms[BoneIndex]);
    }

    return true;
}

UPhysicsAsset* FPhysicsAssetInstance::GetAsset() const
{
    return SourceAsset.Get();
}

USkeletalMeshComponent* FPhysicsAssetInstance::GetOwnerComponent() const
{
    return OwnerComponent.Get();
}

FPhysicsBodyHandle FPhysicsAssetInstance::GetBodyHandleByBoneName(const FName& BoneName) const
{
    const int32 BodyIndex = FindBodySetupIndexByBoneName(BoneName);
    if (BodyIndex < 0 || BodyIndex >= static_cast<int32>(BodiesByBone.size()))
    {
        return FPhysicsBodyHandle{};
    }

    return BodiesByBone[BodyIndex];
}

FTransform FPhysicsAssetInstance::GetBodyWorldTransformByBoneName(const FName& BoneName) const
{
    const USkeletalMeshComponent* Owner   = GetOwnerComponent();
    const IPhysicsRuntime*        Runtime = GetPhysicsRuntime(static_cast<const USkeletalMeshComponent*>(Owner));
    if (!Owner || !Runtime)
    {
        return FTransform();
    }

    std::shared_ptr<const FPhysicsWorldSnapshot> Snapshot     = Runtime->AcquireLatestSnapshotRef();
    const FPhysicsBodySnapshot*                  BodySnapshot = Snapshot
            ? Snapshot->FindRagdollBone(Owner->GetUUID(), BoneName)
            : nullptr;
    return BodySnapshot ? BodySnapshot->CurrentTransform : FTransform();
}

bool FPhysicsAssetInstance::FindNearestBodyToWorldLocation(
    const FVector& WorldLocation,
    FName& OutBoneName,
    FVector& OutBodyWorldLocation) const
{
    UPhysicsAsset* Asset = GetAsset();
    if (!Asset)
    {
        return false;
    }

    const TArray<FPhysicsAssetBodySetup>& BodySetups = Asset->GetBodySetups();
    float BestDistanceSquared = 0.0f;
    bool bFoundBody = false;

    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
    {
        if (BodyIndex >= static_cast<int32>(BodiesByBone.size()) || !BodiesByBone[BodyIndex].IsValid())
        {
            continue;
        }

        const FName& CandidateBoneName = BodySetups[BodyIndex].BoneName;
        const FVector CandidateLocation = GetBodyWorldTransformByBoneName(CandidateBoneName).Location;
        const float DistanceSquared = FVector::DistSquared(WorldLocation, CandidateLocation);
        if (!bFoundBody || DistanceSquared < BestDistanceSquared)
        {
            BestDistanceSquared = DistanceSquared;
            OutBoneName = CandidateBoneName;
            OutBodyWorldLocation = CandidateLocation;
            bFoundBody = true;
        }
    }

    return bFoundBody;
}

bool FPhysicsAssetInstance::AddImpulseToBody(FPhysicsBodyHandle BodyHandle, const FVector& Impulse) const
{
    if (!BodyHandle.IsValid())
    {
        return false;
    }

    IPhysicsRuntime* Runtime = GetPhysicsRuntime(GetOwnerComponent());
    if (!Runtime)
    {
        return false;
    }

    Runtime->AddImpulse(BodyHandle, Impulse);
    return true;
}

bool FPhysicsAssetInstance::AddImpulseToBone(const FName& BoneName, const FVector& Impulse) const
{
    return AddImpulseToBody(GetBodyHandleByBoneName(BoneName), Impulse);
}

bool FPhysicsAssetInstance::HasValidBodyForBone(const FName& BoneName) const
{
    return GetBodyHandleByBoneName(BoneName).IsValid();
}

FName FPhysicsAssetInstance::GetPrimarySimulatedBodyBoneName() const
{
    UPhysicsAsset* Asset = GetAsset();
    if (!Asset)
    {
        return FName::None;
    }

    const TArray<FPhysicsAssetBodySetup>& BodySetups = Asset->GetBodySetups();
    for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BodySetups.size()); ++BodyIndex)
    {
        if (BodyIndex >= static_cast<int32>(BodiesByBone.size()) || !BodiesByBone[BodyIndex].IsValid())
        {
            continue;
        }

        return BodySetups[BodyIndex].BoneName;
    }

    return FName::None;
}

FName FPhysicsAssetInstance::FindNearestSimulatedAncestorBodyBoneName(const FName& BoneName) const
{
    USkeletalMeshComponent* Owner = GetOwnerComponent();
    USkeletalMesh* Mesh = Owner ? Owner->GetSkeletalMesh() : nullptr;
    FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset || BoneName == FName::None)
    {
        return FName::None;
    }

    int32 BoneIndex = -1;
    const FString TargetBoneName = BoneName.ToString();
    for (int32 MeshBoneIndex = 0; MeshBoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++MeshBoneIndex)
    {
        if (MeshAsset->Bones[MeshBoneIndex].Name == TargetBoneName)
        {
            BoneIndex = MeshBoneIndex;
            break;
        }
    }

    while (BoneIndex >= 0 && BoneIndex < static_cast<int32>(MeshAsset->Bones.size()))
    {
        const FName CandidateBoneName(MeshAsset->Bones[BoneIndex].Name);
        if (HasValidBodyForBone(CandidateBoneName))
        {
            return CandidateBoneName;
        }

        BoneIndex = MeshAsset->Bones[BoneIndex].ParentIndex;
    }

    return FName::None;
}

FName FPhysicsAssetInstance::ResolveBestImpulseTargetBoneName(const FName& HitBoneName, const FName& FallbackRootBoneName) const
{
    if (HitBoneName != FName::None && HasValidBodyForBone(HitBoneName))
    {
        return HitBoneName;
    }

    const FName AncestorBoneName = FindNearestSimulatedAncestorBodyBoneName(HitBoneName);
    if (AncestorBoneName != FName::None)
    {
        return AncestorBoneName;
    }

    if (FallbackRootBoneName != FName::None && HasValidBodyForBone(FallbackRootBoneName))
    {
        return FallbackRootBoneName;
    }

    return FName::None;
}

int32 FPhysicsAssetInstance::FindBodySetupIndexByBoneName(const FName& BoneName) const
{
    UPhysicsAsset* Asset = GetAsset();
    return Asset ? Asset->FindBodySetupIndexByBoneName(BoneName) : -1;
}

int32 FPhysicsAssetInstance::FindBoneIndexForBody(const FName& BoneName) const
{
    auto It = BoneNameToIndex.find(BoneName.ToString());
    if (It != BoneNameToIndex.end())
    {
        return It->second;
    }

    return -1;
}
