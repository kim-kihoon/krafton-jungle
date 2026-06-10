#include "ParticleModuleEventGenerator.h"

#include "Particle/ParticleEmitterInstance.h"

void UParticleModuleEventGenerator::HandleSpawnEvent(FParticleEmitterInstance* Owner,
                                                     const FParticleEventSpawnData& InTemplate) const
{
	if (!Owner)
	{
		return;
	}

	for (const FParticleEventGeneratorEntry& Entry : Entries)
	{
		if (!Entry.bEnabled || Entry.Type != EParticleEventType::Spawn)
		{
			continue;
		}

		FParticleEventSpawnData Event = InTemplate;
		// base event는 emitter instance가 이미 enqueue했고,
		// EventGenerator는 이름이 붙은 추가 이벤트만 파생시킨다.
		Event.EventName = Entry.EventName;
		Owner->EnqueueSpawnEvent(Event);
	}
}

void UParticleModuleEventGenerator::HandleDeathEvent(FParticleEmitterInstance* Owner,
                                                     const FParticleEventDeathData& InTemplate) const
{
	if (!Owner)
	{
		return;
	}

	for (const FParticleEventGeneratorEntry& Entry : Entries)
	{
		if (!Entry.bEnabled || Entry.Type != EParticleEventType::Death)
		{
			continue;
		}

		FParticleEventDeathData Event = InTemplate;
		Event.EventName = Entry.EventName;
		Owner->EnqueueDeathEvent(Event);
	}
}

void UParticleModuleEventGenerator::HandleCollisionEvent(FParticleEmitterInstance* Owner,
                                                         const FParticleEventCollideData& InTemplate) const
{
	if (!Owner)
	{
		return;
	}

	for (const FParticleEventGeneratorEntry& Entry : Entries)
	{
		if (!Entry.bEnabled || Entry.Type != EParticleEventType::Collision)
		{
			continue;
		}

		FParticleEventCollideData Event = InTemplate;
		Event.EventName = Entry.EventName;
		Owner->EnqueueCollisionEvent(Event);
	}
}

void UParticleModuleEventGenerator::HandleBurstEvent(FParticleEmitterInstance* Owner,
                                                     const FParticleEventBurstData& InTemplate) const
{
	if (!Owner)
	{
		return;
	}

	for (const FParticleEventGeneratorEntry& Entry : Entries)
	{
		if (!Entry.bEnabled || Entry.Type != EParticleEventType::Burst)
		{
			continue;
		}

		FParticleEventBurstData Event = InTemplate;
		Event.EventName = Entry.EventName;
		Owner->EnqueueBurstEvent(Event);
	}
}
