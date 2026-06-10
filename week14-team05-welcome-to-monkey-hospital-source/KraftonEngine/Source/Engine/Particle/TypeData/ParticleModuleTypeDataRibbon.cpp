#include "ParticleModuleTypeDataRibbon.h"

#include "Particle/ParticleEmitterInstance.h"

FParticleEmitterInstance* UParticleModuleTypeDataRibbon::CreateInstance(UParticleSystemComponent* /*InComponent*/)
{
	// PSC/emitter 경로가 Init()을 수행. 여기선 runtime instance type만 고른다.
	return new FParticleRibbonEmitterInstance();
}
