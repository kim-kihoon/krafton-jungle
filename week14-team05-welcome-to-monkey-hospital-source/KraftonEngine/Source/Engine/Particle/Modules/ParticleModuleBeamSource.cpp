#include "ParticleModuleBeamSource.h"

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionVectorConstant.h"

UParticleModuleBeamSource::UParticleModuleBeamSource()
{
	auto* DefaultSource = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(this);
	if (DefaultSource)
	{
		DefaultSource->Constant = {0.0f, 0.0f, 0.0f};
		SourceDistribution = DefaultSource;
	}

	auto* DefaultTangent = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(this);
	if (DefaultTangent)
	{
		DefaultTangent->Constant = {0.0f, 0.0f, 0.0f};
		SourceTangentDistribution = DefaultTangent;
	}
}

FVector UParticleModuleBeamSource::ResolveSource(const FParticleEmitterInstance* Owner, float EmitterTime, const FVector& DefaultSource) const
{
	if (SourceMethod == EBeam2SourceMethod::Default)
	{
		return DefaultSource;
	}

	if (SourceMethod == EBeam2SourceMethod::Emitter)
	{
		return Owner
			? Owner->ConvertPositionToSimulation(FVector(0.0f, 0.0f, 0.0f), EParticleValueSpace::Local)
			: FVector(0.0f, 0.0f, 0.0f);
	}

	const FVector Source = SourceDistribution
		? SourceDistribution->GetValue(EmitterTime, Owner ? Owner->GetComponent() : nullptr)
		: FVector(0.0f, 0.0f, 0.0f);

	if (!Owner)
	{
		return Source;
	}

	return Owner->ConvertPositionToSimulation(
		Source,
		bSourceAbsolute ? EParticleValueSpace::World : EParticleValueSpace::Local);
}

FVector UParticleModuleBeamSource::ResolveSourceTangent(const FParticleEmitterInstance* Owner, float EmitterTime) const
{
	const FVector Tangent = SourceTangentDistribution
		? SourceTangentDistribution->GetValue(EmitterTime, Owner ? Owner->GetComponent() : nullptr)
		: FVector(0.0f, 0.0f, 0.0f);

	if (!Owner)
	{
		return Tangent;
	}

	return Owner->ConvertVectorToSimulation(
		Tangent,
		bSourceAbsolute ? EParticleValueSpace::World : EParticleValueSpace::Local);
}
