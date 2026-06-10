#pragma once

#include "Particle/Modules/ParticleModuleEventReceiverBase.h"

#include "Source/Engine/Particle/Modules/ParticleModuleEventReceiverKillAll.generated.h"

// =============================================================================
// UParticleModuleEventReceiverKillAll
//   특정 이벤트를 받으면 이 모듈이 붙은 emitter의 활성 파티클을 모두 제거한다.
// =============================================================================
UCLASS()
class UParticleModuleEventReceiverKillAll : public UParticleModuleEventReceiverBase
{
public:
	GENERATED_BODY()
	UParticleModuleEventReceiverKillAll() = default;

	const char* GetDisplayName() const override { return "Event Receiver Kill All"; }

	UPROPERTY(Edit, Save, Category="Event Receiver Kill All", DisplayName="Stop Spawning")
	bool bStopSpawning = false;

	void ReceiveEvent(FParticleEmitterInstance* Owner, const FParticleEventDataBase& Event) const;
};
