#include "Component/Physics/PhysicsHandleComponent.h"

#include "Component/Collision/ShapeComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Engine/Geometry/AABB.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cstring>

namespace
{
bool IsLiveObjectPointer(const UObject* Object)
{
    if (Object == nullptr)
    {
        return false;
    }

    for (const UObject* LiveObject : GUObjectArray)
    {
        if (LiveObject == Object)
        {
            return true;
        }
    }

    return false;
}

bool IsBlockingShape(UPrimitiveComponent* Primitive)
{
    return Primitive != nullptr && Primitive->IsBlockComponent() && Cast<UShapeComponent>(Primitive) != nullptr;
}

void GatherBlockingShapes(AActor* Actor, TArray<UPrimitiveComponent*>& OutShapes)
{
    if (Actor == nullptr)
    {
        return;
    }

    for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
    {
        if (IsBlockingShape(Primitive))
        {
            OutShapes.push_back(Primitive);
        }
    }
}

FQuat GetRigidBodyRotation(URigidBodyComponent* Body)
{
    if (Body == nullptr)
    {
        return FQuat::Identity;
    }

    if (USceneComponent* UpdatedComponent = Body->GetUpdatedComponent())
    {
        return UpdatedComponent->GetWorldTransform().GetRotation();
    }

    return FQuat::Identity;
}

void MoveHeldBodyToTarget(
    URigidBodyComponent* Body,
    const FVector& TargetLocation,
    const FQuat* TargetRotation,
    float DeltaTime,
    FVector& InOutHoldLocation,
    FVector& OutHoldVelocity)
{
    if (Body == nullptr)
    {
        OutHoldVelocity = FVector::ZeroVector;
        return;
    }

    const FVector PreviousLocation = Body->GetPhysicsLocation();
    const FQuat DesiredRotation = TargetRotation != nullptr ? *TargetRotation : GetRigidBodyRotation(Body);
    FVector ResolvedTargetLocation = TargetLocation;
    if (FJoltPhysicsSystem::Get().MoveKinematicBody(Body, ResolvedTargetLocation, DesiredRotation, DeltaTime))
    {
        InOutHoldLocation = ResolvedTargetLocation;
        OutHoldVelocity = DeltaTime > 0.0f ? (ResolvedTargetLocation - PreviousLocation) / DeltaTime : FVector::ZeroVector;
        return;
    }

    Body->SetPhysicsLocation(TargetLocation);
    if (TargetRotation != nullptr)
    {
        Body->SetPhysicsRotation(*TargetRotation);
    }
    InOutHoldLocation = TargetLocation;
    OutHoldVelocity = DeltaTime > 0.0f ? (TargetLocation - PreviousLocation) / DeltaTime : FVector::ZeroVector;
}

bool RaycastPrimitiveForPickup(UPrimitiveComponent* Primitive, const FRay& Ray, FHitResult& OutHit)
{
    if (Primitive == nullptr || Primitive->IsEditorOnly())
    {
        return false;
    }

    const EPrimitiveType PrimType = Primitive->GetPrimitiveType();
    if (PrimType != EPrimitiveType::EPT_StaticMesh && Cast<UShapeComponent>(Primitive) == nullptr)
    {
        return false;
    }

    if (Cast<UShapeComponent>(Primitive))
    {
        if (!Primitive->IsBlockComponent())
        {
            return false;
        }

        return Primitive->RaycastMesh(Ray, OutHit) && OutHit.IsValid();
    }

    return Primitive->Raycast(Ray, OutHit) && OutHit.IsValid();
}

bool LineTracePickup(UWorld* World, const FRay& Ray, float MaxDistance, const AActor* IgnoredActor, FHitResult& OutHit)
{
    if (World == nullptr)
    {
        return false;
    }

    FWorldSpatialIndex::FPrimitiveRayQueryScratch Scratch;
    TArray<UPrimitiveComponent*> Candidates;
    TArray<float> BroadHitTs;
    World->GetSpatialIndex().RayQueryPrimitives(Ray, Candidates, BroadHitTs, Scratch);

    bool bFoundHit = false;
    float ClosestDistance = MaxDistance;
    for (UPrimitiveComponent* Candidate : Candidates)
    {
        if (Candidate == nullptr || Candidate->GetOwner() == IgnoredActor)
        {
            continue;
        }

        FHitResult CandidateHit;
        if (!RaycastPrimitiveForPickup(Candidate, Ray, CandidateHit))
        {
            continue;
        }

        if (CandidateHit.Distance < 0.0f || CandidateHit.Distance > ClosestDistance)
        {
            continue;
        }

        ClosestDistance = CandidateHit.Distance;
        OutHit = CandidateHit;
        bFoundHit = true;
    }

    return bFoundHit;
}
} // namespace

