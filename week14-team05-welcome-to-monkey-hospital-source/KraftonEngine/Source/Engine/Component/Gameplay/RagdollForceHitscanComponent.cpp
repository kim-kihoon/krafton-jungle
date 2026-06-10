#include "RagdollForceHitscanComponent.h"

#include "Component/Gameplay/RagdollForceEmitterComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Character.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "Input/InputSystem.h"
#include "GameFramework/World.h"
#include "Runtime/Engine.h"
#include "Viewport/GameViewportClient.h"

#include <cmath>
#include <windows.h>

namespace
{
    constexpr int32 DefaultImpulseFireKey = VK_LBUTTON;
    constexpr int32 DefaultOwnerShockwaveKey = 'Q';

    enum class ERagdollForceTargetLocationSource
    {
        None,
        ActorFallback,
        MeshFallback,
        RagdollRepresentative
    };

    const char* GetNameSafe(const UObject* Object)
    {
        return Object ? Object->GetName().c_str() : "None";
    }

    const char* LexToString(ERagdollForceTargetLocationSource Source)
    {
        switch (Source)
        {
        case ERagdollForceTargetLocationSource::ActorFallback:
            return "ActorFallback";
        case ERagdollForceTargetLocationSource::MeshFallback:
            return "MeshFallback";
        case ERagdollForceTargetLocationSource::RagdollRepresentative:
            return "RagdollRepresentative";
        default:
            return "None";
        }
    }

    FVector ResolveRagdollForceTargetLocation(
        AActor* CandidateActor,
        USkeletalMeshComponent* TargetMesh,
        ERagdollForceTargetLocationSource* OutSource)
    {
        if (OutSource)
        {
            *OutSource = ERagdollForceTargetLocationSource::None;
        }

        if (!CandidateActor)
        {
            return FVector::ZeroVector;
        }

        if (!TargetMesh)
        {
            if (OutSource)
            {
                *OutSource = ERagdollForceTargetLocationSource::ActorFallback;
            }
            return CandidateActor->GetActorLocation();
        }

        FVector RepresentativeLocation = FVector::ZeroVector;
        if (TargetMesh->TryGetRagdollRepresentativeLocation(RepresentativeLocation))
        {
            if (OutSource)
            {
                *OutSource = ERagdollForceTargetLocationSource::RagdollRepresentative;
            }
            return RepresentativeLocation;
        }

        if (OutSource)
        {
            *OutSource = ERagdollForceTargetLocationSource::MeshFallback;
        }
        return TargetMesh->GetWorldLocation();
    }
}

URagdollForceHitscanComponent::URagdollForceHitscanComponent()
{
    ImpulseFireKey = DefaultImpulseFireKey;
    OwnerShockwaveKey = DefaultOwnerShockwaveKey;
}

void URagdollForceHitscanComponent::BeginPlay()
{
    UActorComponent::BeginPlay();
    CachedForceEmitter = ResolveForceEmitter();
    SetComponentTickEnabled(bEnableInputFiring);
}

void URagdollForceHitscanComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction& ThisTickFunction)
{
    UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
    (void)DeltaTime;
    (void)TickType;
    (void)ThisTickFunction;

    ProcessHitscanInput();
    DrawAutoTargetPreview();
}

bool URagdollForceHitscanComponent::FireImpulseHitscan()
{
    URagdollForceEmitterComponent* ForceEmitter = ResolveForceEmitter();
    if (!ForceEmitter)
    {
        LogHitscanResult("Impulse", nullptr, 0, false);
        return false;
    }

    FVector TargetLocation = FVector::ZeroVector;
    AActor* TargetActor = ResolveBestImpulseTargetActor(&TargetLocation);
    ERagdollForceTargetLocationSource TargetSource = ERagdollForceTargetLocationSource::None;
    if (TargetActor)
    {
        TargetLocation = ResolveRagdollForceTargetLocation(
            TargetActor,
            ResolveTargetMesh(TargetActor),
            &TargetSource);
    }
    FVector AimOrigin = FVector::ZeroVector;
    FVector AimDirection = FVector::ZeroVector;
    if (!TargetActor || !TryGetActionRay(AimOrigin, AimDirection))
    {
        LogHitscanResult("Impulse", nullptr, 0, false);
        return false;
    }

    const FVector ShotDirection =
        (TargetLocation - AimOrigin).IsNearlyZero()
            ? AimDirection
            : (TargetLocation - AimOrigin).Normalized();

    FRagdollImpulseRequest Request;
    Request.HitBoneName = FName::None;
    Request.WorldLocation = TargetLocation;
    Request.WorldDirection = ShotDirection;
    Request.Strength = ImpulseStrength;
    Request.HoldTimeOverride = -1.0f;
    Request.PreferredPreset = PreferredPreset;
    Request.bUsePreferredPreset = true;
    Request.bAllowEscalationToFullBody = bAllowEscalationToFullBody;
    Request.bAllowWhileMoving = bAllowWhileMoving;

    const bool bSucceeded = ForceEmitter->ApplyImpulseToActor(TargetActor, Request);
    LogHitscanResult("Impulse", &TargetLocation, bSucceeded ? 1 : 0, bSucceeded, LexToString(TargetSource));
    return bSucceeded;
}

