#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Component/Primitive/SkeletalMeshComponent.h"

#include "Source/Engine/Component/Gameplay/RagdollForceHitscanComponent.generated.h"

class AActor;
class URagdollForceEmitterComponent;
struct FInputSystemSnapshot;
enum class EPartialRagdollPreset : uint8;

UCLASS()
class URagdollForceHitscanComponent : public UActorComponent
{
public:
    GENERATED_BODY()
    URagdollForceHitscanComponent();
    ~URagdollForceHitscanComponent() override = default;

    void BeginPlay() override;

protected:
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

public:
    bool FireImpulseHitscan();
    bool FireOwnerCenteredShockwave();

private:
    bool WasActionPressed(const FInputSystemSnapshot& Snapshot, int32 KeyCode) const;
    URagdollForceEmitterComponent* ResolveForceEmitter() const;
    bool CanProcessHitscanInput() const;
    bool TryGetActionRay(FVector& OutOrigin, FVector& OutDirection) const;
    USkeletalMeshComponent* ResolveTargetMesh(AActor* CandidateActor) const;
    FVector ResolveTargetLocation(AActor* CandidateActor) const;
    bool HasValidRagdollTarget(AActor* CandidateActor) const;
    AActor* ResolveBestImpulseTargetActor(FVector* OutTargetLocation = nullptr) const;
    int32 ApplyShockwaveBurstAtLocation(const FVector& Origin, float Strength) const;
    void DrawAutoTargetPreview() const;
    void ProcessHitscanInput();
    void LogHitscanResult(
        const char* RequestKind,
        const FVector* Location,
        int32 AffectedCount,
        bool bSucceeded,
        const char* TargetSourceName = nullptr) const;

private:
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Enable Input Firing")
    bool bEnableInputFiring = true;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Impulse Fire Key")
    int32 ImpulseFireKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Owner Shockwave Key")
    int32 OwnerShockwaveKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Impulse Strength", Min=0.0f, Max=3.0f, Speed=0.01f)
    float ImpulseStrength = 1.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Owner Shockwave Strength", Min=0.0f, Max=3.0f, Speed=0.01f)
    float OwnerShockwaveStrength = 1.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Shockwave Radius", Min=0.0f, Max=10000.0f, Speed=1.0f)
    float ShockwaveRadius = 300.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Auto Target Range", Min=0.0f, Max=100000.0f, Speed=1.0f)
    float AutoTargetRange = 1800.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Auto Target Max Angle Degrees", Min=0.0f, Max=180.0f, Speed=1.0f)
    float AutoTargetMaxAngleDegrees = 35.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Preferred Preset", Enum=EPartialRagdollPreset)
    EPartialRagdollPreset PreferredPreset = EPartialRagdollPreset::UpperBody;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Allow Escalation To Full Body")
    bool bAllowEscalationToFullBody = true;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Allow While Moving")
    bool bAllowWhileMoving = true;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Aim Origin Height Offset", Min=-500.0f, Max=500.0f, Speed=0.1f)
    float AimOriginHeightOffset = 60.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Draw Auto Target Preview")
    bool bDrawAimPreview = true;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Draw Auto Target Target Marker")
    bool bDrawAutoTargetTargetMarker = true;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Preview Duration", Min=0.0f, Max=0.25f, Speed=0.01f)
    float AimPreviewDuration = 0.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Target Preview Radius", Min=0.0f, Max=100.0f, Speed=0.1f)
    float AimPreviewImpactRadius = 6.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Source Preview Radius", Min=0.0f, Max=100.0f, Speed=0.1f)
    float AimPreviewSourceRadius = 4.0f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Shockwave Preview Duration", Min=0.0f, Max=5.0f, Speed=0.01f)
    float ShockwavePreviewDuration = 0.4f;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Hitscan", DisplayName="Log Hitscan")
    bool bLogHitscan = false;

    TWeakObjectPtr<URagdollForceEmitterComponent> CachedForceEmitter;
};
