#pragma once

#include "Component/ActorComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"

#include "Source/Engine/Component/Gameplay/RagdollForceEmitterComponent.generated.h"

class AActor;
class APawn;
class USkeletalMeshComponent;

UCLASS()
class URagdollForceEmitterComponent : public UActorComponent
{
public:
    GENERATED_BODY()
    URagdollForceEmitterComponent();
    ~URagdollForceEmitterComponent() override = default;

    void BeginPlay() override;

    bool ApplyImpulseToMesh(USkeletalMeshComponent* TargetMesh, const FRagdollImpulseRequest& Request);
    bool ApplyShockwaveToMesh(USkeletalMeshComponent* TargetMesh, const FRagdollShockwaveRequest& Request);

    bool ApplyImpulseToActor(AActor* TargetActor, const FRagdollImpulseRequest& Request);
    bool ApplyShockwaveToActor(AActor* TargetActor, const FRagdollShockwaveRequest& Request);

    bool ApplyImpulseToPawn(APawn* TargetPawn, const FRagdollImpulseRequest& Request);
    bool ApplyShockwaveToPawn(APawn* TargetPawn, const FRagdollShockwaveRequest& Request);

    bool ApplySimpleImpulseToPawn(
        APawn* TargetPawn,
        const FVector& WorldLocation,
        const FVector& WorldDirection,
        float Strength,
        const FName& HitBoneName = FName::None);

    bool ApplySimpleShockwaveToPawn(
        APawn* TargetPawn,
        const FVector& Origin,
        float Radius,
        float Strength);

private:
    USkeletalMeshComponent* ResolveTargetMeshFromActor(AActor* TargetActor) const;
    USkeletalMeshComponent* ResolveTargetMeshFromPawn(APawn* TargetPawn) const;

    void LogImpulseRequest(
        USkeletalMeshComponent* TargetMesh,
        const FRagdollImpulseRequest& Request,
        bool bSucceeded) const;
    void LogShockwaveRequest(
        USkeletalMeshComponent* TargetMesh,
        const FRagdollShockwaveRequest& Request,
        bool bSucceeded) const;

private:
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Force", DisplayName="Default Impulse Strength", Min=0.0f, Max=3.0f, Speed=0.01f)
    float DefaultImpulseStrength = 0.5f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Force", DisplayName="Default Shockwave Strength", Min=0.0f, Max=3.0f, Speed=0.01f)
    float DefaultShockwaveStrength = 1.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Force", DisplayName="Default Shockwave Radius", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float DefaultShockwaveRadius = 300.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Force", DisplayName="Default Preferred Preset", Enum=EPartialRagdollPreset)
    EPartialRagdollPreset DefaultPreferredPreset = EPartialRagdollPreset::UpperBody;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Force", DisplayName="Allow Escalation To Full Body")
    bool bDefaultAllowEscalationToFullBody = true;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Force", DisplayName="Allow While Moving")
    bool bDefaultAllowWhileMoving = true;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Force", DisplayName="Log Requests")
    bool bLogRequests = false;
};
