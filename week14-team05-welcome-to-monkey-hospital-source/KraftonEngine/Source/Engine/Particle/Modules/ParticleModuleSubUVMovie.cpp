#include "ParticleModuleSubUVMovie.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "Object/Object.h"
#include "ParticleModuleRequired.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleLODLevel.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Engine/Particle/Distributions/DistributionFloatConstant.h"

namespace
{
	struct FSubUVFrameRange
	{
		int32 Start = 0;
		int32 End = 0;
		int32 Count = 1;
	};

	int32 GetSubUVFrameCount(const UParticleLODLevel* SimulationLOD)
	{
		if (!SimulationLOD || !SimulationLOD->RequiredModule)
		{
			return 0;
		}

		const UParticleModuleRequired* Required = SimulationLOD->RequiredModule;
		return Required->SubImagesHorizontal * Required->SubImagesVertical;
	}

	FSubUVFrameRange BuildFrameRange(const UParticleModuleSubUVMovie& Module, int32 FrameCount)
	{
		FSubUVFrameRange Range;
		const int32 LastFrame = std::max(0, FrameCount - 1);
		Range.Start = std::clamp(Module.StartFrame, 0, LastFrame);
		Range.End = Module.EndFrame < 0 ? LastFrame : std::clamp(Module.EndFrame, 0, LastFrame);
		if (Range.End < Range.Start)
		{
			Range.End = Range.Start;
		}
		Range.Count = std::max(1, Range.End - Range.Start + 1);
		return Range;
	}

	int32 PickRandomOffset(int32 RangeCount)
	{
		if (RangeCount <= 1)
		{
			return 0;
		}

		return std::rand() % RangeCount;
	}

	int32 EvaluateMovieFrame(const UParticleModuleSubUVMovie& Module,
	                       const FParticleEmitterInstance* Owner,
	                       const FBaseParticle& Particle,
	                       const FSubUVFrameRange& Range,
	                       int32 RandomOffset)
	{
		const float RelativeTime = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
		const float FrameRate = Module.FrameRateDistribution
			? Module.FrameRateDistribution->GetValue(RelativeTime, Owner ? Owner->GetComponent() : nullptr)
			: 1.0f;

		int32 SequenceFrame = 0;
		if (FrameRate > 0.0f)
		{
			const float ParticleAge = Particle.OneOverMaxLifetime > 0.0f
				? Particle.RelativeTime / Particle.OneOverMaxLifetime
				: 0.0f;
			SequenceFrame = static_cast<int32>(std::floor(std::max(0.0f, ParticleAge) * FrameRate));
		}
		else
		{
			SequenceFrame = static_cast<int32>(std::floor(RelativeTime * static_cast<float>(Range.Count)));
		}

		const int32 SequenceOffset = std::max(0, SequenceFrame) + RandomOffset;
		const int32 FrameOffset = Module.bLooped
			? (SequenceOffset % Range.Count)
			: std::clamp(SequenceOffset, 0, Range.Count - 1);

		return Range.Start + FrameOffset;
	}
}

UParticleModuleSubUVMovie::UParticleModuleSubUVMovie()
{
	auto* DefaultFrameRate = UObjectManager::Get().CreateObject<UDistributionFloatConstant>(this);
	if (DefaultFrameRate)
	{
		DefaultFrameRate->Constant = 1.0f;
		FrameRateDistribution = DefaultFrameRate;
	}
}

void UParticleModuleSubUVMovie::Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	float SpawnTime, FBaseParticle* Particle)
{
	(void)SpawnTime;

	if (!Particle)
	{
		return;
	}

	FSubUVMovieParticlePayload* Payload =
		PARTICLE_PAYLOAD(Particle, ModuleOffset, FSubUVMovieParticlePayload);
	Payload->RandomFrameOffset = 0;

	const int32 FrameCount = GetSubUVFrameCount(Owner ? Owner->GetParticleSimulationLOD(*Particle) : nullptr);
	if (FrameCount <= 0)
	{
		Particle->SubImageIndex = std::max(0, StartFrame);
		return;
	}

	const FSubUVFrameRange Range = BuildFrameRange(*this, FrameCount);
	Payload->RandomFrameOffset = bRandomStartFrame ? PickRandomOffset(Range.Count) : 0;
	Particle->SubImageIndex = Range.Start + Payload->RandomFrameOffset;
}

void UParticleModuleSubUVMovie::UpdateParticleSubset(
	FParticleEmitterInstance* Owner,
	UParticleLODLevel* SimulationLOD,
	uint32 ModuleOffset,
	float DeltaTime,
	const TArray<uint32>& ParticleIndices)
{
	(void)DeltaTime;

	const int32 FrameCount = GetSubUVFrameCount(SimulationLOD);
	if (!Owner || FrameCount <= 0)
	{
		return;
	}

	const FSubUVFrameRange Range = BuildFrameRange(*this, FrameCount);
	for (uint32 ParticleIndex : ParticleIndices)
	{
		FBaseParticle* Particle = Owner->GetParticleAt(ParticleIndex);
		if (!Particle)
		{
			continue;
		}

		const FSubUVMovieParticlePayload* Payload =
			PARTICLE_PAYLOAD_CONST(Particle, ModuleOffset, FSubUVMovieParticlePayload);
		const int32 RandomOffset = bRandomStartFrame ? Payload->RandomFrameOffset : 0;

		Particle->SubImageIndex = EvaluateMovieFrame(*this, Owner, *Particle, Range, RandomOffset);
	}
}

uint32 UParticleModuleSubUVMovie::RequiredBytes(UParticleLODLevel* LODLevel) const
{
	(void)LODLevel;
	return sizeof(FSubUVMovieParticlePayload);
}
