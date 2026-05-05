#include "Physics/JoltPhysicsSystem.h"

#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Collision/CylinderComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logger.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace
{
namespace ObjectLayers
{
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr JPH::ObjectLayer Count = 2;
} // namespace ObjectLayers

namespace BroadPhaseLayers
{
const JPH::BroadPhaseLayer NonMoving(0);
const JPH::BroadPhaseLayer Moving(1);
constexpr JPH::uint Count = 2;
} // namespace BroadPhaseLayers

class FBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::Count;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer InLayer) const override
    {
        switch (InLayer)
        {
        case ObjectLayers::Moving:
            return BroadPhaseLayers::Moving;
        case ObjectLayers::NonMoving:
        default:
            return BroadPhaseLayers::NonMoving;
        }
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer InLayer) const override
    {
        if (InLayer == BroadPhaseLayers::Moving)
        {
            return "Moving";
        }
        if (InLayer == BroadPhaseLayers::NonMoving)
        {
            return "NonMoving";
        }
        return "Invalid";
    }
#endif
};

class FObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer InLayer, JPH::BroadPhaseLayer InBroadPhaseLayer) const override
    {
        switch (InLayer)
        {
        case ObjectLayers::Moving:
            return true;
        case ObjectLayers::NonMoving:
            return InBroadPhaseLayer == BroadPhaseLayers::Moving;
        default:
            return false;
        }
    }
};

class FObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer InLayerA, JPH::ObjectLayer InLayerB) const override
    {
        if (InLayerA == ObjectLayers::NonMoving && InLayerB == ObjectLayers::NonMoving)
        {
            return false;
        }
        return true;
    }
};

JPH::Vec3 ToJoltVector(const FVector& Vector)
{
    return JPH::Vec3(Vector.X, Vector.Y, Vector.Z);
}

JPH::RVec3 ToJoltPosition(const FVector& Vector)
{
    return JPH::RVec3(Vector.X, Vector.Y, Vector.Z);
}

FVector ToEngineVector(const JPH::Vec3& Vector)
{
    return FVector(Vector.GetX(), Vector.GetY(), Vector.GetZ());
}

JPH::Quat ToJoltQuat(const FQuat& Quat)
{
    return JPH::Quat(Quat.X, Quat.Y, Quat.Z, Quat.W).Normalized();
}

JPH::Quat GetJoltCapsuleYAxisToZAxisRotation()
{
    return JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::DegreesToRadians(90.0f));
}

FQuat GetWorldQuat(const USceneComponent* Scene)
{
    return Scene != nullptr ? Scene->GetWorldTransform().GetRotation() : FQuat::Identity;
}

FQuat ToEngineQuat(const JPH::Quat& Quat)
{
    return FQuat(Quat.GetX(), Quat.GetY(), Quat.GetZ(), Quat.GetW()).GetNormalized();
}

float SafePositive(float Value, float Fallback)
{
    return Value > 0.001f ? Value : Fallback;
}

FVector AbsVector(const FVector& Vector)
{
    return FVector(std::fabs(Vector.X), std::fabs(Vector.Y), std::fabs(Vector.Z));
}

bool IsBetterShapeCastHit(const JPH::ShapeCastResult& Candidate, const JPH::ShapeCastResult& CurrentBest)
{
    if (Candidate.mFraction > 0.001f && CurrentBest.mFraction <= 0.001f)
    {
        return true;
    }

    if (Candidate.mFraction <= 0.001f && CurrentBest.mFraction > 0.001f)
    {
        return false;
    }

    if (Candidate.mFraction > 0.001f || CurrentBest.mFraction > 0.001f)
    {
        return Candidate.mFraction < CurrentBest.mFraction;
    }

    return Candidate.mPenetrationDepth > CurrentBest.mPenetrationDepth;
}

bool IsFloorLikeStartContact(const FVector& CurrentLocation, const FVector& RequestedDelta, const FVector& HitBodyLocation)
{
    const FVector ToBody = HitBodyLocation - CurrentLocation;
    const float VerticalSeparation = CurrentLocation.Z - HitBodyLocation.Z;
    const float HorizontalSeparation = std::sqrt(ToBody.X * ToBody.X + ToBody.Y * ToBody.Y);
    const bool bBodyClearlyBelow = VerticalSeparation > 0.05f && VerticalSeparation > HorizontalSeparation * 0.1f;
    return bBodyClearlyBelow && RequestedDelta.Z >= -0.001f;
}

bool ShouldIgnoreKinematicStartContact(const URigidBodyComponent* Body, const JPH::ShapeCastResult& Hit, const FVector& RequestedDelta)
{
    const bool bKinematicSweepBody = Body != nullptr && (Body->IsKinematicBody() || Body->IsHeldByPhysicsHandle());
    if (!bKinematicSweepBody)
    {
        return false;
    }

    const FVector PenetrationAxis = ToEngineVector(Hit.mPenetrationAxis).GetSafeNormal();
    if (PenetrationAxis.IsNearlyZero())
    {
        return true;
    }

    const FVector HorizontalNormal(PenetrationAxis.X, PenetrationAxis.Y, 0.0f);
    const FVector HorizontalDelta(RequestedDelta.X, RequestedDelta.Y, 0.0f);
    if (HorizontalNormal.IsNearlyZero() || HorizontalDelta.IsNearlyZero())
    {
        return true;
    }

    const float IntoA = FVector::DotProduct(HorizontalDelta.GetSafeNormal(), HorizontalNormal.GetSafeNormal());
    const float IntoB = FVector::DotProduct(HorizontalDelta.GetSafeNormal(), (HorizontalNormal * -1.0f).GetSafeNormal());
    return std::max(IntoA, IntoB) < 0.35f;
}

