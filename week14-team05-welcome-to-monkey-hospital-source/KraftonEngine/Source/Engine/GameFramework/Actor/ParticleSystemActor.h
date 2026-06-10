#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/ParticleSystemActor.generated.h"
class UParticleSystemComponent;

UCLASS()
class AParticleSystemActor : public AActor
{
public:
	GENERATED_BODY()
	AParticleSystemActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;
	void BeginPlay() override;

	UFUNCTION(Pure, Category="Actor|Components")
	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }

private:
	TWeakObjectPtr<UParticleSystemComponent> ParticleSystemComponent = nullptr;
};
