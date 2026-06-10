#include "PhysicsAssetRagdollTestComponent.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Character.h"
#include "Input/InputSystem.h"
#include "Physics/PhysicsAsset.h"

#include <windows.h>

namespace
{
    constexpr int32 DefaultEnableRagdollKey = VK_F13;
    constexpr int32 DefaultDisableRagdollKey = VK_F14;
    constexpr int32 DefaultRightArmHitReactionKey = VK_F15;
    constexpr int32 DefaultLeftArmHitReactionKey = VK_F16;
    constexpr int32 DefaultUpperBodyHitReactionKey = VK_F17;
    constexpr int32 DefaultRecoveryKey = VK_F18;
    constexpr int32 DefaultDumpStateKey = VK_F19;

    const char* GetAssetNameSafe(UPhysicsAsset* PhysicsAsset)
    {
        return PhysicsAsset ? PhysicsAsset->GetName().c_str() : "None";
    }

    const char* LexToString(ERagdollMode Mode)
    {
        switch (Mode)
        {
        case ERagdollMode::FullBody:
            return "FullBody";
        case ERagdollMode::Partial:
            return "Partial";
        default:
            return "None";
        }
    }

    const char* LexToString(EPartialRagdollPhase Phase)
    {
        switch (Phase)
        {
        case EPartialRagdollPhase::BlendingIn:
            return "BlendingIn";
        case EPartialRagdollPhase::Active:
            return "Active";
        case EPartialRagdollPhase::BlendingOut:
            return "BlendingOut";
        default:
            return "None";
        }
    }

    const char* LexToString(EPartialRagdollPreset Preset)
    {
        switch (Preset)
        {
        case EPartialRagdollPreset::UpperBody:
            return "UpperBody";
        case EPartialRagdollPreset::LeftArm:
            return "LeftArm";
        case EPartialRagdollPreset::RightArm:
            return "RightArm";
        case EPartialRagdollPreset::HeadNeck:
            return "HeadNeck";
        default:
            return "Unknown";
        }
    }

    const char* LexToString(ECharacterPhysicsOwnershipMode Mode)
    {
        switch (Mode)
        {
        case ECharacterPhysicsOwnershipMode::TransitionToRagdoll:
            return "TransitionToRagdoll";
        case ECharacterPhysicsOwnershipMode::RagdollDriven:
            return "RagdollDriven";
        case ECharacterPhysicsOwnershipMode::TransitionFromRagdoll:
            return "TransitionFromRagdoll";
        default:
            return "CharacterDriven";
        }
    }

    const char* LexToString(ECharacterPhysicsCollisionMode Mode)
    {
        switch (Mode)
        {
        case ECharacterPhysicsCollisionMode::FullRagdollOwned:
            return "FullRagdollOwned";
        case ECharacterPhysicsCollisionMode::PartialHybrid:
            return "PartialHybrid";
        default:
            return "CharacterDriven";
        }
    }

    const char* LexToString(ECharacterQueryCollisionMode Mode)
    {
        switch (Mode)
        {
        case ECharacterQueryCollisionMode::Disabled:
            return "Disabled";
        case ECharacterQueryCollisionMode::ReservedForFullRagdollProxy:
            return "ReservedForFullRagdollProxy";
        default:
            return "CharacterDriven";
        }
    }

    const char* LexToString(ECharacterGameplayOverlapOwnershipMode Mode)
    {
        switch (Mode)
        {
        case ECharacterGameplayOverlapOwnershipMode::None:
            return "None";
        case ECharacterGameplayOverlapOwnershipMode::FullRagdollQueryProxy:
            return "FullRagdollQueryProxy";
        case ECharacterGameplayOverlapOwnershipMode::PartialHybrid:
            return "PartialHybrid";
        default:
            return "CharacterDriven";
        }
    }

