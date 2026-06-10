#include "ParticleModuleEventReceiverKillAll.h"

#include "Particle/ParticleEmitterInstance.h"

void UParticleModuleEventReceiverKillAll::ReceiveEvent(
	FParticleEmitterInstance* Owner,
	const FParticleEventDataBase& Event) const
{
	if (!Owner || !MatchesEvent(Event))
	{
		return;
	}

	Owner->KillAllParticles(bStopSpawning);
}
