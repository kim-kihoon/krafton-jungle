#include "ParticleModuleSize.h"

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionVectorConstant.h"

UParticleModuleSize::UParticleModuleSize()
{
	auto* DefaultStartSize = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(this);
	if (DefaultStartSize)
	{
		DefaultStartSize->Constant = {1, 1, 1};
		StartSizeDistribution = DefaultStartSize;
	}
}

void UParticleModuleSize::Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
                                float SpawnTime, FBaseParticle* Particle)
{
	(void)ModuleOffset;

	if (!Particle) return;

	const FVector Size = StartSizeDistribution
		? StartSizeDistribution->GetValue(SpawnTime, Owner ? Owner->GetComponent() : nullptr)
		: FVector(1.0f, 1.0f, 1.0f);

	Particle->Size = Size;
	Particle->BaseSize = Size;
}
