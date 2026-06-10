#include "Physics/PhysXBodyBuilder.h"
#include "Physics/PhysXConversion.h"

#include <algorithm>
#include <cmath>
#include <PxPhysicsAPI.h>
#include <extensions/PxDefaultStreams.h>

using namespace physx;

namespace
{
    constexpr float MinPhysXGeometryExtent = 1.0e-4f;

    bool IsFiniteGeometryValue(float Value)
    {
        return std::isfinite(Value);
    }

    float ClampPhysXGeometryExtent(float Value)
    {
        return (std::max)(Value, MinPhysXGeometryExtent);
    }

    PxFilterData ToPxFilterData(const FPhysicsFilterData& In)
    {
        PxFilterData Out;
        uint32       PackedObjectAndFlags = In.ObjectType & PhysicsFilter_ObjectTypeMask;
        const EPhysicsGameplayOverlapOwnership EffectiveOverlapOwnership =
            In.GameplayOverlapOwnership != EPhysicsGameplayOverlapOwnership::None
                ? In.GameplayOverlapOwnership
                : GetDefaultGameplayOverlapOwnershipForRole(In.CollisionRole);

        if (In.CollisionEnabled == ECollisionEnabled::QueryOnly)
        {
            PackedObjectAndFlags |= PhysicsFilter_QueryOnly;
        }
        else if (In.CollisionEnabled == ECollisionEnabled::PhysicsOnly)
        {
            PackedObjectAndFlags |= PhysicsFilter_PhysicsOnly;
        }
        else if (In.CollisionEnabled == ECollisionEnabled::QueryAndPhysics)
        {
            PackedObjectAndFlags |= PhysicsFilter_QueryAndPhysics;
        }

        if (In.bIsTrigger)
        {
            PackedObjectAndFlags |= PhysicsFilter_IsTrigger;
        }
        if (In.bGenerateHitEvents)
        {
            PackedObjectAndFlags |= PhysicsFilter_GenerateHitEvents;
        }
        if (In.bGenerateOverlapEvents)
        {
            PackedObjectAndFlags |= PhysicsFilter_GenerateOverlapEvents;
        }
        if (In.bEnableCCD)
        {
            PackedObjectAndFlags |= PhysicsFilter_EnableCCD;
        }
        if (In.bIsComponentPrimitive)
        {
            PackedObjectAndFlags |= PhysicsFilter_ComponentPrimitive;
        }
        if (In.bIgnoreSameActor)
        {
            PackedObjectAndFlags |= PhysicsFilter_SameActorSelfIgnore;
        }
        if (In.bIsIndependentRagdoll)
        {
            PackedObjectAndFlags |= PhysicsFilter_IndependentRagdoll;
        }
        if (In.bIsPartialRagdoll)
        {
            PackedObjectAndFlags |= PhysicsFilter_PartialRagdoll;
        }
        if (In.bSuppressSameActorPrimitivePairs)
        {
            PackedObjectAndFlags |= PhysicsFilter_SuppressSameActorPrimitivePairs;
        }
        PackedObjectAndFlags |= PackPhysicsCollisionRole(In.CollisionRole);
        if (EffectiveOverlapOwnership == EPhysicsGameplayOverlapOwnership::PrimaryOwner)
        {
            PackedObjectAndFlags |= PhysicsFilter_OverlapOwnerPrimary;
        }
        else if (EffectiveOverlapOwnership == EPhysicsGameplayOverlapOwnership::QueryProxyOwner)
        {
            PackedObjectAndFlags |= PhysicsFilter_OverlapOwnerQueryProxy;
        }
        else if (EffectiveOverlapOwnership == EPhysicsGameplayOverlapOwnership::NonOwningReactionBody)
        {
            PackedObjectAndFlags |= PhysicsFilter_OverlapNonOwningReaction;
        }

        Out.word0 = PackedObjectAndFlags;
        Out.word1 = In.BlockMask;
        Out.word2 = In.OverlapMask;
        Out.word3 = In.IgnoreGroup;
        return Out;
    }
}

PxRigidActor* FPhysXBodyBuilder::CreateRigidActor(PxPhysics* Physics, const FBodyCreationDesc& Desc)
{
    if (!Physics)
    {
        return nullptr;
    }

    const PxTransform Pose = ToPxTransform(Desc.WorldTransform);

    if (Desc.BodyType == EPhysicsBodyType::Static)
    {
        return Physics->createRigidStatic(Pose);
    }

    PxRigidDynamic* Dynamic = Physics->createRigidDynamic(Pose);
    if (!Dynamic)
    {
        return nullptr;
    }

    if (Desc.BodyType == EPhysicsBodyType::Kinematic)
    {
        Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
    }

    ApplyBodyProperties(Dynamic, Desc);
    return Dynamic;
}

