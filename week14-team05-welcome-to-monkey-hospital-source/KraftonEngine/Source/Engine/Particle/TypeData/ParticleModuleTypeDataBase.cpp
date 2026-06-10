#include "ParticleModuleTypeDataBase.h"

#include "Particle/ParticleEmitterInstance.h"

FParticleEmitterInstance* UParticleModuleTypeDataBase::CreateInstance(UParticleSystemComponent* InComponent)
{
	// 베이스는 nullptr — emitter 가 Sprite instance 로 폴백.
	return nullptr;
}
