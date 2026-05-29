#include "ParticleBeamInstances.h"

#include "Component/ParticleSystemComponent.h"
#include "Materials/Material.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleHelper.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam2.h"
#include "Particle/BeamModule/ParticleModuleBeamNoise.h"
#include "Particle/BeamModule/ParticleModuleBeamSource.h"
#include "Particle/BeamModule/ParticleModuleBeamTarget.h"

#include <algorithm>

namespace
{
bool ResolveDefaultBeamEndpoints(const UParticleModuleTypeDataBeam2& BeamModule, FVector& OutSource, FVector& OutTarget)
{
	OutSource = BeamModule.SourcePoint;
	OutTarget = BeamModule.TargetPoint;

	switch (BeamModule.BeamMethod)
	{
	case PEB2M_Distance:
		OutTarget = BeamModule.SourcePoint + FVector(BeamModule.Distance, 0.0f, 0.0f);
		return true;
	case PEB2M_Target:
		return true;
	case PEB2M_Branch:
		// Branching is deferred; keep the explicit target fallback for now.
		return true;
	default:
		return false;
	}
}

float CalculateBeamProgress(float TravelTime, float Speed, const FVector& WorldSource, const FVector& WorldTarget)
{
	const float FullBeamLength = (WorldTarget - WorldSource).Length();
	return (Speed > 0.0f && FullBeamLength > 1e-6f)
		? std::clamp((TravelTime * Speed) / FullBeamLength, 0.0f, 1.0f)
		: 1.0f;
}

float CalculateParticleAge(const FBaseParticle* Particle, float FallbackAge)
{
	return (Particle && Particle->OneOverMaxLifetime > 1e-6f)
		? std::max(0.0f, Particle->RelativeTime / Particle->OneOverMaxLifetime)
		: FallbackAge;
}

}

// O(2n) loop. The base class handles the generic “math” of the offsets,
// while the specialized beam class handles the “logic” of identifying and caching those offsets for the beam structure.
void FParticleBeam2EmitterInstance::InitParameters(UParticleEmitter* InTemplate)
{
	// 1. First, call the base initialization.
	FParticleEmitterInstance::InitParameters(InTemplate);

	// 2. Scan the Emitter's module stack for Beam-specific modules
	auto* LOD = InTemplate->GetLODLevel(0);
	if (!LOD) return;

	BeamModule = Cast<UParticleModuleTypeDataBeam2>(LOD->TypeDataModule);

	for (uint16 i = 0; i < LOD->Modules.size(); i++)
	{
		auto* Module = LOD->Modules[i];
		// Cache the Source module if found
		if (Module->IsA(UParticleModuleBeamSource::StaticClass()))
		{
			BeamSourceModule = Cast<UParticleModuleBeamSource>(Module);
		}

		// Cache the Target module if found
		if (Module->IsA(UParticleModuleBeamTarget::StaticClass()))
		{
			BeamTargetModule = Cast<UParticleModuleBeamTarget>(Module);
		}

		// Cache the Noise module if found. Unlike Source/Target, BeamNoise
		// owns its own per-particle payload (FBeamNoisePayloadData) — pointer
		// into NoisePointArena. RequiredBytes / Spawn / Update wiring lands
		// in Chunk B.
		if (Module->IsA(UParticleModuleBeamNoise::StaticClass()))
		{
			BeamNoiseModule = Cast<UParticleModuleBeamNoise>(Module);
		}
	}
}

void FParticleBeam2EmitterInstance::Tick(float DeltaTime, int32 LODLevel, bool bSuppressSpawning)
{
	FParticleEmitterInstance::Tick(DeltaTime, LODLevel, bSuppressSpawning);
	if (!CurrentLODLevel) return;

	if (BeamModule && BeamModule->bAlwaysOn && !bSuppressSpawning)
	{
		const int32 DesiredBeamCount = std::max(1, BeamModule->MaxBeamCount);
		if (ActiveParticles < DesiredBeamCount)
		{
			SpawnParticles(DesiredBeamCount - ActiveParticles, 0.0f, 0.0f,
				Location, FVector::ZeroVector, nullptr);
		}
	}

	BeamTravelTime += DeltaTime;

	UParticleModule::FUpdateContext Context(*this, TypeDataOffset, DeltaTime);
	if (BeamSourceModule) BeamSourceModule->Update(Context);
	if (BeamTargetModule) BeamTargetModule->Update(Context);
	if (BeamNoiseModule)  BeamNoiseModule->Update(Context);
}