PxShape* FPhysXBodyBuilder::CreateShape(PxPhysics* Physics, PxMaterial* DefaultMaterial, const FPhysicsShapeDesc& Desc)
{
    if (!Physics || !DefaultMaterial)
    {
        return nullptr;
    }

    PxGeometryHolder Geometry;
    bool bHasGeometry = false;
    PxQuat ShapeAxisRotation(PxIdentity);
    PxTriangleMesh* TriangleMeshRef = nullptr;

    if (Desc.Type == EPhysicsShapeType::Box)
    {
        if (!IsFiniteGeometryValue(Desc.BoxHalfExtent.X) ||
            !IsFiniteGeometryValue(Desc.BoxHalfExtent.Y) ||
            !IsFiniteGeometryValue(Desc.BoxHalfExtent.Z))
        {
            return nullptr;
        }

        Geometry = PxBoxGeometry(
            ClampPhysXGeometryExtent(Desc.BoxHalfExtent.X),
            ClampPhysXGeometryExtent(Desc.BoxHalfExtent.Y),
            ClampPhysXGeometryExtent(Desc.BoxHalfExtent.Z)
        );
        bHasGeometry = true;
    }
    else if (Desc.Type == EPhysicsShapeType::Sphere)
    {
        if (!IsFiniteGeometryValue(Desc.SphereRadius))
        {
            return nullptr;
        }

        Geometry = PxSphereGeometry(ClampPhysXGeometryExtent(Desc.SphereRadius));
        bHasGeometry = true;
    }
    else if (Desc.Type == EPhysicsShapeType::Capsule)
    {
        if (!IsFiniteGeometryValue(Desc.CapsuleRadius) ||
            !IsFiniteGeometryValue(Desc.CapsuleHalfHeight))
        {
            return nullptr;
        }

        const float CapsuleRadius = ClampPhysXGeometryExtent(Desc.CapsuleRadius);
        const float PhysXHalfHeight = ClampPhysXGeometryExtent(Desc.CapsuleHalfHeight - CapsuleRadius);
        Geometry = PxCapsuleGeometry(CapsuleRadius, PhysXHalfHeight);

        ShapeAxisRotation = PxQuat(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f));
        bHasGeometry = true;
    }
    else if (Desc.Type == EPhysicsShapeType::TriangleMesh)
    {
        if (!Desc.TriangleMeshData ||
            Desc.TriangleMeshData->Vertices.size() < 3 ||
            Desc.TriangleMeshData->Indices.size() < 3 ||
            (Desc.TriangleMeshData->Indices.size() % 3) != 0)
        {
            return nullptr;
        }

        TArray<PxVec3> Points;
        Points.reserve(Desc.TriangleMeshData->Vertices.size());
        for (const FVector& Vertex : Desc.TriangleMeshData->Vertices)
        {
            if (!IsFiniteGeometryValue(Vertex.X) ||
                !IsFiniteGeometryValue(Vertex.Y) ||
                !IsFiniteGeometryValue(Vertex.Z))
            {
                return nullptr;
            }
            Points.push_back(PxVec3(Vertex.X, Vertex.Y, Vertex.Z));
        }

        PxTriangleMeshDesc MeshDesc;
        MeshDesc.points.count = static_cast<PxU32>(Points.size());
        MeshDesc.points.stride = sizeof(PxVec3);
        MeshDesc.points.data = Points.data();
        MeshDesc.triangles.count = static_cast<PxU32>(Desc.TriangleMeshData->Indices.size() / 3);
        MeshDesc.triangles.stride = sizeof(uint32) * 3;
        MeshDesc.triangles.data = Desc.TriangleMeshData->Indices.data();

        PxTolerancesScale Scale;
        PxCookingParams CookingParams(Scale);
        CookingParams.meshPreprocessParams |= PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;
        CookingParams.meshPreprocessParams |= PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;

        PxCooking* Cooking = PxCreateCooking(PX_PHYSICS_VERSION, PxGetFoundation(), CookingParams);
        if (!Cooking)
        {
            return nullptr;
        }

        PxDefaultMemoryOutputStream CookedData;
        PxTriangleMeshCookingResult::Enum CookingResult = PxTriangleMeshCookingResult::eFAILURE;
        const bool bCooked = Cooking->cookTriangleMesh(MeshDesc, CookedData, &CookingResult);
        Cooking->release();

        if (!bCooked || CookingResult == PxTriangleMeshCookingResult::eFAILURE)
        {
            return nullptr;
        }

        PxDefaultMemoryInputData Input(CookedData.getData(), CookedData.getSize());
        TriangleMeshRef = Physics->createTriangleMesh(Input);
        if (!TriangleMeshRef)
        {
            return nullptr;
        }

        Geometry = PxTriangleMeshGeometry(TriangleMeshRef);
        bHasGeometry = true;
    }

    if (!bHasGeometry)
    {
        return nullptr;
    }

    PxShape* Shape = Physics->createShape(Geometry.any(), *DefaultMaterial, true);
    if (TriangleMeshRef)
    {
        TriangleMeshRef->release();
        TriangleMeshRef = nullptr;
    }
    if (!Shape)
    {
        return nullptr;
    }

    PxTransform LocalPose = ToPxTransform(Desc.LocalTransform);
    LocalPose.q = LocalPose.q * ShapeAxisRotation;
    Shape->setLocalPose(LocalPose);

    ApplyShapeProperties(Shape, Desc);
    return Shape;
}

