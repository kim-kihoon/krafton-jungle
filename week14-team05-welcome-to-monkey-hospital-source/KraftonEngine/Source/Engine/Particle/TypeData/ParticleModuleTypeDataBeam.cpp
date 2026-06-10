#include "ParticleModuleTypeDataBeam.h"

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Engine/Particle/Distributions/DistributionFloatConstant.h"

namespace
{
	UDistributionFloatConstant* MakeFloatConstant(UObject* Outer, float Value)
	{
		auto* Distribution = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(Outer);
		if (Distribution)
		{
			Distribution->Constant = Value;
		}
		return Distribution;
	}

	float EvalFloatDistribution(const UDistributionFloat* Distribution, float Time, UObject* Data, float DefaultValue)
	{
		return Distribution ? Distribution->GetValue(Time, Data) : DefaultValue;
	}

}

UParticleModuleTypeDataBeam::UParticleModuleTypeDataBeam()
{
	WidthDistribution = MakeFloatConstant(this, Width);
	DistanceDistribution = MakeFloatConstant(this, Distance);
}

float UParticleModuleTypeDataBeam::EvaluateWidth(float EmitterTime, UObject* Data) const
{
	const float Value = EvalFloatDistribution(WidthDistribution, EmitterTime, Data, Width);
	return Value < 0.0f ? 0.0f : Value;
}

float UParticleModuleTypeDataBeam::EvaluateDistance(float EmitterTime, UObject* Data) const
{
	const float Value = EvalFloatDistribution(DistanceDistribution, EmitterTime, Data, Distance);
	return Value < 0.0f ? 0.0f : Value;
}

FParticleEmitterInstance* UParticleModuleTypeDataBeam::CreateInstance(UParticleSystemComponent* /*InComponent*/)
{
	return new FParticleBeamEmitterInstance();
}
