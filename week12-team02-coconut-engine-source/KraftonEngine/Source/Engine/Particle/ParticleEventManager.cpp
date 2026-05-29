#include "Particle/ParticleEventManager.h"

#include "Component/ParticleSystemComponent.h"
#include "GameFramework/World.h"

AParticleEventManager::AParticleEventManager()
{
	bNeedsTick = false;
}

void AParticleEventManager::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		World->MyParticleEventManager = this;
	}
}

void AParticleEventManager::EndPlay()
{
	if (UWorld* World = GetWorld())
	{
		if (World->MyParticleEventManager == this)
		{
			World->MyParticleEventManager = nullptr;
		}
	}

	Super::EndPlay();
}

void AParticleEventManager::HandleParticleSpawnEvents(UParticleSystemComponent* Component,
	const TArray<FParticleEventSpawnData>& InSpawnEvents)
{
	for (const FParticleEventSpawnData& EventData : InSpawnEvents)
	{
		OnParticleSpawn.Broadcast(Component, EventData);
	}
}

void AParticleEventManager::HandleParticleDeathEvents(UParticleSystemComponent* Component,
	const TArray<FParticleEventDeathData>& InDeathEvents)
{
	for (const FParticleEventDeathData& EventData : InDeathEvents)
	{
		OnParticleDeath.Broadcast(Component, EventData);
	}
}

void AParticleEventManager::HandleParticleCollisionEvents(UParticleSystemComponent* Component,
	const TArray<FParticleEventCollideData>& InCollisionEvents)
{
	for (const FParticleEventCollideData& EventData : InCollisionEvents)
	{
		OnParticleCollide.Broadcast(Component, EventData);
	}
}

void AParticleEventManager::HandleParticleBurstEvents(UParticleSystemComponent* Component,
	const TArray<FParticleEventBurstData>& InBurstEvents)
{
	for (const FParticleEventBurstData& EventData : InBurstEvents)
	{
		OnParticleBurst.Broadcast(Component, EventData);
	}
}
