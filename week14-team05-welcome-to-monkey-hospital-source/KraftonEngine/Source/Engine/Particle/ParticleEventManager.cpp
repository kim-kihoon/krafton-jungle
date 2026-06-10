#include "ParticleEventManager.h"

#include "Particle/ParticleSystemManager.h"

AParticleEventManager::AParticleEventManager() {}

void AParticleEventManager::BeginPlay()
{
	AActor::BeginPlay();

	// Runtime/level setup chooses the default event manager by placing an actor.
	// ParticleSystemManager keeps only a non-owning reference for PSC injection.
	FParticleSystemManager::Get().SetDefaultEventManager(this);
}

void AParticleEventManager::EndPlay()
{
	if (FParticleSystemManager::Get().GetDefaultEventManager() == this)
	{
		FParticleSystemManager::Get().SetDefaultEventManager(nullptr);
	}

	AActor::EndPlay();
}

void AParticleEventManager::HandleParticleSpawnEvents(UParticleSystemComponent* InPSC,
                                                      const TArray<FParticleEventSpawnData>& InEvents)
{
	for (const FParticleEventSpawnData& Event : InEvents)
	{
		OnParticleSpawn.Broadcast(InPSC, Event);
	}
}

void AParticleEventManager::HandleParticleDeathEvents(UParticleSystemComponent* InPSC,
                                                      const TArray<FParticleEventDeathData>& InEvents)
{
	for (const FParticleEventDeathData& Event : InEvents)
	{
		OnParticleDeath.Broadcast(InPSC, Event);
	}
}

void AParticleEventManager::HandleParticleCollisionEvents(UParticleSystemComponent* InPSC,
                                                          const TArray<FParticleEventCollideData>& InEvents)
{
	for (const FParticleEventCollideData& Event : InEvents)
	{
		OnParticleCollision.Broadcast(InPSC, Event);
	}
}

void AParticleEventManager::HandleParticleBurstEvents(UParticleSystemComponent* InPSC,
                                                      const TArray<FParticleEventBurstData>& InEvents)
{
	for (const FParticleEventBurstData& Event : InEvents)
	{
		OnParticleBurst.Broadcast(InPSC, Event);
	}
}
