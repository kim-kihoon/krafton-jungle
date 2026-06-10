#include "ParticleModuleColorOverLife.h"

#include <algorithm>

#include "Object/Object.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionFloatCurve.h"
#include "Engine/Particle/Distributions/DistributionVectorCurve.h"

UParticleModuleColorOverLife::UParticleModuleColorOverLife()
{
	auto* DefaultColor = UObjectManager::Get().CreateObject<UDistributionVectorCurve>(this);
	if (DefaultColor)
	{
		DefaultColor->SetConstant(FVector(1.0f, 1.0f, 1.0f));
		ColorOverLifeDistribution = DefaultColor;
	}

	auto* DefaultAlpha = UObjectManager::Get().CreateObject<UDistributionFloatCurve>(this);
	if (DefaultAlpha)
	{
		DefaultAlpha->SetLinear(0.0f, 1.0f, 1.0f, 0.0f);
		AlphaOverLifeDistribution = DefaultAlpha;
	}
}

void UParticleModuleColorOverLife::UpdateParticle(
	FParticleEmitterInstance* Owner,
	UParticleLODLevel* SimulationLOD,
	uint32 ModuleOffset,
	float DeltaTime,
	FBaseParticle* Particle)
{
	(void)SimulationLOD;
	(void)ModuleOffset;
	(void)DeltaTime;

	if (!Owner || !Particle) return;

	const float T = std::clamp(Particle->RelativeTime, 0.0f, 1.0f);
	const FVector LifeRGB = ColorOverLifeDistribution
		? ColorOverLifeDistribution->GetValue(T, Owner->GetComponent())
		: FVector(1.0f, 1.0f, 1.0f);
	const float LifeAlpha = AlphaOverLifeDistribution
		? AlphaOverLifeDistribution->GetValue(T, Owner->GetComponent())
		: 1.0f;

	if (bMultiplyBaseColor)
	{
		Particle->Color.X = Particle->BaseColor.X * LifeRGB.X;
		Particle->Color.Y = Particle->BaseColor.Y * LifeRGB.Y;
		Particle->Color.Z = Particle->BaseColor.Z * LifeRGB.Z;
		Particle->Color.W = Particle->BaseColor.W * LifeAlpha;
	}
	else
	{
		Particle->Color = FVector4(LifeRGB.X, LifeRGB.Y, LifeRGB.Z, LifeAlpha);
	}
}
