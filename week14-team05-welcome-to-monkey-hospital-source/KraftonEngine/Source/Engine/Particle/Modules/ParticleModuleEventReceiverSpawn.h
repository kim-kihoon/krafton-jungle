#pragma once

#include "Particle/Modules/ParticleModuleEventReceiverBase.h"

#include "Source/Engine/Particle/Modules/ParticleModuleEventReceiverSpawn.generated.h"

// =============================================================================
// UParticleModuleEventReceiverSpawn
//   특정 이벤트를 받으면 이 모듈이 붙은 emitter에서 파티클을 생성한다.
//   대표 용도: Collision Event 위치에 spark/splash emitter 입자 생성.
// =============================================================================
UCLASS()
class UParticleModuleEventReceiverSpawn : public UParticleModuleEventReceiverBase
{
public:
	GENERATED_BODY()
	UParticleModuleEventReceiverSpawn() = default;

	const char* GetDisplayName() const override { return "Event Receiver Spawn"; }

	UPROPERTY(Edit, Save, Category="Event Receiver Spawn", DisplayName="Spawn Count", Min=0.0f, Max=1024.0f)
	int32 SpawnCount = 1;

	UPROPERTY(Edit, Save, Category="Event Receiver Spawn", DisplayName="Use Event Location")
	bool bUseEventLocation = true;

	UPROPERTY(Edit, Save, Category="Event Receiver Spawn", DisplayName="Inherit Event Velocity")
	bool bInheritEventVelocity = false;

	UPROPERTY(Edit, Save, Category="Event Receiver Spawn", DisplayName="Inherit Velocity Scale", Min=-1000.0f, Max=1000.0f)
	float InheritVelocityScale = 1.0f;

	void ReceiveEvent(FParticleEmitterInstance* Owner, const FParticleEventDataBase& Event) const;
};
