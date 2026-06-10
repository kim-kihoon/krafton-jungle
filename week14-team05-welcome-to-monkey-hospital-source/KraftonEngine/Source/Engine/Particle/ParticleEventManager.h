#pragma once

#include "GameFramework/AActor.h"
#include "Particle/ParticleEvents.h"
#include "Core/Delegate.h"

#include "Source/Engine/Particle/ParticleEventManager.generated.h"

class UParticleSystemComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FParticleSpawnEventSignature,    UParticleSystemComponent* /*PSC*/, const FParticleEventSpawnData&    /*Evt*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FParticleDeathEventSignature,    UParticleSystemComponent* /*PSC*/, const FParticleEventDeathData&    /*Evt*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FParticleCollisionEventSignature,UParticleSystemComponent* /*PSC*/, const FParticleEventCollideData&  /*Evt*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FParticleBurstEventSignature,    UParticleSystemComponent* /*PSC*/, const FParticleEventBurstData&    /*Evt*/);

// =============================================================================
// AParticleEventManager
//   레벨에 하나 배치되어 모든 PSC 가 발생시키는 이벤트를 모아 디스패치.
//   PSC::Tick 말미에서 emitter 의 *_Events 큐를 manager 로 보낸다.
//   외부 게임 코드는 OnXxx 델리게이트에 바인딩하여 이벤트를 수신.
// =============================================================================
UCLASS()
class AParticleEventManager : public AActor
{
public:
	GENERATED_BODY()
	AParticleEventManager();
	~AParticleEventManager() override = default;
	// A level/runtime-scoped manager can register itself as the current default
	// provider. ParticleSystemManager stores only a non-owning reference, and
	// PSC consumes it through the existing DI path. The manager is optional for
	// basic particle playback, but gameplay that expects external particle events
	// should provide one at runtime.
	void BeginPlay() override;
	void EndPlay() override;

	// PSC 가 호출 — 자신이 가진 이벤트를 그대로 manager 로 push.
	void HandleParticleSpawnEvents    (UParticleSystemComponent* InPSC, const TArray<FParticleEventSpawnData>&    InEvents);
	void HandleParticleDeathEvents    (UParticleSystemComponent* InPSC, const TArray<FParticleEventDeathData>&    InEvents);
	void HandleParticleCollisionEvents(UParticleSystemComponent* InPSC, const TArray<FParticleEventCollideData>&  InEvents);
	void HandleParticleBurstEvents    (UParticleSystemComponent* InPSC, const TArray<FParticleEventBurstData>&    InEvents);

	FParticleSpawnEventSignature     OnParticleSpawn;
	FParticleDeathEventSignature     OnParticleDeath;
	FParticleCollisionEventSignature OnParticleCollision;
	FParticleBurstEventSignature     OnParticleBurst;
};
