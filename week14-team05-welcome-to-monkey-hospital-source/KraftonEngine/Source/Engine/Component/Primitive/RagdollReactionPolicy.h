#pragma once

#include <algorithm>

#include "Math/Vector.h"
#include "Object/FName.h"

#ifndef KRAFTON_SKELETAL_MESH_RAGDOLL_TYPES_DEFINED
enum class ERagdollMode : uint8;
enum class EPartialRagdollPhase : uint8;
enum class ERagdollRecoveryPhase : uint8;
enum class EPartialRagdollPreset : uint8;
#endif
enum class ECharacterPhysicsOwnershipMode : uint8;
enum class ECharacterPhysicsCollisionMode : uint8;
enum class ECharacterQueryCollisionMode : uint8;
enum class ECharacterGameplayOverlapOwnershipMode : uint8;
enum class EPhysicsCollisionRole : uint8;

enum class ERagdollReactionEventKind : uint8
{
    DirectHit,
    RadialShockwave,
    ScriptedForce,
};

enum class ERagdollReactionType : uint8
{
    None,
    ImpulseOnly,
    Partial,
    FullBody,
    RefreshPartial,
    EscalateToFull,
    Deferred,
    RecoveryLimited,
};

enum class ERagdollReactionDecisionReason : uint8
{
    None,
    InvalidRequest,
    CouldNotResolvePartialSelection,
    OwnerMoving,
    RecoveryProtectedWindow,
    RecoveryImpulseOnly,
    RecoveryEscalatedToFull,
    FullBodyAlreadyActive,
    ExistingPartialRefreshed,
    ExistingPartialRejected,
    ExistingPartialEscalated,
    ShockwaveImpulseOnly,
    ShockwaveFullBody,
    FullBodyThreshold,
    PartialSelected,
    RedundantRequest,
};

struct FRagdollReactionTuning
{
    float DefaultStrengthWhenUnspecified = 0.5f;
    float BaseImpulseMagnitude = 120.0f;
    float AdditionalImpulseMagnitude = 240.0f;
    float OverdriveImpulseMagnitudeScale = 600.0f;
    float FullBodyLaunchImpulseMagnitude = 900.0f;
    float HoldTimeBaseScale = 0.75f;
    float HoldTimeStrengthScale = 0.75f;
    float FullBodyThreshold = 0.85f;
    float EscalationThreshold = 0.80f;
    float ShockwaveFullThreshold = 0.90f;
    float ShockwaveImpulseThreshold = 0.45f;
    float RecoveryEscalationThreshold = 0.90f;
    float RecoveryImpulseThreshold = 0.55f;
};

struct FRagdollReactionRequest
{
    ERagdollReactionEventKind EventKind = ERagdollReactionEventKind::DirectHit;
    EPartialRagdollPreset PreferredPreset{};
    FName HitBoneName = FName::None;
    FVector HitWorldLocation = FVector::ZeroVector;
    FVector HitWorldDirection = FVector::ZeroVector;
    float Strength = 0.5f;
    float HoldTimeOverride = -1.0f;
    bool bUsePreferredPreset = false;
    bool bAllowEscalationToFullBody = false;
    bool bAllowWhileMoving = true;

    bool HasHitBone() const
    {
        return HitBoneName != FName::None;
    }

    bool HasHoldTimeOverride() const
    {
        return HoldTimeOverride >= 0.0f;
    }
};

struct FRagdollReactionContext
{
    ERagdollMode CurrentMode{};
    EPartialRagdollPhase CurrentPartialPhase{};
    ERagdollRecoveryPhase CurrentRecoveryPhase{};
    ECharacterPhysicsOwnershipMode CharacterOwnershipMode{};
    ECharacterPhysicsCollisionMode CharacterPhysicsCollisionMode{};
    ECharacterQueryCollisionMode CharacterQueryCollisionMode{};
    ECharacterGameplayOverlapOwnershipMode CharacterGameplayOverlapMode{};
    EPhysicsCollisionRole CapsuleRole{};
    EPhysicsCollisionRole MeshRole{};
    EPhysicsCollisionRole PartialBodyRole{};
    bool bHasLivePhysicsBodies = false;
    bool bIsRecovering = false;
    bool bOwnerMoving = false;
    bool bPendingPartialBlendOut = false;
    bool bPartialSelfSuppressionActive = false;
    bool bHasResolvedPartialSelection = false;
    EPartialRagdollPreset ResolvedPreset{};
    FName ResolvedRootBoneName = FName::None;
    bool bResolvedIncludeDescendants = true;
    bool bSamePartialSelectionActive = false;
    float DefaultPartialHoldTime = 0.0f;
    FName ActivePartialRootBoneName = FName::None;
};