DEFINE_CLASS(UPhysicsHandleComponent, UActorComponent)
REGISTER_FACTORY(UPhysicsHandleComponent)

UPhysicsHandleComponent::UPhysicsHandleComponent()
{
    bCanEverTick = true;
}

void UPhysicsHandleComponent::Serialize(FArchive& Ar)
{
    UActorComponent::Serialize(Ar);
    Ar << "TraceDistance" << TraceDistance;
    Ar << "DefaultHoldDistance" << DefaultHoldDistance;
    Ar << "HoldDistance" << HoldDistance;
    Ar << "SizeDistanceScale" << SizeDistanceScale;
    Ar << "MaxSizeDistanceOffset" << MaxSizeDistanceOffset;
    Ar << "SpringStrength" << SpringStrength;
    Ar << "Damping" << Damping;
    Ar << "MaxHoldSpeed" << MaxHoldSpeed;

    if (Ar.IsLoading())
    {
        ClampEditableValues();
        DefaultHoldDistance = HoldDistance;
    }
}

void UPhysicsHandleComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UActorComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "Trace Distance", EPropertyType::Float, &TraceDistance, 0.1f, 100.0f, 0.1f });
    OutProps.push_back({ "Default Hold Distance", EPropertyType::Float, &DefaultHoldDistance, 0.1f, 20.0f, 0.1f });
    OutProps.push_back({ "Hold Distance", EPropertyType::Float, &HoldDistance, 0.1f, 20.0f, 0.1f });
    OutProps.push_back({ "Size Distance Scale", EPropertyType::Float, &SizeDistanceScale, 0.0f, 5.0f, 0.05f });
    OutProps.push_back({ "Max Size Distance Offset", EPropertyType::Float, &MaxSizeDistanceOffset, 0.0f, 20.0f, 0.1f });
    OutProps.push_back({ "Spring Strength", EPropertyType::Float, &SpringStrength, 0.0f, 1000.0f, 1.0f });
    OutProps.push_back({ "Damping", EPropertyType::Float, &Damping, 0.0f, 1000.0f, 0.1f });
    OutProps.push_back({ "Max Hold Speed", EPropertyType::Float, &MaxHoldSpeed, 0.0f, 1000.0f, 0.1f });
}

void UPhysicsHandleComponent::PostEditProperty(const char* PropertyName)
{
    UActorComponent::PostEditProperty(PropertyName);
    ClampEditableValues();

    if (PropertyName != nullptr)
    {
        if (std::strcmp(PropertyName, "Hold Distance") == 0)
        {
            DefaultHoldDistance = HoldDistance;
        }
        else if (std::strcmp(PropertyName, "Default Hold Distance") == 0)
        {
            HoldDistance = DefaultHoldDistance;
        }
    }

    ClampEditableValues();
    CurrentHoldDistance = HoldDistance + HoldDistanceOffset;
}

bool UPhysicsHandleComponent::TryGrab(UWorld* World, const FViewportCamera* Camera)
{
    if (Camera == nullptr)
    {
        return false;
    }

    return TryGrab(World, Camera->GetLocation(), Camera->GetForwardVector());
}

bool UPhysicsHandleComponent::TryGrab(UWorld* World, const FVector& CameraLocation, const FVector& CameraForward)
{
    if (World == nullptr || HeldBody != nullptr)
    {
        return false;
    }

    ClampEditableValues();

    FHitResult Hit;
    const FVector Forward = CameraForward.GetSafeNormal();
    URigidBodyComponent* Body = FindPickableBody(World, CameraLocation, Forward, &Hit);
    if (Body == nullptr)
    {
        return false;
    }

    HeldBody = Body;
    HeldWorld = World;
    HoldLocation = Body->GetPhysicsLocation();
    LastHoldLocation = HoldLocation;
    HoldVelocity = FVector::ZeroVector;
    HoldDistanceOffset = ComputeHoldDistanceOffset(Body, CameraLocation, Forward);
    CurrentHoldDistance = HoldDistance + HoldDistanceOffset;
    Body->SetHeldByPhysicsHandle(true);
    FJoltPhysicsSystem::Get().SetHeldBodyOwner(Body, FindOwnerRigidBody());
    Body->SetVelocity(FVector::ZeroVector);
    Body->PlayPickupSound();
    return true;
}

