#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleAcceleration.generated.h"

// =============================================================================
// UParticleModuleAcceleration
//   Spawn 시 acceleration payload를 Distribution으로 설정하고, Update에서 적용한다.
//   AccelerationDistribution은 SpawnTime(emitter loop seconds) 기준으로 평가된다.
// =============================================================================
UCLASS()
class UParticleModuleAcceleration : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleAcceleration();

	EModuleCategory GetCategory() const override { return EModuleCategory::Acceleration; }
	const char* GetDisplayName() const override { return "Const Acceleration"; }

	struct FAccelerationParticlePayload
	{
		FVector Acceleration = {0, 0, 0};
	};

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset, float SpawnTime, FBaseParticle* Particle) override;
	void UpdateParticle(FParticleEmitterInstance* Owner, UParticleLODLevel* SimulationLOD,
	                    uint32 ModuleOffset, float DeltaTime, FBaseParticle* Particle) override;
	uint32 RequiredBytes(UParticleLODLevel* LODLevel) const override { (void)LODLevel; return sizeof(FAccelerationParticlePayload); }

	// Evaluated with SpawnTime: emitter-loop seconds at which the particle is spawned.
	UPROPERTY(Edit, Save, Instanced, Category="Acceleration", DisplayName="Acceleration Distribution", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* AccelerationDistribution = nullptr;
};
