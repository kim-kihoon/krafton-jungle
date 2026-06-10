#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/Debug/PhysicsAssetRagdollTestComponent.generated.h"

class USkeletalMeshComponent;
enum class EPartialRagdollPreset : uint8;

// Temporary validation hook:
// this component exists only to drive the already-authored PhysicsAsset ragdoll flow
// in PIE without introducing permanent gameplay orchestration.
UCLASS()
class UPhysicsAssetRagdollTestComponent : public UActorComponent
{
public:
    GENERATED_BODY()
    UPhysicsAssetRagdollTestComponent();
    ~UPhysicsAssetRagdollTestComponent() override = default;

    void BeginPlay() override;

protected:
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
    void ResolveTargetMeshComponent();
    USkeletalMeshComponent* FindTargetMeshComponent() const;

    bool CanProcessDebugInput() const;
    void ProcessDebugInput();
    void ProcessPeriodicActiveLogging(float DeltaTime);
    void MonitorStateTransitions();
    void TriggerFixedHitReaction(
        const char* EventLabel,
        const FName& HitBoneName,
        const FVector& HitDirection,
        float Strength,
        EPartialRagdollPreset PreferredPreset);

    void LogControls() const;
    void LogResolvedTarget(bool bForceLogFailure = false);
    void LogCurrentState(const char* EventLabel) const;

    const char* GetComponentNameSafe() const;
    const char* GetOwnerNameSafe() const;

private:
    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Target Mesh Component Name")
    FString TargetMeshComponentName;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Enable Key")
    int32 EnableRagdollKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Disable Key")
    int32 DisableRagdollKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Recovery Key")
    int32 BeginRecoveryKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Dump State Key")
    int32 DumpStateKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Upper Body Hit Reaction Key")
    int32 UpperBodyHitReactionKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Left Arm Hit Reaction Key")
    int32 LeftArmHitReactionKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Right Arm Hit Reaction Key")
    int32 RightArmHitReactionKey = 0;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Log While Ragdoll Active")
    bool bLogWhileRagdollActive = false;

    UPROPERTY(Edit, Save, Category="Physics|Ragdoll Test", DisplayName="Active Log Interval", Min=0.1f, Max=0.0f, Speed=0.1f)
    float ActiveLogInterval = 1.0f;

    TWeakObjectPtr<USkeletalMeshComponent> TargetMeshComponent;
    bool bLoggedMissingTarget = false;
    float ActiveLogTimer = 0.0f;
    bool bWasRagdollActive = false;
    bool bWasRecovering = false;
};