    const char* LexToString(EPhysicsCollisionRole Role)
    {
        switch (Role)
        {
        case EPhysicsCollisionRole::CharacterLocomotionProxy:
            return "CharacterLocomotionProxy";
        case EPhysicsCollisionRole::CharacterQueryProxy:
            return "CharacterQueryProxy";
        case EPhysicsCollisionRole::CharacterMeshPrimitive:
            return "CharacterMeshPrimitive";
        case EPhysicsCollisionRole::PartialReactionBody:
            return "PartialReactionBody";
        case EPhysicsCollisionRole::FullRagdollBody:
            return "FullRagdollBody";
        case EPhysicsCollisionRole::TriggerVolume:
            return "TriggerVolume";
        case EPhysicsCollisionRole::WorldStatic:
            return "WorldStatic";
        case EPhysicsCollisionRole::WorldDynamic:
            return "WorldDynamic";
        default:
            return "None";
        }
    }

    const char* LexToString(ECollisionEnabled Mode)
    {
        switch (Mode)
        {
        case ECollisionEnabled::NoCollision:
            return "NoCollision";
        case ECollisionEnabled::QueryOnly:
            return "QueryOnly";
        case ECollisionEnabled::PhysicsOnly:
            return "PhysicsOnly";
        case ECollisionEnabled::QueryAndPhysics:
            return "QueryAndPhysics";
        default:
            return "Unknown";
        }
    }
}

UPhysicsAssetRagdollTestComponent::UPhysicsAssetRagdollTestComponent()
{
    EnableRagdollKey = DefaultEnableRagdollKey;
    DisableRagdollKey = DefaultDisableRagdollKey;
    RightArmHitReactionKey = DefaultRightArmHitReactionKey;
    BeginRecoveryKey = DefaultRecoveryKey;
    DumpStateKey = DefaultDumpStateKey;
    UpperBodyHitReactionKey = DefaultUpperBodyHitReactionKey;
    LeftArmHitReactionKey = DefaultLeftArmHitReactionKey;
}

void UPhysicsAssetRagdollTestComponent::BeginPlay()
{
    UActorComponent::BeginPlay();

    ResolveTargetMeshComponent();
    LogControls();
    LogResolvedTarget(true);
}

void UPhysicsAssetRagdollTestComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
    (void)TickType;
    (void)ThisTickFunction;

    ResolveTargetMeshComponent();
    ProcessDebugInput();
    ProcessPeriodicActiveLogging(DeltaTime);
    MonitorStateTransitions();
}

void UPhysicsAssetRagdollTestComponent::ResolveTargetMeshComponent()
{
    USkeletalMeshComponent* CurrentTarget = TargetMeshComponent.Get();
    if (CurrentTarget && CurrentTarget->GetOwner() == GetOwner())
    {
        return;
    }

    TargetMeshComponent = FindTargetMeshComponent();
    ActiveLogTimer = 0.0f;

    if (TargetMeshComponent.Get())
    {
        bLoggedMissingTarget = false;
        bWasRagdollActive = TargetMeshComponent->IsRagdollActive();
        bWasRecovering = TargetMeshComponent->IsRecoveringFromRagdoll();
        LogResolvedTarget();
    }
    else if (!bLoggedMissingTarget)
    {
        LogResolvedTarget(true);
        bLoggedMissingTarget = true;
    }
}

USkeletalMeshComponent* UPhysicsAssetRagdollTestComponent::FindTargetMeshComponent() const
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return nullptr;
    }

    if (TargetMeshComponentName.empty())
    {
        return OwnerActor->GetComponentByClass<USkeletalMeshComponent>();
    }

    const TArray<UActorComponent*> Components = OwnerActor->GetComponents();
    for (UActorComponent* Component : Components)
    {
        USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Component);
        if (!SkeletalMeshComponent)
        {
            continue;
        }

        if (SkeletalMeshComponent->GetName() == TargetMeshComponentName)
        {
            return SkeletalMeshComponent;
        }
    }

    return nullptr;
}

bool UPhysicsAssetRagdollTestComponent::CanProcessDebugInput() const
{
    return InputSystem::Get().IsWindowFocused();
}

