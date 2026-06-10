#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleSizeByLife.generated.h"

// =============================================================================
// UParticleModuleSizeByLife
//   Particle->RelativeTime(0..1)을 기준으로 BaseSize에 scale distribution을 곱한다.
//   Unreal Cascade의 Size By Life처럼 LifeMultiplier는 particle relative time으로 평가된다.
// =============================================================================
UCLASS()
class UParticleModuleSizeByLife : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleSizeByLife();

	EModuleCategory GetCategory() const override { return EModuleCategory::Size; }
	const char*     GetDisplayName() const override { return "Size By Life"; }

	void UpdateParticle(FParticleEmitterInstance* Owner, UParticleLODLevel* SimulationLOD,
	                    uint32 ModuleOffset, float DeltaTime, FBaseParticle* Particle) override;

	// Evaluated with Particle->RelativeTime: normalized particle lifetime, 0=birth, 1=death.
	UPROPERTY(Edit, Save, Instanced, Category="Size By Life", DisplayName="Life Multiplier Distribution", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* LifeMultiplierDistribution = nullptr;
};