bool TryBuildKinematicSlideDelta(const URigidBodyComponent* Body, const JPH::ShapeCastResult& Hit, const FVector& RequestedDelta, FVector& OutSlideDelta)
{
    const bool bKinematicSweepBody = Body != nullptr && (Body->IsKinematicBody() || Body->IsHeldByPhysicsHandle());
    if (!bKinematicSweepBody)
    {
        return false;
    }

    const FVector RequestedHorizontal(RequestedDelta.X, RequestedDelta.Y, 0.0f);
    if (RequestedHorizontal.IsNearlyZero())
    {
        return false;
    }

    FVector Normal = (ToEngineVector(Hit.mPenetrationAxis) * -1.0f).GetSafeNormal();
    if (Normal.IsNearlyZero())
    {
        return false;
    }

    // Jolt의 penetration axis 부호는 shape cast 상황에 따라 뒤집힐 수 있으므로,
    // 요청 이동에 반대되는 쪽을 접촉 normal로 사용합니다.
    if (FVector::DotProduct(RequestedDelta, Normal) > 0.0f)
    {
        Normal *= -1.0f;
    }

    const float IntoSurfaceDistance = FVector::DotProduct(RequestedDelta, Normal);
    if (IntoSurfaceDistance >= 0.0f)
    {
        OutSlideDelta = RequestedDelta;
        return true;
    }

    OutSlideDelta = RequestedDelta - Normal * IntoSurfaceDistance;

    if (Body->IsHeldByPhysicsHandle())
    {
        return !OutSlideDelta.IsNearlyZero();
    }

    const bool bMostlyVerticalSurface = std::fabs(Normal.Z) > 0.65f;
    if (!bMostlyVerticalSurface)
    {
        return false;
    }

    OutSlideDelta.Z = 0.0f;
    return !OutSlideDelta.IsNearlyZero();
}

void GatherBlockingShapes(AActor* Actor, TArray<UShapeComponent*>& OutShapes)
{
    if (Actor == nullptr)
    {
        return;
    }

    for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
    {
        UShapeComponent* Shape = Cast<UShapeComponent>(Primitive);
        if (Shape != nullptr && Shape->IsBlockComponent())
        {
            OutShapes.push_back(Shape);
        }
    }
}

URigidBodyComponent* FindRigidBody(AActor* Actor)
{
    if (Actor == nullptr)
    {
        return nullptr;
    }

    for (UActorComponent* Component : Actor->GetComponents())
    {
        URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component);
        if (Body != nullptr)
        {
            return Body;
        }
    }

    return nullptr;
}

void ApplyImmediateBodyTransform(
    URigidBodyComponent* Body,
    JPH::BodyInterface& BodyInterface,
    JPH::BodyID BodyID,
    const FVector& Location,
    const FQuat& Rotation)
{
    BodyInterface.SetPositionAndRotation(
        BodyID,
        ToJoltPosition(Location),
        ToJoltQuat(Rotation),
        JPH::EActivation::Activate);
    BodyInterface.SetLinearVelocity(BodyID, JPH::Vec3::sZero());

    if (USceneComponent* UpdatedComponent = Body ? Body->GetUpdatedComponent() : nullptr)
    {
        UpdatedComponent->SetWorldLocation(Location);
        UpdatedComponent->SetRelativeRotationQuat(Rotation);
    }
}

JPH::Vec3 ToJoltLocalPosition(const FVector& Vector)
{
    return JPH::Vec3(Vector.X, Vector.Y, Vector.Z);
}

JPH::ShapeRefC CreateJoltShape(UPrimitiveComponent* Primitive)
{
    if (UBoxComponent* Box = Cast<UBoxComponent>(Primitive))
    {
        const FVector Extent = AbsVector(Box->GetBoxExtent());
        const FVector Scale = AbsVector(Box->GetWorldScale());
        const FVector ScaledExtent(
            Extent.X * SafePositive(Scale.X, 1.0f),
            Extent.Y * SafePositive(Scale.Y, 1.0f),
            Extent.Z * SafePositive(Scale.Z, 1.0f));
        const float MinExtent = std::min({ ScaledExtent.X, ScaledExtent.Y, ScaledExtent.Z });
        const float ConvexRadius = std::min(0.01f, std::max(0.0f, MinExtent * 0.25f));
        return new JPH::BoxShape(
            JPH::Vec3(
                SafePositive(ScaledExtent.X, 0.05f),
                SafePositive(ScaledExtent.Y, 0.05f),
                SafePositive(ScaledExtent.Z, 0.05f)),
            ConvexRadius);
    }

    if (USphereComponent* Sphere = Cast<USphereComponent>(Primitive))
    {
        const FVector Scale = AbsVector(Sphere->GetWorldScale());
        const float MaxScale = std::max({ Scale.X, Scale.Y, Scale.Z });
        return new JPH::SphereShape(SafePositive(std::fabs(Sphere->GetSphereRadius()) * SafePositive(MaxScale, 1.0f), 0.05f));
    }

    if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Primitive))
    {
        const FVector Scale = AbsVector(Capsule->GetWorldScale());
        const float RadiusScale = SafePositive(std::max(Scale.X, Scale.Y), 1.0f);
        const float HeightScale = SafePositive(Scale.Z, 1.0f);
        const float Radius = SafePositive(std::fabs(Capsule->GetCapsuleRadius()) * RadiusScale, 0.05f);
        const float HalfCylinderHeight = std::max(0.01f, std::fabs(Capsule->GetCapsuleHalfHeight()) * HeightScale - Radius);
        JPH::ShapeRefC CapsuleShape = new JPH::CapsuleShape(HalfCylinderHeight, Radius);
        return new JPH::RotatedTranslatedShape(
            JPH::Vec3::sZero(),
            GetJoltCapsuleYAxisToZAxisRotation(),
            CapsuleShape.GetPtr());
    }

    if (UCylinderComponent* Cylinder = Cast<UCylinderComponent>(Primitive))
    {
        const FVector Scale = AbsVector(Cylinder->GetWorldScale());
        const float RadiusScale = SafePositive(std::max(Scale.X, Scale.Y), 1.0f);
        const float HeightScale = SafePositive(Scale.Z, 1.0f);
        const float Radius = SafePositive(std::fabs(Cylinder->GetCylinderRadius()) * RadiusScale, 0.05f);
        const float HalfHeight = SafePositive(std::fabs(Cylinder->GetCylinderHalfHeight()) * HeightScale, 0.01f);
        const float ConvexRadius = std::min(0.01f, Radius * 0.1f);
        JPH::ShapeRefC CylinderShape = new JPH::CylinderShape(HalfHeight, Radius, ConvexRadius);
        return new JPH::RotatedTranslatedShape(
            JPH::Vec3::sZero(),
            GetJoltCapsuleYAxisToZAxisRotation(),
            CylinderShape.GetPtr());
    }

    return nullptr;
}

