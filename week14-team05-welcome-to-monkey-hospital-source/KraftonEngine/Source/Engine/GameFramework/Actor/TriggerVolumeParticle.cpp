#include "GameFramework/Actor/TriggerVolumeParticle.h"

#include "Component/Particle/ParticleSystemComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"

void ATriggerVolumeParticle::BeginPlay()
{
	Super::BeginPlay();

	CachedComponents.clear();
	for (AActor* Actor : FGameplayStatics::FindActorsByTag(GetWorld(), ParticleTag))
	{
		AParticleSystemActor* ParticleActor = Cast<AParticleSystemActor>(Actor);
		if (!ParticleActor)
		{
			continue;
		}

		UParticleSystemComponent* PSC = ParticleActor->GetParticleSystemComponent();
		if (!PSC)
		{
			continue;
		}

		CachedComponents.push_back(PSC);
		if (bActivateOnTriggerEnter)
		{
			PSC->Deactivate();
		}
	}
}

void ATriggerVolumeParticle::OnPossessedPawnEntered(APawn* Pawn)
{
	UE_LOG("[TriggerVolumeParticle] OccupantEntered Trigger=%s Pawn=%s Occupants=%d",
		GetName().c_str(),
		Pawn ? Pawn->GetName().c_str() : "None",
		GetOccupyingPawnCount());

	if (GetOccupyingPawnCount() != 1 || !bActivateOnTriggerEnter)
	{
		return;
	}

	UE_LOG("[TriggerVolumeParticle] FirstOccupantEntered Trigger=%s ParticleCount=%d",
		GetName().c_str(),
		static_cast<int32>(CachedComponents.size()));

	for (const TWeakObjectPtr<UParticleSystemComponent>& WeakPSC : CachedComponents)
	{
		if (UParticleSystemComponent* PSC = WeakPSC.Get())
		{
			PSC->Activate();
		}
	}
}

void ATriggerVolumeParticle::OnPossessedPawnExited(APawn* Pawn)
{
	UE_LOG("[TriggerVolumeParticle] OccupantExited Trigger=%s Pawn=%s Occupants=%d",
		GetName().c_str(),
		Pawn ? Pawn->GetName().c_str() : "None",
		GetOccupyingPawnCount());

	if (GetOccupyingPawnCount() != 0 || !bDeactivateOnTriggerExit)
	{
		return;
	}

	UE_LOG("[TriggerVolumeParticle] LastOccupantExited Trigger=%s ParticleCount=%d",
		GetName().c_str(),
		static_cast<int32>(CachedComponents.size()));

	for (const TWeakObjectPtr<UParticleSystemComponent>& WeakPSC : CachedComponents)
	{
		if (UParticleSystemComponent* PSC = WeakPSC.Get())
		{
			PSC->Deactivate();
		}
	}
}
