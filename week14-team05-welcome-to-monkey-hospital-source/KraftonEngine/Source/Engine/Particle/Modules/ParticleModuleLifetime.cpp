#include "ParticleModuleLifetime.h"

#include <algorithm>

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionFloatUniform.h"

UParticleModuleLifetime::UParticleModuleLifetime()
{
	auto* DefaultLifetime = UObjectManager::Get().CreateObject<UDistributionFloatUniform>(this);
	if (DefaultLifetime)
	{
		DefaultLifetime->Min = 1.0f;
		DefaultLifetime->Max = 2.0f;
		LifetimeDistribution = DefaultLifetime;
	}
}

void UParticleModuleLifetime::Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
                                    float SpawnTime, FBaseParticle* Particle)
{
	(void)ModuleOffset;

	if (!Particle) return;

	float Life = LifetimeDistribution
		? LifetimeDistribution->GetValue(SpawnTime, Owner ? Owner->GetComponent() : nullptr)
		: 1.0f;

	Life = std::max(0.001f, Life);

	Particle->OneOverMaxLifetime = 1.0f / Life;

	float SpawnOffsetSeconds = 0.0f;
	if (Owner)
	{
		SpawnOffsetSeconds = std::max(
			0.0f,
			SpawnTime - Owner->GetCurrentLoopTimeSeconds());
	}

	// 현재 tick 순서는 Spawn -> Update이므로 프레임 중간에 태어난 입자는
	// 음수 relative time에서 시작해야 Update 후 실제 age가 맞는다.
	Particle->RelativeTime = -SpawnOffsetSeconds * Particle->OneOverMaxLifetime;
}
