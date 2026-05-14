#pragma once

#include "Component/ActorComponent.h"
#include "Core/CollisionTypes.h"
#include "Math/Vector.h"

class USceneComponent;

class UHitSquashComponent : public UActorComponent
{
public:
    DECLARE_CLASS(UHitSquashComponent, UActorComponent)

    void BeginPlay() override;
    void EndPlay() override;
    void OnRegister() override;
    void OnUnregister() override;

    void Serialize(FArchive& Ar) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

    void SetTargetComponent(USceneComponent* InTargetComponent);
    USceneComponent* GetTargetComponent() const { return TargetComponent; }

    void TriggerHitSquash();
    bool IsAnimating() const { return bAnimating; }

protected:
    void TickComponent(float DeltaTime) override;

private:
    USceneComponent* ResolveTargetComponent() const;
    void CaptureBaseScale(bool bForce);
    void ApplyScale(const FVector& NewScale);

    void BindCollisionEvents();
    void UnbindCollisionEvents();
    void HandleHit(const FHitResult& Hit);

    float EaseOutCubic(float Alpha) const;
    float EaseInOutCubic(float Alpha) const;

private:
    USceneComponent* TargetComponent = nullptr;

    FVector SquashScaleMultiplier = FVector(1.25f, 1.25f, 0.75f);
    float SquashInDuration = 0.06f;
    float SquashHoldDuration = 0.0f;
    float RecoverDuration = 0.12f;
    float HitCooldown = 0.0f;

    bool bBindHitEvents = true;
    bool bRestartOnHit = true;
    bool bRestoreScaleOnEndPlay = true;

    FVector BaseScale = FVector::OneVector;
    float ElapsedTime = 0.0f;
    float CooldownRemaining = 0.0f;

    bool bAnimating = false;
    bool bHasBaseScale = false;
};
