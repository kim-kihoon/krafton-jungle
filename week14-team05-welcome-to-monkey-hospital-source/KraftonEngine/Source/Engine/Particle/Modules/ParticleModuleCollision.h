#pragma once

#include "Particle/ParticleModule.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Vector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleCollision.generated.h"

USTRUCT()
struct FParticleCollisionLODPolicyOverride
{
	GENERATED_BODY()

	// This override belongs to current-emitter-LOD outer collision policy:
	// workload budget, optional full-disable, and optional event gating.
	// It does not change what an accepted hit means for response/completion.
	UPROPERTY(Edit, Save, Category="Collision|LOD Policy", DisplayName="Enabled")
	bool bEnabled = false;

	UPROPERTY(Edit, Save, Category="Collision|LOD Policy", DisplayName="Collision Query Budget", Min=0)
	int32 CollisionQueryBudget = 0;

	UPROPERTY(Edit, Save, Category="Collision|LOD Policy", DisplayName="Override Disable Policy")
	bool bOverrideDisablePolicy = false;

	UPROPERTY(Edit, Save, Category="Collision|LOD Policy", DisplayName="Disable Collision Queries")
	bool bDisableCollisionQueries = false;

	UPROPERTY(Edit, Save, Category="Collision|LOD Policy", DisplayName="Override Collision Event Policy")
	bool bOverrideCollisionEventPolicy = false;

	UPROPERTY(Edit, Save, Category="Collision|LOD Policy", DisplayName="Allow Collision Events")
	bool bAllowCollisionEvents = true;
};

// =============================================================================
// UParticleModuleCollision
//   Collision authoring/settings module.
//   - Stores collision policy (damping, max collisions, trace channel,
//     kill-on-collision, collision-event intent).
//   - May optionally override current-LOD collision outer policy
//     (budget / full-disable / event gating) without redefining hit response.
//   - Initializes lightweight per-particle collision payload at spawn time.
//   - Actual world hit query and runtime collision response are executed by the
//     explicit FParticleEmitterInstance collision pass.
// =============================================================================
UCLASS()
class UParticleModuleCollision : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleCollision() = default;

	// Immediate response answers "what should happen on this hit?"
	UENUM()
	enum class ECollisionResponseMode : uint8
	{
		Bounce,
		Stop,
		Kill,
	};

	// Completion mode answers "what should happen once MaxCollisions is reached?"
	UENUM()
	enum class ECollisionCompletionMode : uint8
	{
		Kill,
		Freeze,
		IgnoreFurtherCollisions,
	};

	EModuleCategory GetCategory() const override { return EModuleCategory::Collision; }
	const char*     GetDisplayName() const override { return "Collision"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;
	void Update(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	            float DeltaTime) override;

	uint32 RequiredBytes(UParticleLODLevel* LODLevel) const override;

	// Primarily controls how much of the bounce / normal response is retained
	// after an accepted collision.
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Damping Factor", Min=0.0f, Max=1.0f)
	float DampingFactor = 0.5f;

	// Controls how much surface-parallel velocity is retained after an accepted
	// Bounce collision. Lower values feel more "sticky"; higher values preserve
	// more sliding motion along the hit surface.
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Tangential Damping", Min=0.0f, Max=1.0f)
	float TangentialDamping = 0.75f;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Max Collisions")
	int32 MaxCollisions = 4; // <= 0 means unlimited.

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Collision Channel", Enum=ECollisionChannel)
	ECollisionChannel CollisionChannel = ECollisionChannel::WorldStatic;

	// Immediate response for the current hit. This answers "what happens now?"
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Response Mode", Enum=ECollisionResponseMode)
	ECollisionResponseMode ResponseMode = ECollisionResponseMode::Bounce;

	// Completion behavior once MaxCollisions is reached. This answers
	// "what happens after enough hits?"
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Completion Mode", Enum=ECollisionCompletionMode)
	ECollisionCompletionMode CompletionMode = ECollisionCompletionMode::Freeze;

	// Legacy compatibility path. If true, the immediate response is treated as Kill
	// regardless of ResponseMode so existing assets keep their intent.
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Kill On Collision")
	bool bKillOnCollision = false;

	// Base collision events flow through the existing EventGenerator / PSC path.
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Generate Collision Events")
	bool bGenerateCollisionEvents = false;

	// Optional authoring override for the current-emitter-LOD collision outer
	// policy. When disabled, runtime falls back to the legacy hardcoded LOD
	// policy so existing assets keep their current behavior.
	UPROPERTY(Edit, Save, Category="Collision|LOD Policy", DisplayName="LOD Policy Override")
	FParticleCollisionLODPolicyOverride LODPolicyOverride;

	// Per-particle runtime collision state used by the emitter-instance pass.
	// The recent-hit fields are intentionally lightweight and only exist to calm
	// repeated-contact noise; they do not replace response/completion semantics.
	struct FCollisionParticlePayload
	{
		int32 NumCollisions = 0;
		bool bIgnoreFurtherCollisions = false;
		bool bFrozenAfterLimit = false; // Persistently reverts post-update motion at collision pass.
		float LastCollisionTime = -1.0f;
		FVector LastCollisionNormal = FVector::ZeroVector;
	};
};