URigidBodyComponent* UPhysicsHandleComponent::FindPickableBody(UWorld* World, const FVector& CameraLocation, const FVector& CameraForward, FHitResult* OutHit) const
{
    if (World == nullptr)
    {
        return nullptr;
    }

    const FVector Forward = CameraForward.GetSafeNormal();
    if (Forward.IsNearlyZero())
    {
        return nullptr;
    }

    FHitResult Hit;
    const FRay Ray(CameraLocation, Forward);
    if (!LineTracePickup(World, Ray, TraceDistance, GetOwner(), Hit))
    {
        return nullptr;
    }

    URigidBodyComponent* Body = FindRigidBodyFromHit(Hit);
    if (!IsLiveObjectPointer(Body) || !Body->CanBePickedUp())
    {
        return nullptr;
    }

    if (OutHit != nullptr)
    {
        *OutHit = Hit;
    }
    return Body;
}

void UPhysicsHandleComponent::Release()
{
    if (HeldBody == nullptr)
    {
        return;
    }

    if (!IsLiveObjectPointer(HeldBody))
    {
        FJoltPhysicsSystem::Get().SetHeldBodyCollisionSuppressed(HeldBody, false);
        FJoltPhysicsSystem::Get().ClearHeldBodyOwner(HeldBody);
        HeldBody = nullptr;
        HeldWorld = nullptr;
        HoldVelocity = FVector::ZeroVector;
        HoldDistanceOffset = 0.0f;
        CurrentHoldDistance = 0.0f;
        bHeldMovementCollisionSuppressed = false;
        return;
    }

    FJoltPhysicsSystem::Get().SetHeldBodyCollisionSuppressed(HeldBody, false);
    FJoltPhysicsSystem::Get().ClearHeldBodyOwner(HeldBody);
    HeldBody->SetHeldByPhysicsHandle(false);
    HeldBody->SetVelocity(HoldVelocity);
    HeldBody->QueueDropSound(HeldBody->GetDropSoundPath());
    HeldBody = nullptr;
    HeldWorld = nullptr;
    HoldVelocity = FVector::ZeroVector;
    HoldDistanceOffset = 0.0f;
    CurrentHoldDistance = 0.0f;
    bHeldMovementCollisionSuppressed = false;
}

void UPhysicsHandleComponent::TickHandle(float DeltaTime, const FViewportCamera* Camera, const FVector& TargetOffset, const FQuat* TargetRotation, bool bSnapToTarget)
{
    if (Camera == nullptr)
    {
        return;
    }

    TickHandle(DeltaTime, Camera->GetLocation(), Camera->GetForwardVector(), TargetOffset, TargetRotation, bSnapToTarget);
}

void UPhysicsHandleComponent::TickHandle(float DeltaTime, const FVector& CameraLocation, const FVector& CameraForward, const FVector& TargetOffset, const FQuat* TargetRotation, bool bSnapToTarget)
{
    if (DeltaTime <= 0.0f || HeldBody == nullptr)
    {
        return;
    }

    if (!IsLiveObjectPointer(HeldBody))
    {
        FJoltPhysicsSystem::Get().SetHeldBodyCollisionSuppressed(HeldBody, false);
        FJoltPhysicsSystem::Get().ClearHeldBodyOwner(HeldBody);
        HeldBody = nullptr;
        HeldWorld = nullptr;
        HoldVelocity = FVector::ZeroVector;
        HoldDistanceOffset = 0.0f;
        CurrentHoldDistance = 0.0f;
        bHeldMovementCollisionSuppressed = false;
        return;
    }

    ClampEditableValues();

    HoldLocation = HeldBody->GetPhysicsLocation();
    LastHoldLocation = HoldLocation;

    const FVector Target = GetHoldTarget(CameraLocation, CameraForward, TargetOffset);
    if (bSnapToTarget)
    {
        MoveHeldBodyToTarget(HeldBody, Target, TargetRotation, DeltaTime, HoldLocation, HoldVelocity);
        HeldBody->SetVelocity(HoldVelocity);
        return;
    }

    const FVector ToTarget = Target - HoldLocation;
    const FVector Acceleration = ToTarget * SpringStrength - HoldVelocity * Damping;
    HoldVelocity += Acceleration * DeltaTime;

    if (MaxHoldSpeed > 0.0f && HoldVelocity.SizeSquared() > MaxHoldSpeed * MaxHoldSpeed)
    {
        HoldVelocity = HoldVelocity.GetSafeNormal() * MaxHoldSpeed;
    }

    LastHoldLocation = HoldLocation;
    const FVector DesiredDelta = HoldVelocity * DeltaTime;
    const FVector NextLocation = HoldLocation + DesiredDelta;
    MoveHeldBodyToTarget(HeldBody, NextLocation, TargetRotation, DeltaTime, HoldLocation, HoldVelocity);
    HeldBody->SetVelocity(HoldVelocity);
}

