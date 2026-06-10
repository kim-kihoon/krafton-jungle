#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleVelocity.generated.h"

// =============================================================================
// UParticleModuleVelocity
//   입자 spawn 시 초기 속도(Velocity, BaseVelocity)를 Distribution으로 설정한다.
//   StartVelocityDistribution은 SpawnTime(emitter loop seconds) 기준으로 평가된다.
// =============================================================================
UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleVelocity();

	EModuleCategory GetCategory() const override { return EModuleCategory::Velocity; }
	const char*     GetDisplayName() const override { return "Initial Velocity"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;

	// Evaluated with SpawnTime: emitter-loop seconds at which the particle is spawned.
	UPROPERTY(Edit, Save, Instanced, Category="Velocity", DisplayName="Start Velocity Distribution", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* StartVelocityDistribution = nullptr;

	UPROPERTY(Edit, Save, Category="Velocity", DisplayName="In World Space")
	bool bInWorldSpace = false;
};
