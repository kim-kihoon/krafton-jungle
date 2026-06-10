#include "ParticleModule.h"

#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleLODLevel.h"
#include "Serialization/Archive.h"

void UParticleModule::PostDuplicate()
{
	UObject::PostDuplicate();
}

void UParticleModule::UpdateParticleSubset(
	FParticleEmitterInstance* Owner,
	UParticleLODLevel* SimulationLOD,
	uint32 ModuleOffset,
	float DeltaTime,
	const TArray<uint32>& ParticleIndices)
{
	if (!Owner)
	{
		return;
	}

	for (uint32 ParticleIndex : ParticleIndices)
	{
		if (FBaseParticle* Particle = Owner->GetParticleAt(ParticleIndex))
		{
			UpdateParticle(Owner, SimulationLOD, ModuleOffset, DeltaTime, Particle);
		}
	}
}

