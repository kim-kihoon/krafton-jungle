#include "ParticleModuleCollision.h"

#include "Particle/ParticleEmitterInstance.h"

void UParticleModuleCollision::Spawn(
	FParticleEmitterInstance* Owner,
	uint32 ModuleOffset,
	float SpawnTime,
	FBaseParticle* Particle)
{
	(void)Owner;
	(void)SpawnTime;

	if (!Particle)
	{
		return;
	}

	if (FCollisionParticlePayload* Payload =
		PARTICLE_PAYLOAD(Particle, ModuleOffset, FCollisionParticlePayload))
	{
		// Collision runtime state is still per-particle payload, but the actual
		// hit query / response execution now lives in FParticleEmitterInstance.
		*Payload = {};
	}
}

void UParticleModuleCollision::Update(
	FParticleEmitterInstance* Owner,
	uint32 ModuleOffset,
	float DeltaTime)
{
	(void)Owner;
	(void)ModuleOffset;
	(void)DeltaTime;

	// Intentional no-op.
	// Collision remains a regular module for authoring/LOD/payload layout, but
	// runtime collision solving is handled by the explicit emitter-instance
	// collision pass after generic module updates.
}

uint32 UParticleModuleCollision::RequiredBytes(UParticleLODLevel* LODLevel) const
{
	(void)LODLevel;
	return sizeof(FCollisionParticlePayload);
}