UCapsuleComponent* FindBlockingCapsule(AActor* Actor)
{
    if (Actor == nullptr)
    {
        return nullptr;
    }

    for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
    {
        UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Primitive);
        if (Capsule != nullptr && Capsule->IsBlockComponent())
        {
            return Capsule;
        }
    }

    return nullptr;
}

JPH::ShapeRefC CreateShapeFromSettings(const JPH::ShapeSettings& Settings)
{
    JPH::ShapeSettings::ShapeResult Result = Settings.Create();
    if (!Result.IsValid())
    {
        if (Result.HasError())
        {
            UE_LOG("[Jolt] Failed to create shape: %s", Result.GetError().c_str());
        }
        return nullptr;
    }

    return Result.Get();
}

void GetLocalShapeTransform(const USceneComponent* ShapeComponent, const USceneComponent* BodyComponent, FVector& OutPosition, FQuat& OutRotation)
{
    if (ShapeComponent == nullptr || BodyComponent == nullptr)
    {
        OutPosition = FVector::ZeroVector;
        OutRotation = FQuat::Identity;
        return;
    }

    const FTransform BodyTransform = BodyComponent->GetWorldTransform();
    const FTransform ShapeTransform = ShapeComponent->GetWorldTransform();
    OutPosition = BodyTransform.InverseTransformPositionNoScale(ShapeTransform.GetTranslation());
    OutRotation = (ShapeTransform.GetRotation() * BodyTransform.GetRotation().Inverse()).GetNormalized();
}

JPH::ShapeRefC CreateJoltBodyShape(AActor* Actor, const USceneComponent* BodyComponent)
{
    TArray<UShapeComponent*> BlockingShapes;
    GatherBlockingShapes(Actor, BlockingShapes);
    if (BlockingShapes.empty() || BodyComponent == nullptr)
    {
        return nullptr;
    }

    if (BlockingShapes.size() == 1)
    {
        UShapeComponent* ShapeComponent = BlockingShapes[0];
        JPH::ShapeRefC LeafShape = CreateJoltShape(ShapeComponent);
        if (LeafShape == nullptr)
        {
            return nullptr;
        }

        FVector LocalPosition;
        FQuat LocalRotation;
        GetLocalShapeTransform(ShapeComponent, BodyComponent, LocalPosition, LocalRotation);
        if (LocalPosition.IsNearlyZero(0.001f) && LocalRotation.IsIdentity(0.001f))
        {
            return LeafShape;
        }

        JPH::RotatedTranslatedShapeSettings OffsetSettings(
            ToJoltLocalPosition(LocalPosition),
            ToJoltQuat(LocalRotation),
            LeafShape.GetPtr());
        return CreateShapeFromSettings(OffsetSettings);
    }

    JPH::StaticCompoundShapeSettings CompoundSettings;
    for (UShapeComponent* ShapeComponent : BlockingShapes)
    {
        JPH::ShapeRefC LeafShape = CreateJoltShape(ShapeComponent);
        if (LeafShape == nullptr)
        {
            continue;
        }

        FVector LocalPosition;
        FQuat LocalRotation;
        GetLocalShapeTransform(ShapeComponent, BodyComponent, LocalPosition, LocalRotation);
        CompoundSettings.AddShape(
            ToJoltLocalPosition(LocalPosition),
            ToJoltQuat(LocalRotation),
            LeafShape.GetPtr());
    }

    return CreateShapeFromSettings(CompoundSettings);
}

JPH::ShapeRefC CreateJoltCharacterShape(URigidBodyComponent* Body)
{
    if (Body == nullptr || Body->GetOwner() == nullptr || Body->GetUpdatedComponent() == nullptr)
    {
        return nullptr;
    }

    UCapsuleComponent* Capsule = FindBlockingCapsule(Body->GetOwner());
    if (Capsule == nullptr)
    {
        return CreateJoltBodyShape(Body->GetOwner(), Body->GetUpdatedComponent());
    }

    const FVector Scale = AbsVector(Capsule->GetWorldScale());
    const float RadiusScale = SafePositive(std::max(Scale.X, Scale.Y), 1.0f);
    const float HeightScale = SafePositive(Scale.Z, 1.0f);
    const float Radius = SafePositive(std::fabs(Capsule->GetCapsuleRadius()) * RadiusScale, 0.05f);
    const float HalfHeight = SafePositive(std::fabs(Capsule->GetCapsuleHalfHeight()) * HeightScale, Radius);
    const float HalfCylinderHeight = std::max(0.01f, HalfHeight - Radius);

    JPH::ShapeRefC CapsuleShape = new JPH::CapsuleShape(HalfCylinderHeight, Radius);
    FVector LocalPosition;
    FQuat LocalRotation;
    GetLocalShapeTransform(Capsule, Body->GetUpdatedComponent(), LocalPosition, LocalRotation);

    JPH::RotatedTranslatedShapeSettings OffsetSettings(
        ToJoltLocalPosition(LocalPosition),
        (ToJoltQuat(LocalRotation) * GetJoltCapsuleYAxisToZAxisRotation()).Normalized(),
        CapsuleShape.GetPtr());
    return CreateShapeFromSettings(OffsetSettings);
}

JPH::BodyID MakeBodyID(uint32 RawBodyID)
{
    return JPH::BodyID(RawBodyID);
}

