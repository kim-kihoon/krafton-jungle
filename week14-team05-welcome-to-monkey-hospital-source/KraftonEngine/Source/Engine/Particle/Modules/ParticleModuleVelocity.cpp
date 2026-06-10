#include "ParticleModuleVelocity.h"

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionVectorConstant.h"

UParticleModuleVelocity::UParticleModuleVelocity()
{
	auto* DefaultVelocity = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(this);
	if (DefaultVelocity)
	{
		DefaultVelocity->Constant = {0, 0, 0};
		StartVelocityDistribution = DefaultVelocity;
	}
}

void UParticleModuleVelocity::Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
                                    float SpawnTime, FBaseParticle* Particle)
{
	(void)ModuleOffset;

	if (!Owner || !Particle) return;

	const FVector Velocity = StartVelocityDistribution
		? StartVelocityDistribution->GetValue(SpawnTime, Owner->GetComponent())
		: FVector(0.0f, 0.0f, 0.0f);

	const EParticleValueSpace SourceSpace =
		bInWorldSpace ? EParticleValueSpace::World : EParticleValueSpace::Local;
	const FVector SimulationVelocity = Owner->ConvertVectorToSimulation(Velocity, SourceSpace);

	Particle->Velocity = SimulationVelocity;
	Particle->BaseVelocity = SimulationVelocity;
}
