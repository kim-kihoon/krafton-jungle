#include "ParticleModuleSpawn.h"

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionFloatConstant.h"

#include <algorithm>

UParticleModuleSpawn::UParticleModuleSpawn()
{
	auto* DefaultRate = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(this);
	if (DefaultRate)
	{
		DefaultRate->Constant = 20.0f;
		RateDistribution = DefaultRate;
	}

	auto* DefaultRateScale = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(this);
	if (DefaultRateScale)
	{
		DefaultRateScale->Constant = 1.0f;
		RateScaleDistribution = DefaultRateScale;
	}
}

void UParticleModuleSpawn::GetRateSpawnAmount(FParticleEmitterInstance* Owner, float DeltaTime, float EmitterTime,
                                                float& OutSpawnAmount) const
{
	const float SafeDeltaTime = std::max(0.0f, DeltaTime);
	const float RateSampleTime = EmitterTime + SafeDeltaTime * 0.5f;

	const float SampledRate = RateDistribution
		? RateDistribution->GetValue(RateSampleTime, Owner ? Owner->GetComponent() : nullptr)
		: 0.0f;
	const float SampledRateScale = RateScaleDistribution
		? RateScaleDistribution->GetValue(RateSampleTime, Owner ? Owner->GetComponent() : nullptr)
		: 1.0f;

	const float SafeRate = std::max(0.0f, SampledRate);
	const float SafeRateScale = std::max(0.0f, SampledRateScale);

	OutSpawnAmount = SafeRate * SafeRateScale * SafeDeltaTime;
}