void JoltTrace(const char* Format, ...)
{
    if (Format == nullptr)
    {
        return;
    }

    char Buffer[2048] = {};
    va_list Args;
    va_start(Args, Format);
    vsnprintf(Buffer, sizeof(Buffer), Format, Args);
    va_end(Args);

    UE_LOG("[Jolt] %s", Buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertFailed(const char* Expression, const char* Message, const char* File, JPH::uint Line)
{
    UE_LOG("[Jolt Assert] %s | %s (%s:%u)",
           Expression != nullptr ? Expression : "",
           Message != nullptr ? Message : "",
           File != nullptr ? File : "",
           static_cast<unsigned int>(Line));
    return false;
}
#endif
} // namespace

struct FJoltPhysicsSystem::FImpl
{
    JPH::PhysicsSystem PhysicsSystem;
    FBroadPhaseLayerInterface BroadPhaseLayerInterface;
    FObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseLayerFilter;
    FObjectLayerPairFilter ObjectLayerPairFilter;

    JPH::TempAllocatorImpl* TempAllocator = nullptr;
    JPH::JobSystemThreadPool* JobSystem = nullptr;

    std::vector<JPH::BodyID> BodyIDs;
    std::unordered_map<URigidBodyComponent*, JPH::BodyID> RigidBodies;
    std::unordered_map<URigidBodyComponent*, JPH::BodyID> DynamicBodies;
    std::unordered_map<URigidBodyComponent*, JPH::Ref<JPH::CharacterVirtual>> Characters;
    std::unordered_map<URigidBodyComponent*, URigidBodyComponent*> HeldBodyOwners;
    std::unordered_set<URigidBodyComponent*> CollisionSuppressedHeldBodies;
};

FJoltPhysicsSystem& FJoltPhysicsSystem::Get()
{
    static FJoltPhysicsSystem Instance;
    return Instance;
}

FJoltPhysicsSystem::~FJoltPhysicsSystem()
{
    Shutdown();
}

bool FJoltPhysicsSystem::Initialize()
{
    if (bInitialized)
    {
        return true;
    }

    JPH::Trace = JoltTrace;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = JoltAssertFailed;
#endif

    JPH::RegisterDefaultAllocator();
    if (JPH::Factory::sInstance == nullptr)
    {
        JPH::Factory::sInstance = new JPH::Factory();
    }
    JPH::RegisterTypes();

    Impl = new FImpl();
    Impl->TempAllocator = new JPH::TempAllocatorImpl(16 * 1024 * 1024);
    const int ThreadCount = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    Impl->JobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, ThreadCount);

    constexpr JPH::uint MaxBodies = 4096;
    constexpr JPH::uint NumBodyMutexes = 0;
    constexpr JPH::uint MaxBodyPairs = 8192;
    constexpr JPH::uint MaxContactConstraints = 8192;
    Impl->PhysicsSystem.Init(
        MaxBodies,
        NumBodyMutexes,
        MaxBodyPairs,
        MaxContactConstraints,
        Impl->BroadPhaseLayerInterface,
        Impl->ObjectVsBroadPhaseLayerFilter,
        Impl->ObjectLayerPairFilter);
    JPH::PhysicsSettings PhysicsSettings = Impl->PhysicsSystem.GetPhysicsSettings();
    PhysicsSettings.mPenetrationSlop = 0.003f;
    PhysicsSettings.mSpeculativeContactDistance = 0.04f;
    PhysicsSettings.mLinearCastThreshold = 0.35f;
    PhysicsSettings.mLinearCastMaxPenetration = 0.05f;
    PhysicsSettings.mNumVelocitySteps = 12;
    PhysicsSettings.mNumPositionSteps = 8;
    Impl->PhysicsSystem.SetPhysicsSettings(PhysicsSettings);
    Impl->PhysicsSystem.SetGravity(JPH::Vec3(0.0f, 0.0f, -9.8f));

    bInitialized = true;
    UE_LOG("JoltPhysicsSystem: initialized.");
    return true;
}

void FJoltPhysicsSystem::Shutdown()
{
    if (!bInitialized)
    {
        return;
    }

    ClearWorld();

    delete Impl->JobSystem;
    Impl->JobSystem = nullptr;
    delete Impl->TempAllocator;
    Impl->TempAllocator = nullptr;
    delete Impl;
    Impl = nullptr;

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    CurrentWorld = nullptr;
    bInitialized = false;
    UE_LOG("JoltPhysicsSystem: shutdown.");
}

bool FJoltPhysicsSystem::IsBodyManaged(const URigidBodyComponent* Body) const
{
    if (!bInitialized || Body == nullptr)
    {
        return false;
    }

    return Impl->RigidBodies.find(const_cast<URigidBodyComponent*>(Body)) != Impl->RigidBodies.end();
}

void FJoltPhysicsSystem::ClearWorld()
{
    if (Impl == nullptr)
    {
        return;
    }

    JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
    for (JPH::BodyID BodyID : Impl->BodyIDs)
    {
        if (!BodyID.IsInvalid())
        {
            BodyInterface.RemoveBody(BodyID);
            BodyInterface.DestroyBody(BodyID);
        }
    }

    for (auto& Pair : Impl->RigidBodies)
    {
        if (Pair.first != nullptr)
        {
            Pair.first->ClearJoltBodyHandle();
        }
    }

    Impl->BodyIDs.clear();
    Impl->RigidBodies.clear();
    Impl->DynamicBodies.clear();
    Impl->Characters.clear();
    Impl->HeldBodyOwners.clear();
    Impl->CollisionSuppressedHeldBodies.clear();
}

void FJoltPhysicsSystem::RebuildWorld(UWorld* World)
{
    if (World == nullptr || !Initialize())
    {
        return;
    }

    ClearWorld();
    CurrentWorld = World;

    for (AActor* Actor : World->GetActors())
    {
        if (Actor == nullptr || !Actor->IsActive())
        {
            continue;
        }

        URigidBodyComponent* Body = FindRigidBody(Actor);
        TArray<UShapeComponent*> BlockingShapes;
        GatherBlockingShapes(Actor, BlockingShapes);
        if (Body != nullptr && Body->IsSimulatingPhysics() && !BlockingShapes.empty())
        {
            RegisterDynamicBody(Body);
            continue;
        }

        RegisterStaticActor(Actor);
    }

    Impl->PhysicsSystem.OptimizeBroadPhase();
}

