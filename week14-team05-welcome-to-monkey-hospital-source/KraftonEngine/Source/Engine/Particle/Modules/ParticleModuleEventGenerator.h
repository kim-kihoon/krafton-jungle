#pragma once

#include "Particle/ParticleModule.h"
#include "Particle/ParticleEvents.h"
#include "Object/FName.h"

#include "Source/Engine/Particle/Modules/ParticleModuleEventGenerator.generated.h"

USTRUCT()
struct FParticleEventGeneratorEntry
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Event", DisplayName="Type", Type=Enum, Enum=EParticleEventType)
	EParticleEventType Type = EParticleEventType::Death;

	UPROPERTY(Edit, Save, Category="Event", DisplayName="Event Name")
	FName EventName;

	UPROPERTY(Edit, Save, Category="Event", DisplayName="Enabled")
	bool bEnabled = true;
};

// =============================================================================
// UParticleModuleEventGenerator
//   특정 이벤트 (Spawn/Death/Collision/Burst) 가 발생할 때 EmitterInstance 의
//   이벤트 큐에 push 해주는 모듈. 실제 push 위치는:
//     - Spawn  : EmitterInstance::SpawnInternal 후 (생성된 갯수만큼)
//     - Death  : EmitterInstance::UpdateParticles 의 kill 처리 분기
//     - Collision : UParticleModuleCollision::Update 의 hit 분기
//     - Burst  : EmitterInstance::SpawnBurstParticles 의 burst hit 분기
//   본 모듈은 "어떤 이벤트를 어떤 이름으로 발행할지" 설정 + 헬퍼 메서드만 제공.
// =============================================================================
UCLASS()
class UParticleModuleEventGenerator : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleEventGenerator() = default;

	EModuleCategory GetCategory() const override { return EModuleCategory::Event; }
	const char*     GetDisplayName() const override { return "Event Generator"; }

	UPROPERTY(Edit, Save, Category="Event", DisplayName="Entries", Type=Array, Struct=FParticleEventGeneratorEntry)
	TArray<FParticleEventGeneratorEntry> Entries;

	// 각 발행 지점에서 호출. 활성 entry 가 있으면 emitter 의 큐에 push.
	void HandleSpawnEvent    (FParticleEmitterInstance* Owner, const FParticleEventSpawnData&    InTemplate) const;
	void HandleDeathEvent    (FParticleEmitterInstance* Owner, const FParticleEventDeathData&    InTemplate) const;
	void HandleCollisionEvent(FParticleEmitterInstance* Owner, const FParticleEventCollideData&  InTemplate) const;
	void HandleBurstEvent    (FParticleEmitterInstance* Owner, const FParticleEventBurstData&    InTemplate) const;
};