struct FRagdollReactionDecision
{
    ERagdollReactionType Type = ERagdollReactionType::None;
    ERagdollReactionDecisionReason Reason = ERagdollReactionDecisionReason::None;
    EPartialRagdollPreset ResolvedPreset{};
    FName ResolvedRootBoneName = FName::None;
    bool bIncludeDescendants = true;
    float NormalizedStrength = 0.0f;
    float HoldTime = 0.0f;
    float ImpulseMagnitude = 0.0f;
    FVector ImpulseDirection = FVector::ZeroVector;
    bool bApplyImpulse = false;
    bool bEscalationCandidate = false;

    bool HasResolvedPartialSelection() const
    {
        return ResolvedRootBoneName != FName::None;
    }
};

float NormalizeRagdollReactionStrength(const FRagdollReactionRequest& Request, const FRagdollReactionTuning& Tuning);
FRagdollReactionDecision EvaluateRagdollReactionPolicy(
    const FRagdollReactionContext& Context,
    const FRagdollReactionRequest& Request,
    const FRagdollReactionTuning& Tuning);

const char* LexToString(ERagdollReactionEventKind EventKind);
const char* LexToString(ERagdollReactionType ReactionType);
const char* LexToString(ERagdollReactionDecisionReason Reason);

inline float NormalizeRagdollReactionStrength(const FRagdollReactionRequest& Request, const FRagdollReactionTuning& Tuning)
{
    const float RawStrength =
        Request.Strength > 0.0f
            ? Request.Strength
            : Tuning.DefaultStrengthWhenUnspecified;
    return std::clamp(RawStrength, 0.0f, 1.0f);
}

inline float GetRagdollReactionRawStrength(const FRagdollReactionRequest& Request, const FRagdollReactionTuning& Tuning)
{
    return Request.Strength > 0.0f
        ? Request.Strength
        : Tuning.DefaultStrengthWhenUnspecified;
}

inline void ApplyFullBodyLaunchImpulseTuning(
    FRagdollReactionDecision& Decision,
    const FRagdollReactionTuning& Tuning)
{
    if (!Decision.bApplyImpulse)
    {
        return;
    }

    if (Decision.Type == ERagdollReactionType::FullBody ||
        Decision.Type == ERagdollReactionType::EscalateToFull)
    {
        Decision.ImpulseMagnitude = (std::max)(Decision.ImpulseMagnitude, Tuning.FullBodyLaunchImpulseMagnitude);
    }
}

