#pragma once

#include "GameFramework/Actor/TriggerVolumeBase.h"
#include "Core/Types/CoreTypes.h"
#include "Object/FName.h"

#include "Object/Ptr/WeakObjectPtr.h"
#include "Source/Engine/GameFramework/Actor/TriggerVolumeParticle.generated.h"

class APawn;
class UParticleSystemComponent;

// ============================================================
// ATriggerVolumeParticle - consumes trigger occupancy transitions to toggle particles
//
// Runtime intent:
//   1) Cache tagged particle system components at BeginPlay
//   2) Activate on the first real occupying pawn entering
//   3) Deactivate on the last real occupying pawn exiting
// ============================================================
UCLASS()
class ATriggerVolumeParticle : public ATriggerVolumeBase
{
public:
	GENERATED_BODY()
	ATriggerVolumeParticle() = default;

	void BeginPlay() override;

	void OnPossessedPawnEntered(APawn* Pawn) override;
	void OnPossessedPawnExited(APawn* Pawn) override;

private:
	UPROPERTY(Edit, Save, Category="Particle Trigger", DisplayName="ParticleTag")
	FName ParticleTag = "particleactor";

	UPROPERTY(Edit, Save, Category="Particle Trigger", DisplayName="Activate On Trigger Enter")
	bool bActivateOnTriggerEnter = true;

	UPROPERTY(Edit, Save, Category="Particle Trigger", DisplayName="Deactivate On Trigger Exit")
	bool bDeactivateOnTriggerExit = true;

	TArray<TWeakObjectPtr<UParticleSystemComponent>> CachedComponents;
};