bool URagdollForceHitscanComponent::FireOwnerCenteredShockwave()
{
    FVector Origin = FVector::ZeroVector;
    FVector Direction = FVector::ZeroVector;
    if (!TryGetActionRay(Origin, Direction))
    {
        LogHitscanResult("OwnerShockwave", nullptr, 0, false);
        return false;
    }

    const int32 AffectedCount = ApplyShockwaveBurstAtLocation(Origin, OwnerShockwaveStrength);
    const bool bSucceeded = AffectedCount > 0;
    LogHitscanResult("OwnerShockwave", &Origin, AffectedCount, bSucceeded);
    return bSucceeded;
}

bool URagdollForceHitscanComponent::WasActionPressed(const FInputSystemSnapshot& Snapshot, int32 KeyCode) const
{
    switch (KeyCode)
    {
    case VK_LBUTTON:
        return Snapshot.bLeftMousePressed || Snapshot.WasPressed(VK_LBUTTON);
    case VK_RBUTTON:
        return Snapshot.bRightMousePressed || Snapshot.WasPressed(VK_RBUTTON);
    case VK_MBUTTON:
        return Snapshot.bMiddleMousePressed || Snapshot.WasPressed(VK_MBUTTON);
    case VK_XBUTTON1:
        return Snapshot.bXButton1Pressed || Snapshot.WasPressed(VK_XBUTTON1);
    case VK_XBUTTON2:
        return Snapshot.bXButton2Pressed || Snapshot.WasPressed(VK_XBUTTON2);
    default:
        return Snapshot.WasPressed(KeyCode);
    }
}

URagdollForceEmitterComponent* URagdollForceHitscanComponent::ResolveForceEmitter() const
{
    URagdollForceEmitterComponent* CachedEmitter = CachedForceEmitter.Get();
    if (CachedEmitter && CachedEmitter->GetOwner() == GetOwner())
    {
        return CachedEmitter;
    }

    return GetOwner() ? GetOwner()->GetComponentByClass<URagdollForceEmitterComponent>() : nullptr;
}

bool URagdollForceHitscanComponent::CanProcessHitscanInput() const
{
    if (!bEnableInputFiring || !GEngine)
    {
        return false;
    }

    UGameViewportClient* GameViewportClient = GEngine->GetGameViewportClient();
    UWorld* World = GetWorld();
    if (!GameViewportClient || !GameViewportClient->HasGameInputSnapshot() || !World)
    {
        return false;
    }

    APlayerController* PlayerController = World->GetFirstPlayerController();
    APawn* PossessedPawn = PlayerController ? PlayerController->GetPossessedPawn() : nullptr;
    return PossessedPawn && PossessedPawn == GetOwner();
}

bool URagdollForceHitscanComponent::TryGetActionRay(FVector& OutOrigin, FVector& OutDirection) const
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return false;
    }

    OutOrigin = OwnerActor->GetActorLocation() + OwnerActor->GetActorUp() * AimOriginHeightOffset;
    OutDirection = OwnerActor->GetActorForward();
    return !OutDirection.IsNearlyZero();
}

USkeletalMeshComponent* URagdollForceHitscanComponent::ResolveTargetMesh(AActor* CandidateActor) const
{
    if (!CandidateActor)
    {
        return nullptr;
    }

    if (ACharacter* Character = Cast<ACharacter>(CandidateActor))
    {
        if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
        {
            return CharacterMesh;
        }
    }

    return CandidateActor->GetComponentByClass<USkeletalMeshComponent>();
}

FVector URagdollForceHitscanComponent::ResolveTargetLocation(AActor* CandidateActor) const
{
    USkeletalMeshComponent* TargetMesh = ResolveTargetMesh(CandidateActor);
    return ResolveRagdollForceTargetLocation(CandidateActor, TargetMesh, nullptr);
}

bool URagdollForceHitscanComponent::HasValidRagdollTarget(AActor* CandidateActor) const
{
    if (!CandidateActor || CandidateActor == GetOwner())
    {
        return false;
    }

    USkeletalMeshComponent* TargetMesh = ResolveTargetMesh(CandidateActor);
    return TargetMesh && TargetMesh->GetEffectivePhysicsAsset() != nullptr;
}

