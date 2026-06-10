#include "ParticleModuleLocation.h"

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionVectorConstant.h"

UParticleModuleLocation::UParticleModuleLocation()
{
	auto* DefaultLocation = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(this);
	if (DefaultLocation)
	{
		DefaultLocation->Constant = {0, 0, 0};
		StartLocationDistribution = DefaultLocation;
	}
}

void UParticleModuleLocation::Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
                                    float SpawnTime, FBaseParticle* Particle)
{
	(void)ModuleOffset;

	if (!Owner || !Particle) return;

	const FVector Offset = StartLocationDistribution
		? StartLocationDistribution->GetValue(SpawnTime, Owner->GetComponent())
		: FVector(0.0f, 0.0f, 0.0f);

	const EParticleValueSpace SourceSpace =
		bWorldSpaceOverride ? EParticleValueSpace::World : EParticleValueSpace::Local;
	const FVector SimulationOffset = Owner->ConvertVectorToSimulation(Offset, SourceSpace);

	Particle->Location = Particle->Location + SimulationOffset;
	Particle->OldLocation = Particle->Location;
}