void UPhysicsAssetRagdollTestComponent::ProcessDebugInput()
{
    if (!CanProcessDebugInput())
    {
        return;
    }

    USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get();
    const InputSystem& Input = InputSystem::Get();

    if (Input.GetKeyDown(DumpStateKey))
    {
        LogCurrentState("ManualDump");
    }

    if (!MeshComponent)
    {
        return;
    }

    if (Input.GetKeyDown(EnableRagdollKey))
    {
        UE_LOG("[RagdollTest] EnableRequested Actor=%s MeshComponent=%s EffectivePhysicsAsset=%s",
            GetOwnerNameSafe(),
            GetComponentNameSafe(),
            GetAssetNameSafe(MeshComponent->GetEffectivePhysicsAsset()));

        const bool bUsesCharacterWrapper = Cast<ACharacter>(GetOwner()) != nullptr;
        const bool bEnabled = [this, MeshComponent]()
        {
            if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
            {
                return OwnerCharacter->EnterRagdoll();
            }
            return MeshComponent->EnableRagdollPhysics();
        }();
        const char* EventLabel = bEnabled
            ? (bUsesCharacterWrapper ? "EnableQueued" : "EnableSucceeded")
            : "EnableFailed";
        LogCurrentState(EventLabel);
        ActiveLogTimer = 0.0f;
    }

    if (Input.GetKeyDown(DisableRagdollKey))
    {
        if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
        {
            OwnerCharacter->ExitRagdoll();
        }
        else
        {
            MeshComponent->DisableRagdollPhysics();
        }
        LogCurrentState("DisableCompleted");
        ActiveLogTimer = 0.0f;
    }

    if (Input.GetKeyDown(BeginRecoveryKey))
    {
        UE_LOG("[RagdollTest] RecoveryRequested Actor=%s MeshComponent=%s",
            GetOwnerNameSafe(),
            GetComponentNameSafe());

        const bool bStarted = [this, MeshComponent]()
        {
            if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
            {
                return OwnerCharacter->BeginRagdollRecovery();
            }
            return MeshComponent->BeginRagdollRecovery();
        }();
        LogCurrentState(bStarted ? "RecoveryStarted" : "RecoveryIgnored");
        ActiveLogTimer = 0.0f;
    }

    if (Input.GetKeyDown(UpperBodyHitReactionKey))
    {
        TriggerFixedHitReaction(
            "UpperBodyHitReaction",
            FName("spine_02"),
            FVector(-1.0f, 0.0f, 0.15f),
            0.65f,
            EPartialRagdollPreset::UpperBody);
    }

    if (Input.GetKeyDown(LeftArmHitReactionKey))
    {
        TriggerFixedHitReaction(
            "LeftArmHitReaction",
            FName("upperarm_l"),
            FVector(-0.5f, 0.85f, 0.1f),
            0.58f,
            EPartialRagdollPreset::LeftArm);
    }

    if (Input.GetKeyDown(RightArmHitReactionKey))
    {
        TriggerFixedHitReaction(
            "RightArmHitReaction",
            FName("upperarm_r"),
            FVector(-0.5f, -0.85f, 0.1f),
            0.58f,
            EPartialRagdollPreset::RightArm);
    }
}

void UPhysicsAssetRagdollTestComponent::ProcessPeriodicActiveLogging(float DeltaTime)
{
    if (!bLogWhileRagdollActive)
    {
        return;
    }

    USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get();
    if (!MeshComponent || !MeshComponent->IsRagdollActive())
    {
        ActiveLogTimer = 0.0f;
        return;
    }

    const float SafeInterval = ActiveLogInterval > 0.1f ? ActiveLogInterval : 0.1f;
    ActiveLogTimer += DeltaTime;
    if (ActiveLogTimer < SafeInterval)
    {
        return;
    }

    ActiveLogTimer = 0.0f;
    LogCurrentState("ActiveTick");
}

void UPhysicsAssetRagdollTestComponent::MonitorStateTransitions()
{
    USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get();
    const bool bIsRagdollActive = MeshComponent && MeshComponent->IsRagdollActive();
    const bool bIsRecovering = MeshComponent && MeshComponent->IsRecoveringFromRagdoll();

    if (!bWasRagdollActive && bIsRagdollActive)
    {
        LogCurrentState("RagdollActive");
    }
    else if (bWasRecovering && !bIsRecovering)
    {
        LogCurrentState("RecoveryCompleted");
    }
    else if (bWasRagdollActive && !bIsRagdollActive && !bIsRecovering)
    {
        LogCurrentState("RagdollInactive");
    }

    bWasRagdollActive = bIsRagdollActive;
    bWasRecovering = bIsRecovering;
}

