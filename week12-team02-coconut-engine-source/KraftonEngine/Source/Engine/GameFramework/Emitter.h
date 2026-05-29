#pragma once

#include "GameFramework/AActor.h"
#include "Emitter.generated.h"

class UParticleSystem;
class UParticleSystemComponent;
class UBillboardComponent;

UCLASS(Actor)
class AEmitter : public AActor
{
public:
	GENERATED_BODY(AEmitter)

	AEmitter();

	void InitDefaultComponents(UParticleSystem* ParticleSystem = nullptr);
	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }

private:
	UParticleSystemComponent* ParticleSystemComponent = nullptr;
	UBillboardComponent* SpriteComponent = nullptr;
};
