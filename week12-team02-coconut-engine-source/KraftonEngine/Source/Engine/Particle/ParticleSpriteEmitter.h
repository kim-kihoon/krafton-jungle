#pragma once

#include "Particle/ParticleEmitter.h"
#include "ParticleSpriteEmitter.generated.h"

UCLASS()
class UParticleSpriteEmitter : public UParticleEmitter
{
public:
	GENERATED_BODY(UParticleSpriteEmitter)

	virtual void SetToSensibleDefaults() {}
};
