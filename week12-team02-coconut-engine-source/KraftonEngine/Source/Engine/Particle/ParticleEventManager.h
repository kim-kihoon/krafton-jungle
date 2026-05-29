#pragma once

#include "Core/Delegate.h"
#include "GameFramework/AActor.h"
#include "Particle/ParticleEventCollideData.h"
#include "ParticleEventManager.generated.h"

class UParticleSystemComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FParticleSpawnEventSignature, UParticleSystemComponent*, const FParticleEventSpawnData&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FParticleDeathEventSignature, UParticleSystemComponent*, const FParticleEventDeathData&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FParticleCollisionEventSignature, UParticleSystemComponent*, const FParticleEventCollideData&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FParticleBurstEventSignature, UParticleSystemComponent*, const FParticleEventBurstData&);

UCLASS(Actor)
class AParticleEventManager : public AActor
{
public:
	GENERATED_BODY(AParticleEventManager)

	AParticleEventManager();

	void BeginPlay() override;
	void EndPlay() override;

	virtual void HandleParticleSpawnEvents(UParticleSystemComponent* Component, const TArray<FParticleEventSpawnData>& InSpawnEvents);
	virtual void HandleParticleDeathEvents(UParticleSystemComponent* Component, const TArray<FParticleEventDeathData>& InDeathEvents);
	virtual void HandleParticleCollisionEvents(UParticleSystemComponent* Component, const TArray<FParticleEventCollideData>& InCollisionEvents);
	virtual void HandleParticleBurstEvents(UParticleSystemComponent* Component, const TArray<FParticleEventBurstData>& InBurstEvents);

	FParticleSpawnEventSignature OnParticleSpawn;
	FParticleDeathEventSignature OnParticleDeath;
	FParticleCollisionEventSignature OnParticleCollide;
	FParticleBurstEventSignature OnParticleBurst;
};
