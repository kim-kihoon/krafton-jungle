#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"

class URigidBodyComponent;
class UMovementComponent;

class UKnockbackComponent : public UActorComponent
{
public:
    DECLARE_CLASS(UKnockbackComponent, UActorComponent)

    void BeginPlay() override;
    void EndPlay() override;

    void Serialize(FArchive& Ar) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

    void TriggerKnockback(const FVector& Direction, float Strength, float Duration);
    void StopKnockback();

    bool IsKnockbackActive() const { return RemainingTime > 0.0f; }
    bool IsControlLocked() const { return bLockControlDuringKnockback && IsKnockbackActive(); }

protected:
    void TickComponent(float DeltaTime) override;

private:
    URigidBodyComponent* FindRigidBodyComponent() const;
    void ClearMovementInputState() const;
    void SuspendMovementComponents();
    void RestoreMovementComponents(bool bKeepCurrentVelocity = false);
    bool ApplyKnockbackVelocity(URigidBodyComponent* Body, const FVector& Velocity, float DeltaTime, FVector& OutResolvedVelocity);
    float GetFalloffAlpha() const;

private:
    FVector KnockbackVelocity = FVector::ZeroVector;
    TArray<UMovementComponent*> SuspendedMovementComponents;
    TArray<bool> SuspendedMovementTickStates;
    float RemainingTime = 0.0f;
    float TotalDuration = 0.0f;

    float DefaultStrength = 8.0f;
    float DefaultDuration = 0.25f;
    float VerticalBoost = 0.0f;
    float GravityScale = 1.0f;
    bool bOverrideExisting = true;
    bool bLockControlDuringKnockback = true;
    bool bApplyGravityDuringKnockback = true;
    bool bUseDurationFalloff = true;
    bool bUseRigidBodyImpulse = true;
    bool bControlLockOnly = false;
};