inline FRagdollReactionDecision EvaluateRagdollReactionPolicy(
    const FRagdollReactionContext& Context,
    const FRagdollReactionRequest& Request,
    const FRagdollReactionTuning& Tuning)
{
    auto ResolveImpulseDirection = [](const FVector& Direction)
    {
        if (Direction.Dot(Direction) <= 1.0e-6f)
        {
            return FVector::ForwardVector;
        }

        return Direction.Normalized();
    };

    FRagdollReactionDecision Decision;
    const float RawStrength = GetRagdollReactionRawStrength(Request, Tuning);
    const float OverdriveStrength = (std::max)(0.0f, RawStrength - 1.0f);
    Decision.ResolvedPreset = Context.ResolvedPreset;
    Decision.ResolvedRootBoneName = Context.ResolvedRootBoneName;
    Decision.bIncludeDescendants = Context.bResolvedIncludeDescendants;
    Decision.NormalizedStrength = NormalizeRagdollReactionStrength(Request, Tuning);
    Decision.HoldTime = Request.HasHoldTimeOverride()
        ? Request.HoldTimeOverride
        : (Context.DefaultPartialHoldTime * (Tuning.HoldTimeBaseScale + Tuning.HoldTimeStrengthScale * Decision.NormalizedStrength));
    Decision.ImpulseMagnitude =
        Tuning.BaseImpulseMagnitude +
        Tuning.AdditionalImpulseMagnitude * Decision.NormalizedStrength +
        Tuning.OverdriveImpulseMagnitudeScale * OverdriveStrength;
    Decision.ImpulseDirection = ResolveImpulseDirection(Request.HitWorldDirection);
    Decision.bApplyImpulse = Decision.ImpulseMagnitude > 1.0e-3f;
    Decision.bEscalationCandidate =
        Request.bAllowEscalationToFullBody && Decision.NormalizedStrength >= Tuning.FullBodyThreshold;

    if (!Request.bAllowWhileMoving && Context.bOwnerMoving)
    {
        Decision.Type = ERagdollReactionType::None;
        Decision.Reason = ERagdollReactionDecisionReason::OwnerMoving;
        return Decision;
    }

    if (Context.bIsRecovering)
    {
        if (Request.bAllowEscalationToFullBody && Decision.NormalizedStrength >= Tuning.RecoveryEscalationThreshold)
        {
            Decision.Type = ERagdollReactionType::EscalateToFull;
            Decision.Reason = ERagdollReactionDecisionReason::RecoveryEscalatedToFull;
            ApplyFullBodyLaunchImpulseTuning(Decision, Tuning);
            return Decision;
        }

        if (Context.bHasLivePhysicsBodies && Decision.NormalizedStrength >= Tuning.RecoveryImpulseThreshold)
        {
            Decision.Type = ERagdollReactionType::RecoveryLimited;
            Decision.Reason = ERagdollReactionDecisionReason::RecoveryImpulseOnly;
            return Decision;
        }

        Decision.Type = ERagdollReactionType::None;
        Decision.Reason = ERagdollReactionDecisionReason::RecoveryProtectedWindow;
        return Decision;
    }

    if (Request.EventKind == ERagdollReactionEventKind::RadialShockwave)
    {
        if (Request.bAllowEscalationToFullBody && Decision.NormalizedStrength >= Tuning.ShockwaveFullThreshold)
        {
            Decision.Type =
                Context.CurrentMode == ERagdollMode::Partial
                    ? ERagdollReactionType::EscalateToFull
                    : ERagdollReactionType::FullBody;
            Decision.Reason = ERagdollReactionDecisionReason::ShockwaveFullBody;
            ApplyFullBodyLaunchImpulseTuning(Decision, Tuning);
            return Decision;
        }

        if (Context.bHasLivePhysicsBodies && Decision.NormalizedStrength >= Tuning.ShockwaveImpulseThreshold)
        {
            Decision.Type = ERagdollReactionType::ImpulseOnly;
            Decision.Reason = ERagdollReactionDecisionReason::ShockwaveImpulseOnly;
            return Decision;
        }

        if (!Context.bHasResolvedPartialSelection)
        {
            Decision.Type = ERagdollReactionType::None;
            Decision.Reason = ERagdollReactionDecisionReason::CouldNotResolvePartialSelection;
            return Decision;
        }

        Decision.Type =
            Context.bSamePartialSelectionActive
                ? ERagdollReactionType::RefreshPartial
                : ERagdollReactionType::Partial;
        Decision.Reason = ERagdollReactionDecisionReason::PartialSelected;
        return Decision;
    }

    if (Context.CurrentMode == ERagdollMode::FullBody)
    {
        Decision.Type =
            Context.bHasLivePhysicsBodies
                ? ERagdollReactionType::ImpulseOnly
                : ERagdollReactionType::None;
        Decision.Reason = ERagdollReactionDecisionReason::FullBodyAlreadyActive;
        return Decision;
    }

    if (!Context.bHasResolvedPartialSelection)
    {
        if (Decision.bEscalationCandidate)
        {
            Decision.Type = ERagdollReactionType::FullBody;
            Decision.Reason = ERagdollReactionDecisionReason::FullBodyThreshold;
            ApplyFullBodyLaunchImpulseTuning(Decision, Tuning);
            return Decision;
        }

        Decision.Type = ERagdollReactionType::None;
        Decision.Reason = ERagdollReactionDecisionReason::CouldNotResolvePartialSelection;
        return Decision;
    }

    if (Context.CurrentMode == ERagdollMode::Partial)
    {
        if (Context.bSamePartialSelectionActive)
        {
            if (Request.bAllowEscalationToFullBody && Decision.NormalizedStrength >= Tuning.EscalationThreshold)
            {
                Decision.Type = ERagdollReactionType::EscalateToFull;
                Decision.Reason = ERagdollReactionDecisionReason::ExistingPartialEscalated;
                ApplyFullBodyLaunchImpulseTuning(Decision, Tuning);
                return Decision;
            }

            Decision.Type = ERagdollReactionType::RefreshPartial;
            Decision.Reason = ERagdollReactionDecisionReason::ExistingPartialRefreshed;
            return Decision;
        }

        if (Request.bAllowEscalationToFullBody && Decision.NormalizedStrength >= Tuning.EscalationThreshold)
        {
            Decision.Type = ERagdollReactionType::EscalateToFull;
            Decision.Reason = ERagdollReactionDecisionReason::ExistingPartialEscalated;
            ApplyFullBodyLaunchImpulseTuning(Decision, Tuning);
            return Decision;
        }

        Decision.Type = ERagdollReactionType::None;
        Decision.Reason = ERagdollReactionDecisionReason::ExistingPartialRejected;
        return Decision;
    }

    if (Decision.bEscalationCandidate)
    {
        Decision.Type = ERagdollReactionType::FullBody;
        Decision.Reason = ERagdollReactionDecisionReason::FullBodyThreshold;
        ApplyFullBodyLaunchImpulseTuning(Decision, Tuning);
        return Decision;
    }

    Decision.Type = ERagdollReactionType::Partial;
    Decision.Reason = ERagdollReactionDecisionReason::PartialSelected;
    return Decision;
}