void UPhysicsHandleComponent::SetHeldMovementCollisionSuppressed(bool bSuppress)
{
    if (bSuppress == bHeldMovementCollisionSuppressed)
    {
        return;
    }

    if (HeldBody == nullptr || !IsLiveObjectPointer(HeldBody))
    {
        bHeldMovementCollisionSuppressed = false;
        return;
    }

    bHeldMovementCollisionSuppressed = bSuppress;
    FJoltPhysicsSystem::Get().SetHeldBodyCollisionSuppressed(HeldBody, bSuppress);
}

URigidBodyComponent* UPhysicsHandleComponent::FindRigidBodyFromHit(const FHitResult& Hit) const
{
    const UPrimitiveComponent* HitComponent = Hit.HitComponent;
    AActor* HitActor = HitComponent ? HitComponent->GetOwner() : nullptr;
    if (HitActor == nullptr)
    {
        return nullptr;
    }

    for (UActorComponent* Component : HitActor->GetComponents())
    {
        if (URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component))
        {
            if (IsLiveObjectPointer(Body))
            {
                return Body;
            }
        }
    }

    return nullptr;
}

URigidBodyComponent* UPhysicsHandleComponent::FindOwnerRigidBody() const
{
    AActor* OwnerActor = GetOwner();
    if (OwnerActor == nullptr)
    {
        return nullptr;
    }

    for (UActorComponent* Component : OwnerActor->GetComponents())
    {
        if (URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component))
        {
            if (IsLiveObjectPointer(Body))
            {
                return Body;
            }
        }
    }

    return nullptr;
}

FVector UPhysicsHandleComponent::GetHoldTarget(const FVector& CameraLocation, const FVector& CameraForward, const FVector& TargetOffset) const
{
    const float TargetDistance = CurrentHoldDistance > 0.0f ? CurrentHoldDistance : (HoldDistance + HoldDistanceOffset);
    return CameraLocation + CameraForward.GetSafeNormal() * TargetDistance + TargetOffset;
}

float UPhysicsHandleComponent::ComputeHoldDistanceOffset(URigidBodyComponent* Body, const FVector& CameraLocation, const FVector& CameraForward) const
{
    (void)CameraLocation;
    if (Body == nullptr || Body->GetOwner() == nullptr || SizeDistanceScale <= 0.0f || MaxSizeDistanceOffset <= 0.0f)
    {
        return 0.0f;
    }

    TArray<UPrimitiveComponent*> BlockingShapes;
    GatherBlockingShapes(Body->GetOwner(), BlockingShapes);
    if (BlockingShapes.empty())
    {
        return 0.0f;
    }

    const FVector BodyLocation = Body->GetPhysicsLocation();
    const FVector Forward = CameraForward.GetSafeNormal();
    if (Forward.IsNearlyZero())
    {
        return 0.0f;
    }

    float RadiusTowardCamera = 0.0f;
    for (UPrimitiveComponent* Shape : BlockingShapes)
    {
        if (Shape == nullptr)
        {
            continue;
        }

        const FAABB Bounds = Shape->GetWorldAABB();
        const FVector Corners[8] = {
            FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z),
            FVector(Bounds.Min.X, Bounds.Min.Y, Bounds.Max.Z),
            FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Min.Z),
            FVector(Bounds.Min.X, Bounds.Max.Y, Bounds.Max.Z),
            FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Min.Z),
            FVector(Bounds.Max.X, Bounds.Min.Y, Bounds.Max.Z),
            FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Min.Z),
            FVector(Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z),
        };

        for (const FVector& Corner : Corners)
        {
            const float Projection = FVector::DotProduct(Corner - BodyLocation, Forward);
            RadiusTowardCamera = std::max(RadiusTowardCamera, -Projection);
        }
    }

    return std::min(RadiusTowardCamera * SizeDistanceScale, MaxSizeDistanceOffset);
}

void UPhysicsHandleComponent::ClampEditableValues()
{
    TraceDistance = std::max(0.1f, TraceDistance);
    DefaultHoldDistance = std::max(0.1f, DefaultHoldDistance);
    HoldDistance = std::max(0.1f, HoldDistance);
    SizeDistanceScale = std::max(0.0f, SizeDistanceScale);
    MaxSizeDistanceOffset = std::max(0.0f, MaxSizeDistanceOffset);
    SpringStrength = std::max(0.0f, SpringStrength);
    Damping = std::max(0.0f, Damping);
    MaxHoldSpeed = std::max(0.0f, MaxHoldSpeed);
}