void UPhysicsAssetRagdollTestComponent::TriggerFixedHitReaction(
    const char* EventLabel,
    const FName& HitBoneName,
    const FVector& HitDirection,
    float Strength,
    EPartialRagdollPreset PreferredPreset)
{
    USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get();
    if (!MeshComponent)
    {
        LogCurrentState(EventLabel ? EventLabel : "HitReactionMissingMesh");
        return;
    }

    FPartialRagdollHitReactionRequest Request;
    Request.HitBoneName = HitBoneName;
    Request.HitWorldLocation = MeshComponent->GetWorldLocation();
    Request.HitWorldDirection = HitDirection;
    Request.Strength = Strength;
    Request.PreferredPreset = PreferredPreset;
    Request.bUsePreferredPreset = true;
    Request.bAllowEscalationToFullBody = true;

    UE_LOG("[RagdollTest] HitReactionRequested Actor=%s MeshComponent=%s HitBone=%s Strength=%.2f Direction=(%.2f,%.2f,%.2f)",
        GetOwnerNameSafe(),
        GetComponentNameSafe(),
        HitBoneName.ToString().c_str(),
        Strength,
        HitDirection.X,
        HitDirection.Y,
        HitDirection.Z);

    const bool bTriggered = MeshComponent->TriggerPartialRagdollHitReaction(Request);
    LogCurrentState(bTriggered ? EventLabel : "HitReactionRejected");
    ActiveLogTimer = 0.0f;
}

void UPhysicsAssetRagdollTestComponent::LogControls() const
{
    UE_LOG("[RagdollTest] Controls: F13=Enable Ragdoll, F14=Disable Ragdoll, F15=RightArm HitReaction, F16=LeftArm HitReaction, F17=UpperBody HitReaction, F18=Begin Recovery, F19=Dump State. Actor=%s RequestedMesh=%s",
        GetOwnerNameSafe(),
        TargetMeshComponentName.empty() ? "<First SkeletalMeshComponent>" : TargetMeshComponentName.c_str());
}

void UPhysicsAssetRagdollTestComponent::LogResolvedTarget(bool bForceLogFailure)
{
    if (USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get())
    {
        UE_LOG("[RagdollTest] Resolved target mesh component. Actor=%s MeshComponent=%s",
            GetOwnerNameSafe(),
            MeshComponent->GetName().c_str());
        return;
    }

    if (bForceLogFailure)
    {
        UE_LOG("[RagdollTest] No valid target skeletal mesh component found. Actor=%s RequestedMesh=%s",
            GetOwnerNameSafe(),
            TargetMeshComponentName.empty() ? "<First SkeletalMeshComponent>" : TargetMeshComponentName.c_str());
    }
}

