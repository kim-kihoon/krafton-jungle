#include "ParticleModuleEventReceiverSpawn.h"

#include "Particle/ParticleEmitterInstance.h"

void UParticleModuleEventReceiverSpawn::ReceiveEvent(
	FParticleEmitterInstance* Owner,
	const FParticleEventDataBase& Event) const
{
	if (!Owner || SpawnCount <= 0 || !MatchesEvent(Event))
	{
		return;
	}

	FParticleEventSpawnOverride SpawnOverride;
	SpawnOverride.bUseLocation = bUseEventLocation;
	SpawnOverride.LocationWorld = Event.Location;
	SpawnOverride.bInheritVelocity = bInheritEventVelocity;
	SpawnOverride.VelocityWorld = Event.Velocity;
	SpawnOverride.InheritVelocityScale = InheritVelocityScale;

	Owner->SpawnFromEvent(SpawnCount, SpawnOverride);
}