void FJoltPhysicsSystem::RegisterStaticActor(AActor* Actor)
{
    if (Impl == nullptr || Actor == nullptr)
    {
        return;
    }

    TArray<UShapeComponent*> BlockingShapes;
    GatherBlockingShapes(Actor, BlockingShapes);
    if (BlockingShapes.empty())
    {
        return;
    }

    if (BlockingShapes.size() == 1)
    {
        RegisterStaticBody(BlockingShapes[0]);
        return;
    }

    USceneComponent* Root = Actor->GetRootComponent();
    if (Root == nullptr)
    {
        return;
    }

    JPH::ShapeRefC Shape = CreateJoltBodyShape(Actor, Root);
    if (Shape == nullptr)
    {
        return;
    }

    JPH::BodyCreationSettings Settings(
        Shape,
        ToJoltPosition(Root->GetWorldLocation()),
        ToJoltQuat(GetWorldQuat(Root)),
        JPH::EMotionType::Static,
        ObjectLayers::NonMoving);
    Settings.mFriction = 0.8f;
    Settings.mRestitution = 0.05f;
    Settings.mEnhancedInternalEdgeRemoval = true;

    JPH::BodyID BodyID = Impl->PhysicsSystem.GetBodyInterface().CreateAndAddBody(Settings, JPH::EActivation::DontActivate);
    if (!BodyID.IsInvalid())
    {
        Impl->BodyIDs.push_back(BodyID);
    }
}

void FJoltPhysicsSystem::RegisterStaticBody(UPrimitiveComponent* ShapeComponent)
{
    if (Impl == nullptr || ShapeComponent == nullptr)
    {
        return;
    }

    JPH::ShapeRefC Shape = CreateJoltShape(ShapeComponent);
    if (Shape == nullptr)
    {
        return;
    }

    JPH::BodyCreationSettings Settings(
        Shape,
        ToJoltPosition(ShapeComponent->GetWorldLocation()),
        ToJoltQuat(GetWorldQuat(ShapeComponent)),
        JPH::EMotionType::Static,
        ObjectLayers::NonMoving);
    Settings.mFriction = 0.8f;
    Settings.mRestitution = 0.05f;
    Settings.mEnhancedInternalEdgeRemoval = true;

    JPH::BodyID BodyID = Impl->PhysicsSystem.GetBodyInterface().CreateAndAddBody(Settings, JPH::EActivation::DontActivate);
    if (!BodyID.IsInvalid())
    {
        Impl->BodyIDs.push_back(BodyID);
    }
}

void FJoltPhysicsSystem::RegisterDynamicBody(URigidBodyComponent* Body)
{
    if (Impl == nullptr || Body == nullptr || Body->GetOwner() == nullptr)
    {
        return;
    }

    USceneComponent* UpdatedComponent = Body->GetUpdatedComponent();
    if (UpdatedComponent == nullptr)
    {
        return;
    }

    JPH::ShapeRefC Shape = CreateJoltBodyShape(Body->GetOwner(), UpdatedComponent);
    if (Shape == nullptr)
    {
        return;
    }

    const bool bStaticBody = Body->IsStaticBody();
    const bool bKinematicBody = Body->IsKinematicBody() || Body->IsHeldByPhysicsHandle();
    const JPH::EMotionType MotionType = bStaticBody ? JPH::EMotionType::Static : (bKinematicBody ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic);
    const JPH::ObjectLayer ObjectLayer = bStaticBody ? ObjectLayers::NonMoving : ObjectLayers::Moving;
    const JPH::EActivation Activation = bStaticBody ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;

    JPH::BodyCreationSettings Settings(
        Shape,
        ToJoltPosition(UpdatedComponent->GetWorldLocation()),
        ToJoltQuat(GetWorldQuat(UpdatedComponent)),
        MotionType,
        ObjectLayer);
    Settings.mFriction = 0.85f;
    Settings.mRestitution = 0.05f;
    Settings.mEnhancedInternalEdgeRemoval = true;
    Settings.mLinearDamping = Body->GetLinearDamping();
    Settings.mAngularDamping = Body->GetAngularDamping();
    Settings.mMaxLinearVelocity = Body->GetMaxSpeed() > 0.0f ? Body->GetMaxSpeed() : 500.0f;
    Settings.mMaxAngularVelocity = Body->GetMaxAngularSpeed() > 0.0f ? Body->GetMaxAngularSpeed() * (3.1415926535f / 180.0f) : 60.0f;
    Settings.mGravityFactor = Body->IsGravityEnabled() ? Body->GetGravityScale() : 0.0f;
    if (!bStaticBody)
    {
        Settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        Settings.mUseManifoldReduction = false;
        Settings.mNumVelocityStepsOverride = 12;
        Settings.mNumPositionStepsOverride = 8;
        Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        Settings.mMassPropertiesOverride.mMass = Body->GetMass();
    }

    JPH::BodyID BodyID = Impl->PhysicsSystem.GetBodyInterface().CreateAndAddBody(Settings, Activation);
    if (BodyID.IsInvalid())
    {
        return;
    }

    Body->SetJoltBodyHandle(BodyID.GetIndexAndSequenceNumber());
    Impl->BodyIDs.push_back(BodyID);
    Impl->RigidBodies[Body] = BodyID;
    if (!bStaticBody)
    {
        Impl->DynamicBodies[Body] = BodyID;
    }
}

void FJoltPhysicsSystem::Step(UWorld* World, float DeltaTime)
{
    if (!bInitialized || Impl == nullptr || World == nullptr || World != CurrentWorld || DeltaTime <= 0.0f)
    {
        return;
    }

    const float ClampedDeltaTime = std::min(DeltaTime, 1.0f / 15.0f);
    const int CollisionSteps = std::max(1, static_cast<int>(std::ceil(ClampedDeltaTime / (1.0f / 60.0f))));
    Impl->PhysicsSystem.Update(ClampedDeltaTime, CollisionSteps, Impl->TempAllocator, Impl->JobSystem);

    JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
    for (auto& Pair : Impl->DynamicBodies)
    {
        URigidBodyComponent* Body = Pair.first;
        if (Body == nullptr)
        {
            continue;
        }

        USceneComponent* UpdatedComponent = Body->GetUpdatedComponent();
        if (UpdatedComponent == nullptr)
        {
            continue;
        }

        JPH::RVec3 Position;
        JPH::Quat Rotation;
        BodyInterface.GetPositionAndRotation(Pair.second, Position, Rotation);
        UpdatedComponent->SetWorldLocation(ToEngineVector(Position));
        UpdatedComponent->SetRelativeRotationQuat(ToEngineQuat(Rotation));

        JPH::Vec3 LinearVelocity;
        JPH::Vec3 AngularVelocity;
        BodyInterface.GetLinearAndAngularVelocity(Pair.second, LinearVelocity, AngularVelocity);
        Body->SetVelocity(ToEngineVector(LinearVelocity));
        Body->SetAngularVelocity(ToEngineVector(AngularVelocity));
    }
}