void FPhysXBodyBuilder::ApplyShapeProperties(PxShape* Shape, const FPhysicsShapeDesc& Desc)
{
    if (!Shape)
    {
        return;
    }

    const PxFilterData SimulationFilterData = ToPxFilterData(Desc.FilterData);
    PxFilterData       QueryFilterData      = SimulationFilterData;
    if (Desc.QueryIgnoreGroup != 0)
    {
        QueryFilterData.word3 = Desc.QueryIgnoreGroup;
    }

    Shape->setSimulationFilterData(SimulationFilterData);
    Shape->setQueryFilterData(QueryFilterData);

    if (Desc.CollisionEnabled == ECollisionEnabled::NoCollision)
    {
        Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
        Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
        Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
        return;
    }

    const bool bQueryEnabled =
        Desc.CollisionEnabled == ECollisionEnabled::QueryOnly ||
        Desc.CollisionEnabled == ECollisionEnabled::QueryAndPhysics;

    const bool bQueryOnlyNeedsSimulationForEvents =
            Desc.CollisionEnabled == ECollisionEnabled::QueryOnly &&
            !Desc.bIsTrigger &&
            ((Desc.FilterData.BlockMask != 0 && Desc.FilterData.bGenerateHitEvents) ||
                (Desc.FilterData.OverlapMask != 0 && Desc.FilterData.bGenerateOverlapEvents));

    Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, bQueryEnabled);

    if (Desc.bIsTrigger)
    {
        Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
        Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
    }
    else
    {
        Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
        Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE,
            Desc.CollisionEnabled == ECollisionEnabled::PhysicsOnly ||
            Desc.CollisionEnabled == ECollisionEnabled::QueryAndPhysics ||
            bQueryOnlyNeedsSimulationForEvents
        );
    }
}

void FPhysXBodyBuilder::ApplyBodyProperties(PxRigidActor* Actor, const FBodyCreationDesc& Desc)
{
    if (!Actor)
    {
        return;
    }

    if (PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>())
    {
        Dynamic->setLinearDamping(Desc.LinearDamping);
        Dynamic->setAngularDamping(Desc.AngularDamping);
        Dynamic->setMaxAngularVelocity(Desc.MaxAngularVelocity);
        Dynamic->setSolverIterationCounts(
            static_cast<PxU32>((std::max)(1, Desc.PositionSolverIterationCount)),
            static_cast<PxU32>((std::max)(1, Desc.VelocitySolverIterationCount))
        );
        Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, Desc.bEnableCCD);
        Dynamic->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !Desc.bEnableGravity);

        Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_X, Desc.bLockLinearX);
        Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_Y, Desc.bLockLinearY);
        Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_LINEAR_Z, Desc.bLockLinearZ);
        Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, Desc.bLockAngularX);
        Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, Desc.bLockAngularY);
        Dynamic->setRigidDynamicLockFlag(PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, Desc.bLockAngularZ);
    }
}

void FPhysXBodyBuilder::UpdateMassAndInertia(PxRigidActor* Actor, const FBodyCreationDesc& Desc)
{
    if (!Actor)
    {
        return;
    }

    PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>();
    if (!Dynamic)
    {
        return;
    }

    const float Mass = Desc.Mass > 0.0f ? Desc.Mass : 1.0f;
    PxVec3 LocalCOM = ToPxVec3(Desc.CenterOfMassLocalOffset);

    PxRigidBodyExt::setMassAndUpdateInertia(*Dynamic, Mass, &LocalCOM);
    Dynamic->setCMassLocalPose(PxTransform(LocalCOM));
}
