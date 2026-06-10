#include "RagdollForceEmitterComponent.h"

#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Character.h"
#include "GameFramework/Pawn/Pawn.h"

namespace
{
    const char* GetObjectNameSafe(const UObject* Object)
    {
        return Object ? Object->GetName().c_str() : "None";
    }

    const char* GetActorNameSafe(const AActor* Actor)
    {
        return Actor ? Actor->GetName().c_str() : "None";
    }
}

URagdollForceEmitterComponent::URagdollForceEmitterComponent()
{
    SetComponentTickEnabled(false);
}

void URagdollForceEmitterComponent::BeginPlay()
{
    UActorComponent::BeginPlay();
    SetComponentTickEnabled(false);
}

bool URagdollForceEmitterComponent::ApplyImpulseToMesh(
    USkeletalMeshComponent* TargetMesh,
    const FRagdollImpulseRequest& Request)
{
    const bool bSucceeded = TargetMesh && TargetMesh->ApplyRagdollImpulse(Request);
    LogImpulseRequest(TargetMesh, Request, bSucceeded);
    return bSucceeded;
}

bool URagdollForceEmitterComponent::ApplyShockwaveToMesh(
    USkeletalMeshComponent* TargetMesh,
    const FRagdollShockwaveRequest& Request)
{
    const bool bSucceeded = TargetMesh && TargetMesh->ApplyRagdollShockwave(Request);
    LogShockwaveRequest(TargetMesh, Request, bSucceeded);
    return bSucceeded;
}

bool URagdollForceEmitterComponent::ApplyImpulseToActor(
    AActor* TargetActor,
    const FRagdollImpulseRequest& Request)
{
    return ApplyImpulseToMesh(ResolveTargetMeshFromActor(TargetActor), Request);
}

bool URagdollForceEmitterComponent::ApplyShockwaveToActor(
    AActor* TargetActor,
    const FRagdollShockwaveRequest& Request)
{
    return ApplyShockwaveToMesh(ResolveTargetMeshFromActor(TargetActor), Request);
}

bool URagdollForceEmitterComponent::ApplyImpulseToPawn(
    APawn* TargetPawn,
    const FRagdollImpulseRequest& Request)
{
    return ApplyImpulseToMesh(ResolveTargetMeshFromPawn(TargetPawn), Request);
}

bool URagdollForceEmitterComponent::ApplyShockwaveToPawn(
    APawn* TargetPawn,
    const FRagdollShockwaveRequest& Request)
{
    return ApplyShockwaveToMesh(ResolveTargetMeshFromPawn(TargetPawn), Request);
}

bool URagdollForceEmitterComponent::ApplySimpleImpulseToPawn(
    APawn* TargetPawn,
    const FVector& WorldLocation,
    const FVector& WorldDirection,
    float Strength,
    const FName& HitBoneName)
{
    FRagdollImpulseRequest Request;
    Request.HitBoneName = HitBoneName;
    Request.WorldLocation = WorldLocation;
    Request.WorldDirection = WorldDirection;
    Request.Strength = Strength;
    Request.PreferredPreset = DefaultPreferredPreset;
    Request.bUsePreferredPreset = true;
    Request.bAllowEscalationToFullBody = bDefaultAllowEscalationToFullBody;
    Request.bAllowWhileMoving = bDefaultAllowWhileMoving;
    return ApplyImpulseToPawn(TargetPawn, Request);
}

bool URagdollForceEmitterComponent::ApplySimpleShockwaveToPawn(
    APawn* TargetPawn,
    const FVector& Origin,
    float Radius,
    float Strength)
{
    FRagdollShockwaveRequest Request;
    Request.Origin = Origin;
    Request.Radius = Radius;
    Request.Strength = Strength;
    Request.PreferredPreset = DefaultPreferredPreset;
    Request.bUsePreferredPreset = true;
    Request.bAllowEscalationToFullBody = bDefaultAllowEscalationToFullBody;
    Request.bAllowWhileMoving = bDefaultAllowWhileMoving;
    return ApplyShockwaveToPawn(TargetPawn, Request);
}

USkeletalMeshComponent* URagdollForceEmitterComponent::ResolveTargetMeshFromActor(AActor* TargetActor) const
{
    if (!TargetActor)
    {
        return nullptr;
    }

    if (ACharacter* Character = Cast<ACharacter>(TargetActor))
    {
        if (USkeletalMeshComponent* CharacterMesh = Character->GetMesh())
        {
            return CharacterMesh;
        }
    }

    return TargetActor->GetComponentByClass<USkeletalMeshComponent>();
}

USkeletalMeshComponent* URagdollForceEmitterComponent::ResolveTargetMeshFromPawn(APawn* TargetPawn) const
{
    return ResolveTargetMeshFromActor(TargetPawn);
}

void URagdollForceEmitterComponent::LogImpulseRequest(
    USkeletalMeshComponent* TargetMesh,
    const FRagdollImpulseRequest& Request,
    bool bSucceeded) const
{
    if (!bLogRequests)
    {
        return;
    }

    UE_LOG("Ragdoll force impulse emitted. Emitter=%s Owner=%s TargetMesh=%s TargetActor=%s Bone=%s Strength=%.2f Success=%s",
        GetObjectNameSafe(this),
        GetActorNameSafe(GetOwner()),
        GetObjectNameSafe(TargetMesh),
        GetActorNameSafe(TargetMesh ? TargetMesh->GetOwner() : nullptr),
        Request.HitBoneName.ToString().c_str(),
        Request.Strength,
        bSucceeded ? "true" : "false");
}

void URagdollForceEmitterComponent::LogShockwaveRequest(
    USkeletalMeshComponent* TargetMesh,
    const FRagdollShockwaveRequest& Request,
    bool bSucceeded) const
{
    if (!bLogRequests)
    {
        return;
    }

    UE_LOG("Ragdoll force shockwave emitted. Emitter=%s Owner=%s TargetMesh=%s TargetActor=%s Radius=%.2f Strength=%.2f Success=%s",
        GetObjectNameSafe(this),
        GetActorNameSafe(GetOwner()),
        GetObjectNameSafe(TargetMesh),
        GetActorNameSafe(TargetMesh ? TargetMesh->GetOwner() : nullptr),
        Request.Radius,
        Request.Strength,
        bSucceeded ? "true" : "false");
}
