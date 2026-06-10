#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleSize.generated.h"

// =============================================================================
// UParticleModuleSize
//   Initial Size 전용 모듈.
//   SpawnTime은 particle relative time이 아니라 emitter-loop 기준 시간이며,
//   StartSizeDistribution 평가에만 사용한다.
//   생존 시간에 따른 size 변화는 UParticleModuleSizeByLife가 담당한다.
// =============================================================================
UCLASS()
class UParticleModuleSize : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleSize();

	EModuleCategory GetCategory() const override { return EModuleCategory::Size; }
	const char*     GetDisplayName() const override { return "Initial Size"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;

	// Evaluated with SpawnTime: emitter-loop seconds at which the particle is spawned.
	UPROPERTY(Edit, Save, Instanced, Category="Size", DisplayName="Start Size Distribution", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* StartSizeDistribution = nullptr;
};