FParticleEmitterInstance* FParticleBeam2EmitterInstance::FindSiblingEmitter(const FName& Name) const
{
	if (!Name.IsValid() || !Component)
	{
		return nullptr;
	}

	for (FParticleEmitterInstance* Sibling : Component->EmitterInstances)
	{
		if (!Sibling || Sibling == this || !Sibling->EmitterTemplate)
		{
			continue;
		}
		if (Sibling->EmitterTemplate->GetEmitterName() == Name)
		{
			return Sibling;
		}
	}
	return nullptr;
}

const FBeam2TypeDataPayload* FParticleBeam2EmitterInstance::GetBeamPayload(const FBaseParticle* Particle) const
{
	const int32 PayloadEnd = TypeDataOffset + static_cast<int32>(sizeof(FBeam2TypeDataPayload));
	if (!Particle || TypeDataOffset < static_cast<int32>(sizeof(FBaseParticle)) || PayloadEnd > ParticleSize)
	{
		return nullptr;
	}

	return reinterpret_cast<const FBeam2TypeDataPayload*>(
		reinterpret_cast<const uint8*>(Particle) + TypeDataOffset);
}

FBeam2TypeDataPayload* FParticleBeam2EmitterInstance::GetBeamPayload(FBaseParticle* Particle) const
{
	return const_cast<FBeam2TypeDataPayload*>(
		GetBeamPayload(static_cast<const FBaseParticle*>(Particle)));
}

