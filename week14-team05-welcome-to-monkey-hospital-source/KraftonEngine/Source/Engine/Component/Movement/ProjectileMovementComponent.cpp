#include "ProjectileMovementComponent.h"

#include "Component/SceneComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Render/Scene/FScene.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool BuildSweepShapeForProjectileComponent(USceneComponent* Component, FCollisionShape& OutShape)
    {
        if (!Component)
        {
            return false;
        }

        if (USphereComponent* Sphere = Cast<USphereComponent>(Component))
        {
            const float Radius = Sphere->GetScaledSphereRadius();
            if (Radius > 0.0f)
            {
                OutShape = FCollisionShape::MakeSphere(Radius);
                return true;
            }
            return false;
        }

        if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Component))
        {
            const float Radius     = Capsule->GetScaledCapsuleRadius();
            const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
            if (Radius > 0.0f && HalfHeight > 0.0f)
            {
                OutShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
                return true;
            }
            return false;
        }

        if (UBoxComponent* Box = Cast<UBoxComponent>(Component))
        {
            const FVector Extent = Box->GetScaledBoxExtent();
            if (Extent.X > 0.0f && Extent.Y > 0.0f && Extent.Z > 0.0f)
            {
                OutShape = FCollisionShape::MakeBox(Extent);
                return true;
            }
            return false;
        }

        if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
        {
            const FBoundingBox Bounds = Primitive->GetWorldBoundingBox();
            if (Bounds.IsValid())
            {
                const FVector Extent = Bounds.GetExtent();
                if (Extent.X > 0.0f && Extent.Y > 0.0f && Extent.Z > 0.0f)
                {
                    // 정확한 convex/mesh sweep은 아직 없으므로 컴포넌트 origin 기준 bounding sphere로 보수적으로 감싼다.
                    // AABB 중심이 origin과 달라도 false negative가 나지 않게 center offset까지 radius에 포함한다.
                    const FVector CenterOffset = Bounds.GetCenter() - Component->GetWorldLocation();
                    const float Radius = CenterOffset.Length() + Extent.Length();
                    if (Radius > 0.0f)
                    {
                        OutShape = FCollisionShape::MakeSphere(Radius);
                        return true;
                    }
                }
            }
        }

        return false;
    }

	void AddProjectileVelocityArrow(FScene& Scene, const FVector& Start, const FVector& Velocity)
	{
		constexpr float ProjectileArrowScale = 0.25f;
		const FVector ScaledVelocity = Velocity * ProjectileArrowScale;
		const float VelocityLength = ScaledVelocity.Length();
		if (VelocityLength <= FMath::Epsilon)
		{
			return;
		}

		const FVector Direction = ScaledVelocity / VelocityLength;
		const FVector End = Start + ScaledVelocity;
		const FColor ArrowColor(135, 206, 235);

		Scene.AddDebugLine(Start, End, ArrowColor);

		const float HeadLength = Clamp(VelocityLength * 0.2f, 0.2f, 1.5f);
		FVector ReferenceUp(0.0f, 0.0f, 1.0f);
		if (std::abs(Direction.Dot(ReferenceUp)) > 0.98f)
		{
			ReferenceUp = FVector(0.0f, 1.0f, 0.0f);
		}

		const FVector Side = Direction.Cross(ReferenceUp).Normalized();
		const FVector Back = Direction * HeadLength;
		const FVector SideOffset = Side * (HeadLength * 0.45f);

		Scene.AddDebugLine(End, End - Back + SideOffset, ArrowColor);
		Scene.AddDebugLine(End, End - Back - SideOffset, ArrowColor);
	}
}

void UProjectileMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();
	InitializeVelocityIfNeeded();
}

void UProjectileMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent)
	{
		return;
	}

	InitializeVelocityIfNeeded();

	if (ProjectileGravityScale != 0.0f)
	{
		AActor* OwnerActor = GetOwner();
		UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
		const FVector Gravity = World ? World->GetWorldSettings().Gravity : FVector(0.0f, 0.0f, -9.81f);
		Velocity += Gravity * ProjectileGravityScale * DeltaTime;
	}

	const float CurrentSpeed = Velocity.Length();
	if (MaxSpeed > 0.0f && CurrentSpeed > MaxSpeed)
	{
		Velocity = Velocity.Normalized() * MaxSpeed;
	}

	const FVector MoveDelta = Velocity * DeltaTime;
	if (MoveDelta.Length() <= FMath::Epsilon)
	{
		return;
	}

    const FVector CurrentLocation = UpdatedSceneComponent->GetWorldLocation();
    const FVector TargetLocation  = CurrentLocation + MoveDelta;

    if (bSweepCollision)
    {
        FCollisionShape SweepShape;
        if (BuildSweepShapeForProjectileComponent(UpdatedSceneComponent, SweepShape))
        {
            AActor* OwnerActor = GetOwner();
            UWorld* World      = OwnerActor ? OwnerActor->GetWorld() : nullptr;

            if (World)
            {
                ECollisionChannel TraceChannel = ECollisionChannel::Projectile;
                if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(UpdatedSceneComponent))
                {
                    TraceChannel = Primitive->GetCollisionObjectType();
                }

                FHitResult Hit;
                const FQuat SweepRotation = UpdatedSceneComponent->GetWorldMatrix().ToQuat();
                const AActor* IgnoreActor = IgnoredActor.IsValid() ? IgnoredActor.Get() : OwnerActor;
                if (World->PhysicsSweep(CurrentLocation, TargetLocation, SweepRotation, SweepShape, Hit, TraceChannel, IgnoreActor))
                {
                    const FVector MoveDir = MoveDelta.Normalized();
                    const float SafeDistance = (std::max)(0.0f, Hit.Distance - SweepPullbackDistance);
                    UpdatedSceneComponent->SetWorldLocation(CurrentLocation + MoveDir * SafeDistance);

                    if (UPrimitiveComponent* MovingPrimitive = Cast<UPrimitiveComponent>(UpdatedSceneComponent))
                    {
                        MovingPrimitive->NotifyComponentHit(
                            MovingPrimitive,
                            Hit.HitActor,
                            Hit.HitComponent,
                            FVector::ZeroVector,
                            Hit
                        );
                    }

                    HandleBlockingHit(UpdatedSceneComponent, CurrentLocation, MoveDelta, Hit);
                    return;
                }
            }
        }
    }

	UpdatedSceneComponent->SetWorldLocation(TargetLocation);
}

void UProjectileMovementComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	const FVector PreviewVelocity = GetPreviewVelocity();
	if (PreviewVelocity.Length() <= FMath::Epsilon)
	{
		return;
	}

	USceneComponent* SourceComponent = GetUpdatedComponent();
	if (!SourceComponent)
	{
		AActor* OwnerActor = GetOwner();
		SourceComponent = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
	}
	if (!SourceComponent)
	{
		return;
	}

	AddProjectileVelocityArrow(Scene, SourceComponent->GetWorldLocation(), PreviewVelocity);
}

void UProjectileMovementComponent::SetVelocity(const FVector& InVelocity)
{
	Velocity = InVelocity;
	bVelocityInitialized = false;
}

void UProjectileMovementComponent::StopSimulating()
{
	Velocity = FVector(0.0f, 0.0f, 0.0f);
	bVelocityInitialized = true;
}

void UProjectileMovementComponent::SetIgnoredActor(AActor* Actor)
{
	IgnoredActor = Actor;
}

FVector UProjectileMovementComponent::GetPreviewVelocity() const
{
	return ComputeEffectiveVelocity();
}

EProjectileHitBehavior UProjectileMovementComponent::GetHitBehavior() const
{
	return EProjectileHitBehavior::Stop;
}

FVector UProjectileMovementComponent::ComputeEffectiveVelocity() const
{
	if (bVelocityInitialized)
	{
		return Velocity;
	}

	FVector EffectiveVelocity = Velocity;

	if (EffectiveVelocity.Length() <= FMath::Epsilon)
	{
		USceneComponent* SourceComponent = GetUpdatedComponent();
		if (!SourceComponent)
		{
			AActor* OwnerActor = GetOwner();
			SourceComponent = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
		}

		if (SourceComponent)
		{
			EffectiveVelocity = SourceComponent->GetForwardVector().Normalized();
		}
	}

	if (InitialSpeed > 0.0f && EffectiveVelocity.Length() > FMath::Epsilon)
	{
		EffectiveVelocity *= InitialSpeed;
	}

	return EffectiveVelocity;
}

void UProjectileMovementComponent::InitializeVelocityIfNeeded()
{
	if (bVelocityInitialized)
	{
		return;
	}

	Velocity = ComputeEffectiveVelocity();
	bVelocityInitialized = true;
}

bool UProjectileMovementComponent::HandleBlockingHit(USceneComponent* UpdatedSceneComponent, const FVector& CurrentLocation, const FVector& MoveDelta, const FHitResult& HitResult)
{
    (void)UpdatedSceneComponent;
    (void)CurrentLocation;
    (void)MoveDelta;

    switch (GetHitBehavior())
    {
    case EProjectileHitBehavior::Destroy:
        if (AActor* OwnerActor = GetOwner())
        {
            if (UWorld* World = OwnerActor->GetWorld())
            {
                World->DestroyActor(OwnerActor);
            }
        }
        return true;

    case EProjectileHitBehavior::Bounce:
        if (!HitResult.ImpactNormal.IsNearlyZero())
        {
            const float VelocityIntoSurface = Velocity.Dot(HitResult.ImpactNormal);
            Velocity = Velocity - HitResult.ImpactNormal * (2.0f * VelocityIntoSurface);
        }
        return true;

    case EProjectileHitBehavior::Stop:
    default:
        StopSimulating();
        return true;
    }
}
