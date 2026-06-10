#include "ParticleModuleBeamTarget.h"

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionVectorConstant.h"

UParticleModuleBeamTarget::UParticleModuleBeamTarget()
{
	auto* DefaultTarget = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(this);
	if (DefaultTarget)
	{
		DefaultTarget->Constant = {1.0f, 0.0f, 0.0f};
		TargetDistribution = DefaultTarget;
	}

	auto* DefaultTangent = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(this);
	if (DefaultTangent)
	{
		DefaultTangent->Constant = {0.0f, 0.0f, 0.0f};
		TargetTangentDistribution = DefaultTangent;
	}
}

FVector UParticleModuleBeamTarget::ResolveTarget(const FParticleEmitterInstance* Owner, float EmitterTime,
                                                const FVector& Source, const FVector& DefaultTarget,
                                                float Distance) const
{
	if (TargetMethod == EBeam2TargetMethod::Default)
	{
		return DefaultTarget;
	}

	if (TargetMethod == EBeam2TargetMethod::Emitter)
	{
		return Owner
			? Owner->ConvertPositionToSimulation(FVector(0.0f, 0.0f, 0.0f), EParticleValueSpace::Local)
			: FVector(0.0f, 0.0f, 0.0f);
	}

	if (TargetMethod == EBeam2TargetMethod::Distance)
	{
		const FVector LocalXDistance(Distance, 0.0f, 0.0f);
		const FVector SimulationDelta = Owner
			? Owner->ConvertVectorToSimulation(LocalXDistance, EParticleValueSpace::Local)
			: LocalXDistance;
		return Source + SimulationDelta;
	}

	const FVector Target = TargetDistribution
		? TargetDistribution->GetValue(EmitterTime, Owner ? Owner->GetComponent() : nullptr)
		: FVector(1.0f, 0.0f, 0.0f);

	if (!Owner)
	{
		return Target;
	}

	return Owner->ConvertPositionToSimulation(
		Target,
		bTargetAbsolute ? EParticleValueSpace::World : EParticleValueSpace::Local);
}

FVector UParticleModuleBeamTarget::ResolveTargetTangent(const FParticleEmitterInstance* Owner, float EmitterTime) const
{
	const FVector Tangent = TargetTangentDistribution
		? TargetTangentDistribution->GetValue(EmitterTime, Owner ? Owner->GetComponent() : nullptr)
		: FVector(0.0f, 0.0f, 0.0f);

	if (!Owner)
	{
		return Tangent;
	}

	return Owner->ConvertVectorToSimulation(
		Tangent,
		bTargetAbsolute ? EParticleValueSpace::World : EParticleValueSpace::Local);
}
