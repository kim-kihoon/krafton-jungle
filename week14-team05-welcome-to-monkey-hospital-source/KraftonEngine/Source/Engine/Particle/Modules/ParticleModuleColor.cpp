#include "ParticleModuleColor.h"

#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"

void UParticleModuleColor::Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
                                 float SpawnTime, FBaseParticle* Particle)
{
	(void)ModuleOffset;

	if (!Particle) return;

	UObject* EvalData = Owner ? Owner->GetComponent() : nullptr;
	const FVector RGB = StartColorDistribution
		? StartColorDistribution->GetValue(SpawnTime, EvalData)
		: FVector(StartColor.X, StartColor.Y, StartColor.Z);
	const float Alpha = StartAlphaDistribution
		? StartAlphaDistribution->GetValue(SpawnTime, EvalData)
		: StartColor.W;

	const FVector4 InitialColor = { RGB.X, RGB.Y, RGB.Z, Alpha };
	Particle->Color = InitialColor;
	Particle->BaseColor = InitialColor;
}