FDynamicEmitterReplayDataBase* FParticleBeam2EmitterInstance::GetReplayData()
{
	if (!CurrentLODLevel || !Component)
	{
		return nullptr;
	}

	if (!BeamModule || (!BeamModule->bAlwaysOn && ActiveParticles <= 0))
	{
		return nullptr;
	}

	FVector FallbackLocalSource = FVector::ZeroVector;
	FVector FallbackLocalTarget = FVector::ZeroVector;
	if (!ResolveDefaultBeamEndpoints(*BeamModule, FallbackLocalSource, FallbackLocalTarget))
	{
		return nullptr;
	}

	const FMatrix& ComponentToWorld = Component->GetWorldMatrix();
	FBeamResolveContext FallbackResolveContext{ *this, *BeamModule, ComponentToWorld, nullptr, 0, 1.0f };
	if (BeamSourceModule && BeamSourceModule->bEnabled)
	{
		FallbackLocalSource = BeamSourceModule->ResolveSource(FallbackResolveContext);
	}
	if (BeamTargetModule && BeamTargetModule->bEnabled)
	{
		FallbackLocalTarget = BeamTargetModule->ResolveTarget(FallbackResolveContext, FallbackLocalSource);
	}

	const int32 SheetCount = std::max(1, BeamModule->Sheets);
	const int32 MaxBeamCount = std::max(1, BeamModule->MaxBeamCount);
	const int32 LogicalBeamCount = BeamModule->bAlwaysOn
		? MaxBeamCount
		: std::clamp(ActiveParticles, 0, MaxBeamCount);

	FDynamicBeamEmitterReplayData* NewEmitterReplayData = new FDynamicBeamEmitterReplayData();
	NewEmitterReplayData->LogicalBeamCount = LogicalBeamCount;
	NewEmitterReplayData->ParticleStride = 0;
	NewEmitterReplayData->Scale = FVector::OneVector;
	NewEmitterReplayData->InterpolationPoints = std::max(0, BeamModule->InterpolationPoints);
	NewEmitterReplayData->Sheets = SheetCount;
	NewEmitterReplayData->MaxBeamCount = MaxBeamCount;
	NewEmitterReplayData->UpVectorStepSize = std::max(0, BeamModule->UpVectorStepSize);
	NewEmitterReplayData->TextureTile = std::max(1, BeamModule->TextureTile);
	NewEmitterReplayData->TextureTileDistance = std::max(0.0f, BeamModule->TextureTileDistance);
	NewEmitterReplayData->bRenderDirectLine = BeamModule->bRenderDirectLine;
	NewEmitterReplayData->bRenderGeometry = BeamModule->bRenderGeometry;
	NewEmitterReplayData->bRenderLines = BeamModule->bRenderLines;
	NewEmitterReplayData->bRenderTessellation = BeamModule->bRenderTessellation;
	NewEmitterReplayData->BranchParentName = BeamModule->BranchParentName;
	NewEmitterReplayData->TargetData = BeamModule->TargetData;
	NewEmitterReplayData->ActiveParticleCount = LogicalBeamCount * SheetCount;

	const int32 NoiseOffset = (BeamNoiseModule && BeamNoiseModule->bEnabled && BeamNoiseModule->Frequency > 0)
		? static_cast<int32>(GetModuleDataOffset(BeamNoiseModule))
		: 0;
	const int32 NoiseFrequency = NoiseOffset > 0 ? BeamNoiseModule->Frequency : 0;

	NewEmitterReplayData->Beams.reserve(LogicalBeamCount);
	for (int32 i = 0; i < LogicalBeamCount; ++i)
	{
		const int32 ActiveParticleIndex = (ActiveParticles > 0 && ParticleIndices)
			? i % ActiveParticles
			: -1;
		FBaseParticle* Particle = (ActiveParticleIndex >= 0)
			? GetParticleDirect(ParticleIndices[ActiveParticleIndex])
			: nullptr;
		const FBeam2TypeDataPayload* Payload = GetBeamPayload(Particle);
		const FVector LocalSource = Payload ? Payload->SourcePoint : FallbackLocalSource;
		const FVector LocalTarget = Payload ? Payload->TargetPoint : FallbackLocalTarget;
		const FVector LocalBeamDelta = LocalTarget - LocalSource;
		const FVector LocalSourceTangent = Payload ? Payload->SourceTangent : LocalBeamDelta;
		const FVector LocalTargetTangent = Payload ? Payload->TargetTangent : LocalBeamDelta;
		const FVector WorldSource = ComponentToWorld.TransformPositionWithW(LocalSource);
		const FVector WorldTarget = ComponentToWorld.TransformPositionWithW(LocalTarget);
		const FVector BeamColor = Particle
			? FVector(Particle->Color.R * BeamModule->Color.X,
			          Particle->Color.G * BeamModule->Color.Y,
			          Particle->Color.B * BeamModule->Color.Z)
			: BeamModule->Color;
		const float BeamAlpha = Particle
			? Particle->Color.A * BeamModule->Alpha
			: BeamModule->Alpha;
		const float WidthScale = Particle ? std::max(0.0f, Particle->Size.X) : 1.0f;

		FBeamInstanceData Beam;
		Beam.Source       = WorldSource;
		Beam.Target       = WorldTarget;
		Beam.Color        = BeamColor;
		Beam.Alpha        = std::clamp(BeamAlpha, 0.0f, 1.0f);
		Beam.Width        = BeamModule->Width * WidthScale;
		Beam.TaperMethod  = BeamModule->TaperMethod;
		Beam.TaperFactor  = BeamModule->TaperFactor;
		Beam.TaperScale   = BeamModule->TaperScale;
		Beam.BeamProgress = CalculateBeamProgress(
			CalculateParticleAge(Particle, BeamTravelTime), BeamModule->Speed, Beam.Source, Beam.Target);
		Beam.SourceTangent = ComponentToWorld.TransformVector(LocalSourceTangent);
		Beam.TargetTangent = ComponentToWorld.TransformVector(LocalTargetTangent);
		Beam.SourceStrength = Payload ? std::max(0.0f, Payload->SourceStrength) : 1.0f;
		Beam.TargetStrength = Payload ? std::max(0.0f, Payload->TargetStrength) : 1.0f;

		if (Particle && NoiseOffset > 0)
		{
			const auto* NoisePayload = reinterpret_cast<const FBeamNoisePayloadData*>(
				reinterpret_cast<const uint8*>(Particle) + NoiseOffset);
			if (NoisePayload->NoisePoints)
			{
				const int32 NoisePointCount = std::clamp(NoisePayload->NoiseCount, 0, NoiseFrequency);
				Beam.NoisePoints.reserve(NoisePointCount);
				for (int32 N = 0; N < NoisePointCount; ++N)
				{
					Beam.NoisePoints.push_back(
						ComponentToWorld.TransformPositionWithW(NoisePayload->NoisePoints[N]));
				}
			}
		}

		NewEmitterReplayData->Beams.push_back(Beam);
	}

	if (CurrentLODLevel->RequiredModule)
	{
		NewEmitterReplayData->SortMode = CurrentLODLevel->RequiredModule->SortMode;
		NewEmitterReplayData->MaterialInterface = CurrentLODLevel->RequiredModule->Material;
		if (UMaterial* Material = CurrentLODLevel->RequiredModule->Material
			? CurrentLODLevel->RequiredModule->Material->GetMaterial()
			: nullptr)
		{
			NewEmitterReplayData->BlendMode = Material->GetBlendState();
		}
	}

	return NewEmitterReplayData;
}