void UPhysicsAssetRagdollTestComponent::LogCurrentState(const char* EventLabel) const
{
    USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get();
    ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
    UPhysicsAsset* EffectivePhysicsAsset = MeshComponent ? MeshComponent->GetEffectivePhysicsAsset() : nullptr;
    UPhysicsAsset* ActivePhysicsAsset = MeshComponent ? MeshComponent->GetActivePhysicsAsset() : nullptr;
    const FVector LastHitDirection =
        MeshComponent ? MeshComponent->GetLastPartialHitReactionDirection() : FVector::ZeroVector;

    UE_LOG("[RagdollTest] %s Actor=%s MeshComponent=%s EffectivePhysicsAsset=%s ActivePhysicsAsset=%s Mode=%s Active=%s PhysicsPose=%s Recovering=%s CharacterOwnership=%s CharacterPhysicsCollision=%s CharacterQueryCollision=%s OverlapOwnership=%s CapsuleCollision=%s MeshCollision=%s CapsuleRole=%s MeshRole=%s PartialBodyRole=%s CapsuleOverlapOwner=%s MeshOverlapOwner=%s PartialBodiesOverlapOwner=%s AwaitingRestore=%s FullQueryProxyActive=%s PartialRoot=%s PartialPhase=%s Hold=%.2f PendingBlendOut=%s PartialSelfSuppression=%s LiveBodies=%d LiveConstraints=%d BlendWeight=%.2f LastEvent=%s LastDecision=%s LastReason=%s LastPreset=%s LastHitBone=%s LastRoot=%s LastTarget=%s LastStrength=%.2f LastHold=%.2f LastImpulse=%.2f LastDirection=(%.2f,%.2f,%.2f) EscalationCandidate=%s",
        EventLabel ? EventLabel : "State",
        GetOwnerNameSafe(),
        GetComponentNameSafe(),
        GetAssetNameSafe(EffectivePhysicsAsset),
        GetAssetNameSafe(ActivePhysicsAsset),
        MeshComponent ? LexToString(MeshComponent->GetRagdollMode()) : "None",
        (MeshComponent && MeshComponent->IsRagdollActive()) ? "true" : "false",
        (MeshComponent && MeshComponent->IsUsingPhysicsAssetPose()) ? "true" : "false",
        (MeshComponent && MeshComponent->IsRecoveringFromRagdoll()) ? "true" : "false",
        OwnerCharacter ? LexToString(OwnerCharacter->GetPhysicsOwnershipMode()) : "None",
        OwnerCharacter ? LexToString(OwnerCharacter->GetCharacterPhysicsCollisionMode()) : "None",
        OwnerCharacter ? LexToString(OwnerCharacter->GetCharacterQueryCollisionMode()) : "None",
        OwnerCharacter ? LexToString(OwnerCharacter->GetGameplayOverlapOwnershipMode()) : "None",
        OwnerCharacter ? LexToString(OwnerCharacter->GetCurrentCapsuleCollisionEnabled()) : "None",
        OwnerCharacter ? LexToString(OwnerCharacter->GetCurrentMeshCollisionEnabled()) : "None",
        OwnerCharacter ? LexToString(OwnerCharacter->GetCapsuleCollisionRole()) : "None",
        OwnerCharacter ? LexToString(OwnerCharacter->GetMeshCollisionRole()) : "None",
        OwnerCharacter ? LexToString(OwnerCharacter->GetPartialReactionBodyCollisionRole()) : "None",
        (OwnerCharacter && OwnerCharacter->IsCapsuleGameplayOverlapOwner()) ? "true" : "false",
        (OwnerCharacter && OwnerCharacter->IsMeshGameplayOverlapOwner()) ? "true" : "false",
        (OwnerCharacter && OwnerCharacter->ArePartialReactionBodiesGameplayOverlapOwners()) ? "true" : "false",
        (OwnerCharacter && OwnerCharacter->IsAwaitingRagdollRecoveryRestore()) ? "true" : "false",
        (OwnerCharacter && OwnerCharacter->IsUsingFullRagdollQueryProxy()) ? "true" : "false",
        MeshComponent ? MeshComponent->GetActivePartialRagdollRootBoneName().ToString().c_str() : "None",
        MeshComponent ? LexToString(MeshComponent->GetPartialRagdollPhase()) : "None",
        MeshComponent ? MeshComponent->GetPartialRagdollHoldRemaining() : 0.0f,
        (MeshComponent && MeshComponent->IsPartialRagdollBlendOutPending()) ? "true" : "false",
        (MeshComponent && MeshComponent->IsPartialRagdollSelfSuppressionActive()) ? "true" : "false",
        MeshComponent ? MeshComponent->GetLiveRagdollBodyCount() : 0,
        MeshComponent ? MeshComponent->GetLiveRagdollConstraintCount() : 0,
        MeshComponent ? MeshComponent->GetPhysicsAssetBlendWeight() : 0.0f,
        MeshComponent ? ::LexToString(MeshComponent->GetLastRagdollReactionEventKind()) : "DirectHit",
        MeshComponent ? ::LexToString(MeshComponent->GetLastRagdollReactionType()) : "None",
        MeshComponent ? ::LexToString(MeshComponent->GetLastRagdollReactionDecisionReason()) : "None",
        MeshComponent ? LexToString(MeshComponent->GetLastPartialHitReactionPreset()) : "Unknown",
        MeshComponent ? MeshComponent->GetLastPartialHitReactionHitBoneName().ToString().c_str() : "None",
        MeshComponent ? MeshComponent->GetLastPartialHitReactionRootBoneName().ToString().c_str() : "None",
        MeshComponent ? MeshComponent->GetLastPartialHitReactionTargetBoneName().ToString().c_str() : "None",
        MeshComponent ? MeshComponent->GetLastPartialHitReactionStrength() : 0.0f,
        MeshComponent ? MeshComponent->GetLastPartialHitReactionHoldTime() : 0.0f,
        MeshComponent ? MeshComponent->GetLastPartialHitReactionImpulseMagnitude() : 0.0f,
        LastHitDirection.X,
        LastHitDirection.Y,
        LastHitDirection.Z,
        (MeshComponent && MeshComponent->WasLastPartialHitReactionEscalationCandidate()) ? "true" : "false");

    if (OwnerCharacter && OwnerCharacter->IsUsingFullRagdollQueryProxy())
    {
        UE_LOG("[RagdollTest] FullRagdollQueryProxy Actor=%s Ownership=%s PhysicsCollision=%s QueryCollision=%s OverlapOwnership=%s Capsule=%s Mesh=%s CapsuleRole=%s PartialBodyRole=%s CapsuleOverlapOwner=%s AwaitingRestore=%s",
            GetOwnerNameSafe(),
            LexToString(OwnerCharacter->GetPhysicsOwnershipMode()),
            LexToString(OwnerCharacter->GetCharacterPhysicsCollisionMode()),
            LexToString(OwnerCharacter->GetCharacterQueryCollisionMode()),
            LexToString(OwnerCharacter->GetGameplayOverlapOwnershipMode()),
            LexToString(OwnerCharacter->GetCurrentCapsuleCollisionEnabled()),
            LexToString(OwnerCharacter->GetCurrentMeshCollisionEnabled()),
            LexToString(OwnerCharacter->GetCapsuleCollisionRole()),
            LexToString(OwnerCharacter->GetPartialReactionBodyCollisionRole()),
            OwnerCharacter->IsCapsuleGameplayOverlapOwner() ? "true" : "false",
            OwnerCharacter->IsAwaitingRagdollRecoveryRestore() ? "true" : "false");
    }

    if (OwnerCharacter &&
        OwnerCharacter->GetCharacterPhysicsCollisionMode() == ECharacterPhysicsCollisionMode::PartialHybrid)
    {
        UE_LOG("[RagdollTest] PartialHybridCollisionPath Actor=%s CharacterOwnership=%s OverlapOwnership=%s Capsule=%s Mesh=%s CapsuleRole=%s PartialBodyRole=%s CapsuleOverlapOwner=%s PartialBodiesOverlapOwner=%s PartialMode=%s PartialPhase=%s PartialSelfSuppression=%s",
            GetOwnerNameSafe(),
            LexToString(OwnerCharacter->GetPhysicsOwnershipMode()),
            LexToString(OwnerCharacter->GetGameplayOverlapOwnershipMode()),
            LexToString(OwnerCharacter->GetCurrentCapsuleCollisionEnabled()),
            LexToString(OwnerCharacter->GetCurrentMeshCollisionEnabled()),
            LexToString(OwnerCharacter->GetCapsuleCollisionRole()),
            LexToString(OwnerCharacter->GetPartialReactionBodyCollisionRole()),
            OwnerCharacter->IsCapsuleGameplayOverlapOwner() ? "true" : "false",
            OwnerCharacter->ArePartialReactionBodiesGameplayOverlapOwners() ? "true" : "false",
            MeshComponent ? LexToString(MeshComponent->GetRagdollMode()) : "None",
            MeshComponent ? LexToString(MeshComponent->GetPartialRagdollPhase()) : "None",
            (MeshComponent && MeshComponent->IsPartialRagdollSelfSuppressionActive()) ? "true" : "false");
    }
}

const char* UPhysicsAssetRagdollTestComponent::GetComponentNameSafe() const
{
    if (USkeletalMeshComponent* MeshComponent = TargetMeshComponent.Get())
    {
        return MeshComponent->GetName().c_str();
    }
    return "None";
}

const char* UPhysicsAssetRagdollTestComponent::GetOwnerNameSafe() const
{
    if (AActor* OwnerActor = GetOwner())
    {
        return OwnerActor->GetName().c_str();
    }
    return "None";
}
