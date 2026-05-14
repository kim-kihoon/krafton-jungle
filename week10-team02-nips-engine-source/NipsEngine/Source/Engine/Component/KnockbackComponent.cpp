#include "Component/KnockbackComponent.h"

#include "Component/Movement/MovementComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Core/PropertyTypes.h"
#include "Engine/GameFramework/AActor.h"
#include "Math/Utils.h"
#include "Object/ObjectFactory.h"
#include "Physics/JoltPhysicsSystem.h"

DEFINE_CLASS(UKnockbackComponent, UActorComponent)
REGISTER_FACTORY(UKnockbackComponent)

namespace
{
constexpr float GravityAcceleration = 9.8f;
}

void UKnockbackComponent::BeginPlay()
{
    if (HasBegunPlay())
    {
        return;
    }

    UActorComponent::BeginPlay();
}

void UKnockbackComponent::EndPlay()
{
    StopKnockback();
    UActorComponent::EndPlay();
}

void UKnockbackComponent::Serialize(FArchive& Ar)
{
    UActorComponent::Serialize(Ar);

    Ar << "DefaultStrength" << DefaultStrength;
    Ar << "DefaultDuration" << DefaultDuration;
    Ar << "VerticalBoost" << VerticalBoost;
    Ar << "GravityScale" << GravityScale;
    Ar << "OverrideExisting" << bOverrideExisting;
    Ar << "LockControlDuringKnockback" << bLockControlDuringKnockback;
    Ar << "ApplyGravityDuringKnockback" << bApplyGravityDuringKnockback;
    Ar << "UseDurationFalloff" << bUseDurationFalloff;
    Ar << "UseRigidBodyImpulse" << bUseRigidBodyImpulse;
}

void UKnockbackComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UActorComponent::GetEditableProperties(OutProps);

    OutProps.push_back({ "Default Strength", EPropertyType::Float, &DefaultStrength, 0.0f, 100.0f, 0.1f });
    OutProps.push_back({ "Default Duration", EPropertyType::Float, &DefaultDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Vertical Boost", EPropertyType::Float, &VerticalBoost, -100.0f, 100.0f, 0.1f });
    OutProps.push_back({ "Gravity Scale", EPropertyType::Float, &GravityScale, 0.0f, 10.0f, 0.05f });
    OutProps.push_back({ "Override Existing", EPropertyType::Bool, &bOverrideExisting });
    OutProps.push_back({ "Lock Control During Knockback", EPropertyType::Bool, &bLockControlDuringKnockback });
    OutProps.push_back({ "Apply Gravity During Knockback", EPropertyType::Bool, &bApplyGravityDuringKnockback });
    OutProps.push_back({ "Use Duration Falloff", EPropertyType::Bool, &bUseDurationFalloff });
    OutProps.push_back({ "Use RigidBody Impulse", EPropertyType::Bool, &bUseRigidBodyImpulse });
}

void UKnockbackComponent::TriggerKnockback(const FVector& Direction, float Strength, float Duration)
{
    if (IsKnockbackActive() && !bOverrideExisting)
    {
        return;
    }

    const float FinalStrength = Strength > 0.0f ? Strength : DefaultStrength;
    const float FinalDuration = Duration > 0.0f ? Duration : DefaultDuration;
    if (FinalStrength <= MathUtil::SmallNumber || FinalDuration <= MathUtil::SmallNumber)
    {
        return;
    }

    URigidBodyComponent* Body = FindRigidBodyComponent();
    if (Body == nullptr)
    {
        return;
    }

    bControlLockOnly = Direction.IsNearlyZero();
    KnockbackVelocity = bControlLockOnly
        ? FVector::ZeroVector
        : Direction.GetSafeNormal() * FinalStrength;
    if (!bControlLockOnly)
    {
        KnockbackVelocity.Z += VerticalBoost;
    }
    RemainingTime = FinalDuration;
    TotalDuration = FinalDuration;
    ClearMovementInputState();
    SuspendMovementComponents();

    if (!KnockbackVelocity.IsNearlyZero() && Body->IsDynamicBody() && bUseRigidBodyImpulse)
    {
        Body->AddImpulse(KnockbackVelocity);
    }
    else if (!KnockbackVelocity.IsNearlyZero() && Body->IsDynamicBody())
    {
        Body->SetVelocity(KnockbackVelocity);
    }
}

void UKnockbackComponent::StopKnockback()
{
    RestoreMovementComponents(false);
    RemainingTime = 0.0f;
    TotalDuration = 0.0f;
    KnockbackVelocity = FVector::ZeroVector;
    bControlLockOnly = false;
}

void UKnockbackComponent::TickComponent(float DeltaTime)
{
    if (!IsKnockbackActive())
    {
        return;
    }

    const float ClampedDeltaTime = std::max(DeltaTime, 0.0f);
    RemainingTime = std::max(RemainingTime - ClampedDeltaTime, 0.0f);

    if (bControlLockOnly)
    {
        if (!IsKnockbackActive())
        {
            RestoreMovementComponents(false);
            TotalDuration = 0.0f;
            KnockbackVelocity = FVector::ZeroVector;
            bControlLockOnly = false;
        }
        return;
    }

    if (bApplyGravityDuringKnockback)
    {
        KnockbackVelocity.Z -= GravityAcceleration * GravityScale * ClampedDeltaTime;
    }

    if (URigidBodyComponent* Body = FindRigidBodyComponent())
    {
        FVector ResolvedVelocity = KnockbackVelocity;
        const bool bGrounded = ApplyKnockbackVelocity(Body, KnockbackVelocity, ClampedDeltaTime, ResolvedVelocity);
        KnockbackVelocity = ResolvedVelocity;

        if (bGrounded && KnockbackVelocity.Z < 0.0f)
        {
            KnockbackVelocity.Z = 0.0f;
        }

        (void)bGrounded;
    }

    if (!IsKnockbackActive())
    {
        RestoreMovementComponents(true);
        TotalDuration = 0.0f;
        KnockbackVelocity = FVector::ZeroVector;
        bControlLockOnly = false;
    }
}

URigidBodyComponent* UKnockbackComponent::FindRigidBodyComponent() const
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
            return Body;
        }
    }

    return nullptr;
}