inline const char* LexToString(ERagdollReactionEventKind EventKind)
{
    switch (EventKind)
    {
    case ERagdollReactionEventKind::RadialShockwave:
        return "RadialShockwave";
    case ERagdollReactionEventKind::ScriptedForce:
        return "ScriptedForce";
    default:
        return "DirectHit";
    }
}

inline const char* LexToString(ERagdollReactionType ReactionType)
{
    switch (ReactionType)
    {
    case ERagdollReactionType::ImpulseOnly:
        return "ImpulseOnly";
    case ERagdollReactionType::Partial:
        return "Partial";
    case ERagdollReactionType::FullBody:
        return "FullBody";
    case ERagdollReactionType::RefreshPartial:
        return "RefreshPartial";
    case ERagdollReactionType::EscalateToFull:
        return "EscalateToFull";
    case ERagdollReactionType::Deferred:
        return "Deferred";
    case ERagdollReactionType::RecoveryLimited:
        return "RecoveryLimited";
    default:
        return "None";
    }
}

inline const char* LexToString(ERagdollReactionDecisionReason Reason)
{
    switch (Reason)
    {
    case ERagdollReactionDecisionReason::InvalidRequest:
        return "InvalidRequest";
    case ERagdollReactionDecisionReason::CouldNotResolvePartialSelection:
        return "CouldNotResolvePartialSelection";
    case ERagdollReactionDecisionReason::OwnerMoving:
        return "OwnerMoving";
    case ERagdollReactionDecisionReason::RecoveryProtectedWindow:
        return "RecoveryProtectedWindow";
    case ERagdollReactionDecisionReason::RecoveryImpulseOnly:
        return "RecoveryImpulseOnly";
    case ERagdollReactionDecisionReason::RecoveryEscalatedToFull:
        return "RecoveryEscalatedToFull";
    case ERagdollReactionDecisionReason::FullBodyAlreadyActive:
        return "FullBodyAlreadyActive";
    case ERagdollReactionDecisionReason::ExistingPartialRefreshed:
        return "ExistingPartialRefreshed";
    case ERagdollReactionDecisionReason::ExistingPartialRejected:
        return "ExistingPartialRejected";
    case ERagdollReactionDecisionReason::ExistingPartialEscalated:
        return "ExistingPartialEscalated";
    case ERagdollReactionDecisionReason::ShockwaveImpulseOnly:
        return "ShockwaveImpulseOnly";
    case ERagdollReactionDecisionReason::ShockwaveFullBody:
        return "ShockwaveFullBody";
    case ERagdollReactionDecisionReason::FullBodyThreshold:
        return "FullBodyThreshold";
    case ERagdollReactionDecisionReason::PartialSelected:
        return "PartialSelected";
    case ERagdollReactionDecisionReason::RedundantRequest:
        return "RedundantRequest";
    default:
        return "None";
    }
}
