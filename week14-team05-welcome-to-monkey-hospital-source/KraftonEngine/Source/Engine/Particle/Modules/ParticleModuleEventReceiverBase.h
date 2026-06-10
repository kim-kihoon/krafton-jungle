#pragma once

#include "Particle/ParticleModule.h"
#include "Particle/ParticleEvents.h"
#include "Object/FName.h"

#include "Source/Engine/Particle/Modules/ParticleModuleEventReceiverBase.generated.h"

// =============================================================================
// UParticleModuleEventReceiverBase
//   Event Generator / emitter event queue에서 넘어온 이벤트를 수신하는 Receiver
//   모듈 공통 베이스. 실제 동작은 파생 모듈(Spawn, KillAll)이 수행한다.
// =============================================================================
UCLASS()
class UParticleModuleEventReceiverBase : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleEventReceiverBase() = default;

	EModuleCategory GetCategory() const override { return EModuleCategory::Event; }

	// true면 SourceEventType과 같은 이벤트만 받는다.
	// false면 타입과 무관하게 EventName만 맞으면 받는다.
	UPROPERTY(Edit, Save, Category="Event Receiver", DisplayName="Use Event Type Filter")
	bool bUseEventTypeFilter = true;

	UPROPERTY(Edit, Save, Category="Event Receiver", DisplayName="Source Event Type", Type=Enum, Enum=EParticleEventType)
	EParticleEventType SourceEventType = EParticleEventType::Collision;

	// true면 EventName이 정확히 같은 이벤트만 받는다.
	// Receiver는 기본적으로 이름 있는 이벤트만 받도록 두는 편이 안전하다.
	UPROPERTY(Edit, Save, Category="Event Receiver", DisplayName="Use Event Name Filter")
	bool bUseEventNameFilter = true;

	UPROPERTY(Edit, Save, Category="Event Receiver", DisplayName="Event Name")
	FName EventName = FName("ParticleEvent");

	bool MatchesEvent(const FParticleEventDataBase& Event) const
	{
		if (!IsEnabled())
		{
			return false;
		}

		if (bUseEventTypeFilter && Event.Type != SourceEventType)
		{
			return false;
		}

		if (bUseEventNameFilter && Event.EventName != EventName)
		{
			return false;
		}

		return true;
	}
};
