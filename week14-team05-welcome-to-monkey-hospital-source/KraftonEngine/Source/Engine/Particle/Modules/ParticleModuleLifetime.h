#pragma once

#include "Particle/ParticleModule.h"
#include "Engine/Particle/Distributions/DistributionFloat.h"

#include "Source/Engine/Particle/Modules/ParticleModuleLifetime.generated.h"

// =============================================================================
// UParticleModuleLifetime
//   입자가 생성될 때 한 번 호출되어 OneOverMaxLifetime 을 설정한다.
//   SpawnTime은 particle relative time이 아니라 emitter-loop 기준 시간이며,
//   Lifetime Distribution 평가에만 사용한다.
// =============================================================================
UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleLifetime();

	EModuleCategory GetCategory() const override { return EModuleCategory::Lifetime; }
	const char*     GetDisplayName() const override { return "Lifetime"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;

	// Evaluated with SpawnTime: emitter-loop seconds at which the particle is spawned.
	// This determines max lifetime; it is not evaluated with Particle->RelativeTime.
	UPROPERTY(Edit, Save, Instanced, Category="Lifetime", DisplayName="Lifetime Distribution", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* LifetimeDistribution = nullptr;
};
