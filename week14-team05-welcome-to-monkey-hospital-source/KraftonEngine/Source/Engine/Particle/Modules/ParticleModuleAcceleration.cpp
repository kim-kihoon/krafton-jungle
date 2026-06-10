#include "ParticleModuleAcceleration.h"

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionVectorConstant.h"

UParticleModuleAcceleration::UParticleModuleAcceleration()
{
	auto* DefaultAcceleration = UObjectManager::Get().CreateObject<UDistributionVectorConstant>(this);
	if (DefaultAcceleration)
	{
		DefaultAcceleration->Constant = {0, 0, -9.8f};
		AccelerationDistribution = DefaultAcceleration;
	}
}

void UParticleModuleAcceleration::Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset, float SpawnTime, FBaseParticle* Particle)
{
	if (!Particle)
	{
		return;
	}

	FAccelerationParticlePayload* Payload = PARTICLE_PAYLOAD(Particle, ModuleOffset, FAccelerationParticlePayload);
	if (!Payload)
	{
		return;
	}

	Payload->Acceleration = AccelerationDistribution
		? AccelerationDistribution->GetValue(SpawnTime, Owner ? Owner->GetComponent() : nullptr)
		: FVector(0.0f, 0.0f, 0.0f);
}

void UParticleModuleAcceleration::UpdateParticle(
	FParticleEmitterInstance* Owner,
	UParticleLODLevel* SimulationLOD,
	uint32 ModuleOffset,
	float DeltaTime,
	FBaseParticle* Particle)
{
	(void)SimulationLOD;

	if (!Owner)
	{
		return;
	}

	if (!Particle)
	{
		return;
	}

	const FAccelerationParticlePayload* Payload = PARTICLE_PAYLOAD_CONST(Particle, ModuleOffset, FAccelerationParticlePayload);
	const FVector SampledAcceleration = Payload ? Payload->Acceleration : FVector(0.0f, 0.0f, 0.0f);
	Particle->Velocity += SampledAcceleration * DeltaTime;
}