AActor* URagdollForceHitscanComponent::ResolveBestImpulseTargetActor(FVector* OutTargetLocation) const
{
    UWorld* World = GetWorld();
    FVector Origin;
    FVector Forward;
    if (!World || !TryGetActionRay(Origin, Forward))
    {
        return nullptr;
    }

    const float MaxAngleRadians = AutoTargetMaxAngleDegrees * (3.1415926535f / 180.0f);
    const float ConeSlope = std::tan(MaxAngleRadians);
    const float MinCaptureRadius = 120.0f;
    float BestDistance = 0.0f;
    float BestLateralDistance = 0.0f;
    AActor* BestActor = nullptr;
    FVector BestLocation = FVector::ZeroVector;

    for (AActor* Actor : World->GetActors())
    {
        if (!HasValidRagdollTarget(Actor))
        {
            continue;
        }

        const FVector CandidateLocation = ResolveTargetLocation(Actor);
        const FVector ToCandidate = CandidateLocation - Origin;
        const float DistanceSquared = ToCandidate.Dot(ToCandidate);
        if (DistanceSquared <= 1.0e-6f)
        {
            continue;
        }

        const float Distance = std::sqrt(DistanceSquared);
        if (Distance > AutoTargetRange)
        {
            continue;
        }

        const float ForwardDistance = Forward.Dot(ToCandidate);
        if (ForwardDistance <= 0.0f)
        {
            continue;
        }

        const float LateralDistanceSquared =
            DistanceSquared - (ForwardDistance * ForwardDistance);
        const float LateralDistance = std::sqrt(LateralDistanceSquared > 0.0f ? LateralDistanceSquared : 0.0f);
        const float AllowedLateralDistance =
            (ForwardDistance * ConeSlope) + MinCaptureRadius;
        if (LateralDistance > AllowedLateralDistance)
        {
            continue;
        }

        if (!BestActor ||
            Distance < BestDistance ||
            (std::abs(Distance - BestDistance) <= 1.0e-3f && LateralDistance < BestLateralDistance))
        {
            BestDistance = Distance;
            BestLateralDistance = LateralDistance;
            BestActor = Actor;
            BestLocation = CandidateLocation;
        }
    }

    if (OutTargetLocation)
    {
        *OutTargetLocation = BestLocation;
    }

    return BestActor;
}

int32 URagdollForceHitscanComponent::ApplyShockwaveBurstAtLocation(const FVector& Origin, float Strength) const
{
    UWorld* World = GetWorld();
    URagdollForceEmitterComponent* ForceEmitter = ResolveForceEmitter();
    if (!World || !ForceEmitter)
    {
        return 0;
    }

    int32 AffectedCount = 0;
    for (AActor* Actor : World->GetActors())
    {
        if (!HasValidRagdollTarget(Actor))
        {
            continue;
        }

        const FVector CandidateLocation = ResolveTargetLocation(Actor);
        const float Distance = FVector::Distance(Origin, CandidateLocation);
        if (Distance > ShockwaveRadius)
        {
            continue;
        }

        FRagdollShockwaveRequest Request;
        Request.Origin = Origin;
        Request.Radius = ShockwaveRadius;
        Request.Strength = Strength;
        Request.HoldTimeOverride = -1.0f;
        Request.PreferredPreset = PreferredPreset;
        Request.bUsePreferredPreset = true;
        Request.bAllowEscalationToFullBody = bAllowEscalationToFullBody;
        Request.bAllowWhileMoving = bAllowWhileMoving;

        if (ForceEmitter->ApplyShockwaveToActor(Actor, Request))
        {
            ++AffectedCount;
        }
    }

    return AffectedCount;
}

void URagdollForceHitscanComponent::DrawAutoTargetPreview() const
{
    if (!bDrawAimPreview || !CanProcessHitscanInput())
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FVector Start = FVector::ZeroVector;
    FVector Direction = FVector::ZeroVector;
    if (!TryGetActionRay(Start, Direction))
    {
        return;
    }

    FVector TargetLocation = FVector::ZeroVector;
    AActor* TargetActor = ResolveBestImpulseTargetActor(&TargetLocation);
    if (!TargetActor)
    {
        return;
    }

    const FVector End = TargetLocation;
    const float Duration = AimPreviewDuration;
    DrawDebugSphere(World, Start, AimPreviewSourceRadius, 10, FColor(0, 180, 255), Duration);
    if (bDrawAutoTargetTargetMarker)
    {
        DrawDebugSphere(World, End, AimPreviewImpactRadius, 12, FColor::Green(), Duration);
    }
}

void URagdollForceHitscanComponent::ProcessHitscanInput()
{
    if (!CanProcessHitscanInput())
    {
        return;
    }

    const FInputSystemSnapshot& Snapshot = GEngine->GetGameViewportClient()->GetGameInputSnapshot();
    if (WasActionPressed(Snapshot, ImpulseFireKey))
    {
        FireImpulseHitscan();
    }

    if (WasActionPressed(Snapshot, OwnerShockwaveKey))
    {
        FireOwnerCenteredShockwave();
    }
}

void URagdollForceHitscanComponent::LogHitscanResult(
    const char* RequestKind,
    const FVector* Location,
    int32 AffectedCount,
    bool bSucceeded,
    const char* TargetSourceName) const
{
    if (!bLogHitscan)
    {
        return;
    }

    UE_LOG("Ragdoll force %s. Component=%s Owner=%s TargetResolved=%s TargetSource=%s Location=(%.2f,%.2f,%.2f) Affected=%d Success=%s",
        RequestKind ? RequestKind : "Unknown",
        GetNameSafe(this),
        GetNameSafe(GetOwner()),
        Location ? "true" : "false",
        TargetSourceName ? TargetSourceName : "None",
        Location ? Location->X : 0.0f,
        Location ? Location->Y : 0.0f,
        Location ? Location->Z : 0.0f,
        AffectedCount,
        bSucceeded ? "true" : "false");
}
