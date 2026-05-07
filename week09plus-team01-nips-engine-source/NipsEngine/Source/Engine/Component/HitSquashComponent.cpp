#include "Component/HitSquashComponent.h"

#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logger.h"
#include "Engine/GameFramework/AActor.h"
#include "Math/Utils.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(UHitSquashComponent, UActorComponent)
REGISTER_FACTORY(UHitSquashComponent)

void UHitSquashComponent::BeginPlay()
{
    if (HasBegunPlay())
    {
        return;
    }

    UActorComponent::BeginPlay();
    CaptureBaseScale(true);
    BindCollisionEvents();
}

void UHitSquashComponent::EndPlay()
{
    UnbindCollisionEvents();

    if (bRestoreScaleOnEndPlay && bHasBaseScale)
    {
        ApplyScale(BaseScale);
    }

    bAnimating = false;
    ElapsedTime = 0.0f;
    UActorComponent::EndPlay();
}

void UHitSquashComponent::OnRegister()
{
    if (bRegistered)
    {
        return;
    }

    bRegistered = true;
}

void UHitSquashComponent::OnUnregister()
{
    UnbindCollisionEvents();
    bRegistered = false;
}

void UHitSquashComponent::Serialize(FArchive& Ar)
{
    UActorComponent::Serialize(Ar);

    uint32 TargetComponentUUID = TargetComponent ? TargetComponent->GetUUID() : 0;
    Ar << "TargetComponentUUID" << TargetComponentUUID;
    Ar << "SquashScaleMultiplier" << SquashScaleMultiplier;
    Ar << "SquashInDuration" << SquashInDuration;
    Ar << "SquashHoldDuration" << SquashHoldDuration;
    Ar << "RecoverDuration" << RecoverDuration;
    Ar << "HitCooldown" << HitCooldown;
    Ar << "BindHitEvents" << bBindHitEvents;
    Ar << "RestartOnHit" << bRestartOnHit;
    Ar << "RestoreScaleOnEndPlay" << bRestoreScaleOnEndPlay;
}

void UHitSquashComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UActorComponent::GetEditableProperties(OutProps);

    OutProps.push_back({ "Target Component", EPropertyType::SceneComponentRef, &TargetComponent });
    OutProps.push_back({ "Squash Scale Multiplier", EPropertyType::Vec3, &SquashScaleMultiplier, 0.01f, 5.0f, 0.01f });
    OutProps.push_back({ "Squash In Duration", EPropertyType::Float, &SquashInDuration, 0.0f, 2.0f, 0.01f });
    OutProps.push_back({ "Squash Hold Duration", EPropertyType::Float, &SquashHoldDuration, 0.0f, 2.0f, 0.01f });
    OutProps.push_back({ "Recover Duration", EPropertyType::Float, &RecoverDuration, 0.0f, 2.0f, 0.01f });
    OutProps.push_back({ "Hit Cooldown", EPropertyType::Float, &HitCooldown, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Bind Hit Events", EPropertyType::Bool, &bBindHitEvents });
    OutProps.push_back({ "Restart On Hit", EPropertyType::Bool, &bRestartOnHit });
    OutProps.push_back({ "Restore Scale On End Play", EPropertyType::Bool, &bRestoreScaleOnEndPlay });
}

void UHitSquashComponent::SetTargetComponent(USceneComponent* InTargetComponent)
{
    TargetComponent = InTargetComponent;
    CaptureBaseScale(true);
}

void UHitSquashComponent::TriggerHitSquash()
{
    USceneComponent* Target = ResolveTargetComponent();
    if (Target == nullptr)
    {
        return;
    }

    if (bAnimating && !bRestartOnHit)
    {
        return;
    }

    if (!bAnimating)
    {
        CaptureBaseScale(true);
    }

    bAnimating = true;
    ElapsedTime = 0.0f;
    CooldownRemaining = HitCooldown;
}

void UHitSquashComponent::TickComponent(float DeltaTime)
{
    if (CooldownRemaining > 0.0f)
    {
        CooldownRemaining = std::max(CooldownRemaining - DeltaTime, 0.0f);
    }

    if (!bAnimating)
    {
        return;
    }

    USceneComponent* Target = ResolveTargetComponent();
    if (Target == nullptr)
    {
        bAnimating = false;
        return;
    }

    const float InDuration = std::max(SquashInDuration, 0.0f);
    const float HoldDuration = std::max(SquashHoldDuration, 0.0f);
    const float OutDuration = std::max(RecoverDuration, 0.0f);
    const float TotalDuration = InDuration + HoldDuration + OutDuration;
    if (TotalDuration <= MathUtil::SmallNumber)
    {
        ApplyScale(BaseScale);
        bAnimating = false;
        return;
    }

    ElapsedTime += DeltaTime;

    const FVector SquashedScale = BaseScale * SquashScaleMultiplier;
    if (ElapsedTime < InDuration && InDuration > MathUtil::SmallNumber)
    {
        const float Alpha = MathUtil::Clamp(ElapsedTime / InDuration, 0.0f, 1.0f);
        ApplyScale(FVector::Lerp(BaseScale, SquashedScale, EaseOutCubic(Alpha)));
        return;
    }

    if (ElapsedTime < InDuration + HoldDuration)
    {
        ApplyScale(SquashedScale);
        return;
    }

    const float RecoverElapsed = ElapsedTime - InDuration - HoldDuration;
    const float Alpha = OutDuration > MathUtil::SmallNumber
        ? MathUtil::Clamp(RecoverElapsed / OutDuration, 0.0f, 1.0f)
        : 1.0f;
    ApplyScale(FVector::Lerp(SquashedScale, BaseScale, EaseInOutCubic(Alpha)));

    if (ElapsedTime >= TotalDuration)
    {
        ApplyScale(BaseScale);
        bAnimating = false;
        ElapsedTime = 0.0f;
    }
}

USceneComponent* UHitSquashComponent::ResolveTargetComponent() const
{
    if (TargetComponent != nullptr)
    {
        return TargetComponent;
    }

    AActor* OwnerActor = GetOwner();
    return OwnerActor ? OwnerActor->GetRootComponent() : nullptr;
}

void UHitSquashComponent::CaptureBaseScale(bool bForce)
{
    if (bHasBaseScale && !bForce)
    {
        return;
    }

    USceneComponent* Target = ResolveTargetComponent();
    if (Target == nullptr)
    {
        return;
    }

    BaseScale = Target->GetRelativeScale();
    bHasBaseScale = true;
}

void UHitSquashComponent::ApplyScale(const FVector& NewScale)
{
    USceneComponent* Target = ResolveTargetComponent();
    if (Target != nullptr)
    {
        Target->SetRelativeScale(NewScale);
    }
}

void UHitSquashComponent::BindCollisionEvents()
{
    if (!bBindHitEvents)
    {
        return;
    }

    AActor* OwnerActor = GetOwner();
    if (OwnerActor == nullptr)
    {
        return;
    }

    for (UPrimitiveComponent* Primitive : OwnerActor->GetPrimitiveComponents())
    {
        if (Primitive == nullptr)
        {
            continue;
        }

        Primitive->OnComponentHit.AddDynamic(this, &UHitSquashComponent::HandleHit);
    }
}

void UHitSquashComponent::UnbindCollisionEvents()
{
    AActor* OwnerActor = GetOwner();
    if (OwnerActor == nullptr)
    {
        return;
    }

    for (UPrimitiveComponent* Primitive : OwnerActor->GetPrimitiveComponents())
    {
        if (Primitive == nullptr)
        {
            continue;
        }

        Primitive->OnComponentHit.RemoveDynamic(this);
    }
}

void UHitSquashComponent::HandleHit(const FHitResult& Hit)
{
    if (!Hit.IsValid())
    {
        return;
    }

    if (CooldownRemaining > 0.0f)
    {
        return;
    }

    AActor* OwnerActor = GetOwner();
    UPrimitiveComponent* HitComponent = Hit.HitComponent;
    AActor* OtherActor = HitComponent ? HitComponent->GetOwner() : nullptr;
    UE_LOG("[HitSquash] Trigger Owner=%s(%u), Other=%s(%u), Location=(%.3f, %.3f, %.3f)",
        OwnerActor ? *OwnerActor->GetName() : "None",
        OwnerActor ? OwnerActor->GetUUID() : 0,
        OtherActor ? *OtherActor->GetName() : "None",
        OtherActor ? OtherActor->GetUUID() : 0,
        Hit.Location.X,
        Hit.Location.Y,
        Hit.Location.Z);

    TriggerHitSquash();
}

float UHitSquashComponent::EaseOutCubic(float Alpha) const
{
    Alpha = MathUtil::Clamp(Alpha, 0.0f, 1.0f);
    const float Inv = 1.0f - Alpha;
    return 1.0f - Inv * Inv * Inv;
}

float UHitSquashComponent::EaseInOutCubic(float Alpha) const
{
    Alpha = MathUtil::Clamp(Alpha, 0.0f, 1.0f);
    if (Alpha < 0.5f)
    {
        return 4.0f * Alpha * Alpha * Alpha;
    }

    const float T = -2.0f * Alpha + 2.0f;
    return 1.0f - (T * T * T) * 0.5f;
}
