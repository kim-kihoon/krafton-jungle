#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleLocation.generated.h"

// =============================================================================
// UParticleModuleLocation
//   입자 spawn 시 초기 위치 offset을 Distribution으로 결정한다.
//   StartLocationDistribution은 SpawnTime(emitter loop seconds) 기준으로 평가된다.
// =============================================================================
UCLASS()
class UParticleModuleLocation : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleLocation();

	EModuleCategory GetCategory() const override { return EModuleCategory::Location; }
	const char*     GetDisplayName() const override { return "Initial Location"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;

	// Evaluated with SpawnTime: emitter-loop seconds at which the particle is spawned.
	UPROPERTY(Edit, Save, Instanced, Category="Location", DisplayName="Start Location Distribution", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* StartLocationDistribution = nullptr;

	UPROPERTY(Edit, Save, Category="Location", DisplayName="World Space Override")
	bool bWorldSpaceOverride = false;
};