void UKnockbackComponent::ClearMovementInputState() const
{
    AActor* OwnerActor = GetOwner();
    if (OwnerActor == nullptr)
    {
        return;
    }

    for (UActorComponent* Component : OwnerActor->GetComponents())
    {
        if (UMovementComponent* Movement = Cast<UMovementComponent>(Component))
        {
            Movement->SetPendingInputVector(FVector::ZeroVector);
            Movement->StopMovementImmediately();
        }
    }
}

void UKnockbackComponent::SuspendMovementComponents()
{
    RestoreMovementComponents(false);

    AActor* OwnerActor = GetOwner();
    if (OwnerActor == nullptr)
    {
        return;
    }

    for (UActorComponent* Component : OwnerActor->GetComponents())
    {
        if (UMovementComponent* Movement = Cast<UMovementComponent>(Component))
        {
            Movement->SetPendingInputVector(FVector::ZeroVector);
            Movement->StopMovementImmediately();
            SuspendedMovementComponents.push_back(Movement);
            SuspendedMovementTickStates.push_back(Movement->IsComponentTickEnabled());
            Movement->SetComponentTickEnabled(false);
        }
    }
}

void UKnockbackComponent::RestoreMovementComponents(bool bKeepCurrentVelocity)
{
    const size_t Count = std::min(SuspendedMovementComponents.size(), SuspendedMovementTickStates.size());
    for (size_t Index = 0; Index < Count; ++Index)
    {
        if (UMovementComponent* Movement = SuspendedMovementComponents[Index])
        {
            Movement->SetPendingInputVector(FVector::ZeroVector);
            if (bKeepCurrentVelocity)
            {
                Movement->SetVelocity(FVector(0.0f, 0.0f, std::min(KnockbackVelocity.Z, 0.0f)));
            }
            else
            {
                Movement->StopMovementImmediately();
            }
            Movement->SetComponentTickEnabled(SuspendedMovementTickStates[Index]);
        }
    }

    SuspendedMovementComponents.clear();
    SuspendedMovementTickStates.clear();
}

bool UKnockbackComponent::ApplyKnockbackVelocity(URigidBodyComponent* Body, const FVector& Velocity, float DeltaTime, FVector& OutResolvedVelocity)
{
    OutResolvedVelocity = Velocity;
    if (Body == nullptr)
    {
        return true;
    }

    if (Body->IsDynamicBody())
    {
        Body->SetVelocity(Velocity);
        return false;
    }

    if (Body->IsKinematicBody() && DeltaTime > 0.0f)
    {
        FVector ResolvedLocation = Body->GetPhysicsLocation();
        FVector ResolvedVelocity = Velocity;
        bool bResolvedGrounded = false;
        if (FJoltPhysicsSystem::Get().MoveCharacter(Body, Velocity, DeltaTime, 0.02f, ResolvedLocation, ResolvedVelocity, bResolvedGrounded))
        {
            Body->SetPhysicsLocation(ResolvedLocation);
            OutResolvedVelocity = ResolvedVelocity;
            return bResolvedGrounded;
        }
    }

    return true;
}

float UKnockbackComponent::GetFalloffAlpha() const
{
    if (!bUseDurationFalloff || TotalDuration <= MathUtil::SmallNumber)
    {
        return 1.0f;
    }

    return MathUtil::Clamp(RemainingTime / TotalDuration, 0.0f, 1.0f);
}