void FJoltPhysicsSystem::SetBodyKinematic(URigidBodyComponent* Body)
{
    if (Impl == nullptr || Body == nullptr || Impl->DynamicBodies.find(Body) == Impl->DynamicBodies.end())
    {
        return;
    }

    JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
    BodyInterface.SetMotionType(Impl->DynamicBodies[Body], JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
    BodyInterface.SetLinearVelocity(Impl->DynamicBodies[Body], JPH::Vec3::sZero());
}

void FJoltPhysicsSystem::SetBodyDynamic(URigidBodyComponent* Body)
{
    if (Impl == nullptr || Body == nullptr || Impl->DynamicBodies.find(Body) == Impl->DynamicBodies.end() || !Body->IsDynamicBody())
    {
        return;
    }

    JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
    BodyInterface.SetMotionType(Impl->DynamicBodies[Body], JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
}

void FJoltPhysicsSystem::SetBodyTransformFromComponent(URigidBodyComponent* Body)
{
    if (Impl == nullptr || Body == nullptr || Impl->DynamicBodies.find(Body) == Impl->DynamicBodies.end())
    {
        return;
    }

    USceneComponent* UpdatedComponent = Body->GetUpdatedComponent();
    if (UpdatedComponent == nullptr)
    {
        return;
    }

    Impl->PhysicsSystem.GetBodyInterface().SetPositionAndRotation(
        Impl->DynamicBodies[Body],
        ToJoltPosition(UpdatedComponent->GetWorldLocation()),
        ToJoltQuat(GetWorldQuat(UpdatedComponent)),
        JPH::EActivation::Activate);
}

void FJoltPhysicsSystem::SetHeldBodyOwner(URigidBodyComponent* HeldBody, URigidBodyComponent* HolderBody)
{
    if (Impl == nullptr || HeldBody == nullptr)
    {
        return;
    }

    if (HolderBody == nullptr)
    {
        Impl->HeldBodyOwners.erase(HeldBody);
        return;
    }

    Impl->HeldBodyOwners[HeldBody] = HolderBody;
}

void FJoltPhysicsSystem::ClearHeldBodyOwner(URigidBodyComponent* HeldBody)
{
    if (Impl == nullptr || HeldBody == nullptr)
    {
        return;
    }

    Impl->HeldBodyOwners.erase(HeldBody);
    Impl->CollisionSuppressedHeldBodies.erase(HeldBody);
}

void FJoltPhysicsSystem::SetHeldBodyCollisionSuppressed(URigidBodyComponent* HeldBody, bool bSuppressed)
{
    if (Impl == nullptr || HeldBody == nullptr)
    {
        return;
    }

    if (bSuppressed)
    {
        Impl->CollisionSuppressedHeldBodies.insert(HeldBody);
    }
    else
    {
        Impl->CollisionSuppressedHeldBodies.erase(HeldBody);
    }
}

bool FJoltPhysicsSystem::CheckKinematicGround(URigidBodyComponent* Body, float ProbeDistance, float& OutGroundDistance)
{
    OutGroundDistance = 0.0f;
    if (Impl == nullptr || Body == nullptr || !Body->IsKinematicBody() || Body->IsHeldByPhysicsHandle() ||
        Impl->DynamicBodies.find(Body) == Impl->DynamicBodies.end() || ProbeDistance <= 0.0f)
    {
        return false;
    }

    const JPH::BodyID BodyID = Impl->DynamicBodies[Body];
    JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
    JPH::RefConst<JPH::Shape> Shape = BodyInterface.GetShape(BodyID);
    if (Shape == nullptr)
    {
        return false;
    }

    const FVector CurrentLocation = Body->GetPhysicsLocation();
    const FVector ProbeDelta(0.0f, 0.0f, -ProbeDistance);
    const FQuat Rotation = Body->GetUpdatedComponent()
                               ? Body->GetUpdatedComponent()->GetWorldTransform().GetRotation()
                               : FQuat::Identity;

    const JPH::RMat44 StartTransform = JPH::RMat44::sRotationTranslation(
        ToJoltQuat(Rotation),
        ToJoltPosition(CurrentLocation));
    const JPH::RShapeCast ShapeCast = JPH::RShapeCast::sFromWorldTransform(
        Shape,
        JPH::Vec3::sReplicate(1.0f),
        StartTransform,
        ToJoltVector(ProbeDelta));

    JPH::ShapeCastSettings Settings;
    Settings.mReturnDeepestPoint = true;
    JPH::IgnoreSingleBodyFilter BodyFilter(BodyID);
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> Collector;
    Impl->PhysicsSystem.GetNarrowPhaseQuery().CastShape(
        ShapeCast,
        Settings,
        ToJoltPosition(CurrentLocation),
        Collector,
        {},
        {},
        BodyFilter);

    if (!Collector.HadHit() || Collector.mHit.mFraction > 1.0f)
    {
        return false;
    }

    OutGroundDistance = std::max(0.0f, Collector.mHit.mFraction * ProbeDistance);
    return true;
}

bool FJoltPhysicsSystem::MoveKinematicBody(URigidBodyComponent* Body, FVector& InOutTargetLocation, const FQuat& TargetRotation, float DeltaTime)
{
    if (Impl == nullptr || Body == nullptr || Impl->DynamicBodies.find(Body) == Impl->DynamicBodies.end() || Body->IsStaticBody())
    {
        return false;
    }

    const JPH::BodyID BodyID = Impl->DynamicBodies[Body];
    JPH::BodyInterface& BodyInterface = Impl->PhysicsSystem.GetBodyInterface();
    const FVector CurrentLocation = Body->GetPhysicsLocation();
    const FVector RequestedDelta = InOutTargetLocation - CurrentLocation;
    const bool bSuppressHeldCollision = Impl->CollisionSuppressedHeldBodies.find(Body) != Impl->CollisionSuppressedHeldBodies.end();
    if (!bSuppressHeldCollision && !RequestedDelta.IsNearlyZero())
    {
        JPH::RefConst<JPH::Shape> Shape = BodyInterface.GetShape(BodyID);
        if (Shape != nullptr)
        {
            const JPH::RMat44 StartTransform = JPH::RMat44::sRotationTranslation(
                ToJoltQuat(TargetRotation),
                ToJoltPosition(CurrentLocation));
            const JPH::RShapeCast ShapeCast = JPH::RShapeCast::sFromWorldTransform(
                Shape,
                JPH::Vec3::sReplicate(1.0f),
                StartTransform,
                ToJoltVector(RequestedDelta));
            JPH::ShapeCastSettings Settings;
            Settings.mReturnDeepestPoint = true;
            JPH::IgnoreMultipleBodiesFilter BodyFilter;
            BodyFilter.Reserve(2);
            BodyFilter.IgnoreBody(BodyID);
            auto OwnerIt = Impl->HeldBodyOwners.find(Body);
            if (OwnerIt != Impl->HeldBodyOwners.end())
            {
                auto OwnerBodyIt = Impl->DynamicBodies.find(OwnerIt->second);
                if (OwnerBodyIt != Impl->DynamicBodies.end())
                {
                    BodyFilter.IgnoreBody(OwnerBodyIt->second);
                }
            }
            JPH::AllHitCollisionCollector<JPH::CastShapeCollector> Collector;
            Impl->PhysicsSystem.GetNarrowPhaseQuery().CastShape(
                ShapeCast,
                Settings,
                ToJoltPosition(CurrentLocation),
                Collector,
                {},
                {},
                BodyFilter);

            bool bHasBlockingHit = false;
            JPH::ShapeCastResult BlockingHit;
            if (Collector.HadHit())
            {
                for (const JPH::ShapeCastResult& Hit : Collector.mHits)
                {
                    if (Hit.mFraction >= 1.0f)
                    {
                        continue;
                    }

                    if (Hit.mFraction <= 0.001f)
                    {
                        const JPH::BodyID HitBodyID = Hit.mBodyID2;
                        if (!HitBodyID.IsInvalid())
                        {
                            const FVector HitBodyLocation = ToEngineVector(BodyInterface.GetPosition(HitBodyID));
                            if (IsFloorLikeStartContact(CurrentLocation, RequestedDelta, HitBodyLocation))
                            {
                                continue;
                            }
                        }

                        if (ShouldIgnoreKinematicStartContact(Body, Hit, RequestedDelta))
                        {
                            continue;
                        }

                        const FVector ContactNormal = (ToEngineVector(Hit.mPenetrationAxis) * -1.0f).GetSafeNormal();
                        const float IntoSurfaceDistance = FVector::DotProduct(RequestedDelta, ContactNormal);
                        if (ContactNormal.IsNearlyZero() || IntoSurfaceDistance >= 0.0f)
                        {
                            continue;
                        }
                    }

                    if (!bHasBlockingHit || IsBetterShapeCastHit(Hit, BlockingHit))
                    {
                        BlockingHit = Hit;
                        bHasBlockingHit = true;
                    }
                }
            }

            if (bHasBlockingHit)
            {
                const JPH::Vec3 LocalExtent = Shape->GetLocalBounds().GetExtent();
                const float SmallestExtent = std::max(0.01f, LocalExtent.ReduceMin());
                const float RequestedDistance = RequestedDelta.Size();
                float SafetyDistance = std::clamp(SmallestExtent * 0.35f, 0.02f, 0.12f);
                if (Body->IsHeldByPhysicsHandle())
                {
                    SafetyDistance = std::clamp(SmallestExtent * 0.08f, 0.005f, 0.03f);
                    if (RequestedDistance > 0.001f)
                    {
                        SafetyDistance = std::min(SafetyDistance, RequestedDistance * 0.1f);
                    }
                }
                if (Body->IsKinematicBody() && RequestedDistance > 0.001f)
                {
                    SafetyDistance = std::min(SafetyDistance, RequestedDistance * 0.2f);
                }
                const float SafetyFraction = RequestedDistance > 0.001f ? SafetyDistance / RequestedDistance : 1.0f;
                if (BlockingHit.mFraction <= 0.001f)
                {
                    const FVector ContactNormal = (ToEngineVector(BlockingHit.mPenetrationAxis) * -1.0f).GetSafeNormal();
                    const float IntoSurfaceDistance = FVector::DotProduct(RequestedDelta, ContactNormal);
                    if (!ContactNormal.IsNearlyZero() && IntoSurfaceDistance < 0.0f)
                    {
                        const FVector SlideDelta = RequestedDelta - ContactNormal * IntoSurfaceDistance;
                        InOutTargetLocation = CurrentLocation + SlideDelta;
                    }
                }
                else if (FVector SlideDelta; TryBuildKinematicSlideDelta(Body, BlockingHit, RequestedDelta, SlideDelta))
                {
                    InOutTargetLocation = CurrentLocation + SlideDelta;
                }
                else
                {
                    const float AllowedFraction = std::max(0.0f, BlockingHit.mFraction - SafetyFraction);
                    InOutTargetLocation = CurrentLocation + RequestedDelta * AllowedFraction;
                }
            }
        }
    }

    if (Body->IsHeldByPhysicsHandle())
    {
        ApplyImmediateBodyTransform(Body, BodyInterface, BodyID, InOutTargetLocation, TargetRotation);
        return true;
    }

    const float MoveDeltaTime = std::max(DeltaTime, 1.0f / 240.0f);
    BodyInterface.MoveKinematic(
        BodyID,
        ToJoltPosition(InOutTargetLocation),
        ToJoltQuat(TargetRotation),
        MoveDeltaTime);
    return true;
}

bool FJoltPhysicsSystem::MoveCharacter(URigidBodyComponent* Body, const FVector& DesiredVelocity, float DeltaTime, float GroundStickDistance, FVector& OutLocation, FVector& OutVelocity, bool& bOutGrounded)
{
    OutLocation = Body != nullptr ? Body->GetPhysicsLocation() : FVector::ZeroVector;
    OutVelocity = DesiredVelocity;
    bOutGrounded = false;

    if (Impl == nullptr || Body == nullptr || Body->GetUpdatedComponent() == nullptr || DeltaTime <= 0.0f)
    {
        return false;
    }

    JPH::Ref<JPH::CharacterVirtual>& Character = Impl->Characters[Body];
    if (Character == nullptr)
    {
        JPH::ShapeRefC Shape = CreateJoltCharacterShape(Body);
        if (Shape == nullptr)
        {
            Impl->Characters.erase(Body);
            return false;
        }

        JPH::CharacterVirtualSettings Settings;
        Settings.mShape = Shape;
        Settings.mUp = JPH::Vec3::sAxisZ();
        Settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisZ(), -0.05f);
        Settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
        Settings.mEnhancedInternalEdgeRemoval = true;
        Settings.mPredictiveContactDistance = 0.08f;
        Settings.mCollisionTolerance = 0.001f;
        Settings.mCharacterPadding = 0.015f;
        Settings.mPenetrationRecoverySpeed = 1.0f;
        Settings.mMaxStrength = 0.0f;

        Character = new JPH::CharacterVirtual(
            &Settings,
            ToJoltPosition(Body->GetPhysicsLocation()),
            ToJoltQuat(GetWorldQuat(Body->GetUpdatedComponent())),
            &Impl->PhysicsSystem);
    }

    const FVector ComponentLocation = Body->GetPhysicsLocation();
    const FVector CharacterLocation = ToEngineVector(Character->GetPosition());
    if ((CharacterLocation - ComponentLocation).SizeSquared() > 0.25f)
    {
        Character->SetPosition(ToJoltPosition(ComponentLocation));
        Character->RefreshContacts(
            Impl->PhysicsSystem.GetDefaultBroadPhaseLayerFilter(ObjectLayers::Moving),
            Impl->PhysicsSystem.GetDefaultLayerFilter(ObjectLayers::Moving),
            {},
            {},
            *Impl->TempAllocator);
    }

    Character->SetRotation(ToJoltQuat(GetWorldQuat(Body->GetUpdatedComponent())));
    Character->SetLinearVelocity(ToJoltVector(DesiredVelocity));
    Character->SetEnhancedInternalEdgeRemoval(true);

    JPH::CharacterVirtual::ExtendedUpdateSettings UpdateSettings;
    const float SafeStickDistance = std::clamp(GroundStickDistance, 0.02f, 0.5f);
    UpdateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, 0.0f, -SafeStickDistance);
    UpdateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.0f, 0.25f);
    UpdateSettings.mWalkStairsStepDownExtra = JPH::Vec3(0.0f, 0.0f, -0.15f);

    const JPH::Vec3 Gravity(0.0f, 0.0f, -9.8f);
    const JPH::DefaultBroadPhaseLayerFilter BroadPhaseFilter = Impl->PhysicsSystem.GetDefaultBroadPhaseLayerFilter(ObjectLayers::Moving);
    const JPH::DefaultObjectLayerFilter ObjectLayerFilter = Impl->PhysicsSystem.GetDefaultLayerFilter(ObjectLayers::Moving);
    JPH::ShapeFilter ShapeFilter;
    JPH::IgnoreMultipleBodiesFilter BodyFilter;
    BodyFilter.Reserve(static_cast<JPH::uint>(Impl->HeldBodyOwners.size() + 1));

    auto BodyIt = Impl->DynamicBodies.find(Body);
    if (BodyIt != Impl->DynamicBodies.end())
    {
        BodyFilter.IgnoreBody(BodyIt->second);
    }

    for (const auto& Pair : Impl->HeldBodyOwners)
    {
        if (Pair.second != Body)
        {
            continue;
        }

        auto HeldBodyIt = Impl->DynamicBodies.find(Pair.first);
        if (HeldBodyIt != Impl->DynamicBodies.end())
        {
            BodyFilter.IgnoreBody(HeldBodyIt->second);
        }
    }

    Character->ExtendedUpdate(
        DeltaTime,
        Gravity,
        UpdateSettings,
        BroadPhaseFilter,
        ObjectLayerFilter,
        BodyFilter,
        ShapeFilter,
        *Impl->TempAllocator);

    if (BodyIt != Impl->DynamicBodies.end())
    {
        Impl->PhysicsSystem.GetBodyInterface().SetPositionAndRotation(
            BodyIt->second,
            Character->GetPosition(),
            Character->GetRotation(),
            JPH::EActivation::Activate);
    }

    OutLocation = ToEngineVector(Character->GetPosition());
    OutVelocity = ToEngineVector(Character->GetLinearVelocity());
    bOutGrounded = Character->IsSupported();
    return true;
}

void FJoltPhysicsSystem::SetBodyLinearVelocity(URigidBodyComponent* Body, const FVector& Velocity)
{
    if (Impl == nullptr || Body == nullptr || Impl->DynamicBodies.find(Body) == Impl->DynamicBodies.end() || Body->IsHeldByPhysicsHandle() || !Body->IsDynamicBody())
    {
        return;
    }

    Impl->PhysicsSystem.GetBodyInterface().SetLinearVelocity(Impl->DynamicBodies[Body], ToJoltVector(Velocity));
}

void FJoltPhysicsSystem::AddBodyImpulse(URigidBodyComponent* Body, const FVector& Impulse)
{
    if (Impl == nullptr || Body == nullptr || Impl->DynamicBodies.find(Body) == Impl->DynamicBodies.end() || !Body->IsDynamicBody())
    {
        return;
    }

    Impl->PhysicsSystem.GetBodyInterface().AddImpulse(Impl->DynamicBodies[Body], ToJoltVector(Impulse));
}
