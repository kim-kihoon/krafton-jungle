#include "RibbonEmitterInstance.h"

#include "Component/ParticleSystemComponent.h"
#include "Materials/Material.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"

#include <algorithm>
#include <cmath>

namespace
{
struct FRibbonBuildPoint
{
	const FBaseParticle* Particle = nullptr;
	const FRibbonParticlePayload* Payload = nullptr;
	int32 TrailIndex = 0;
};

bool IsExplicitSourceName(const FName& Name)
{
	return Name.IsValid() && Name != FName::None;
}

bool IsSpriteEmitterInstance(const FParticleEmitterInstance* Instance)
{
	if (!Instance || !Instance->EmitterTemplate)
	{
		return false;
	}

	const UParticleLODLevel* LOD = Instance->CurrentLODLevel
		? Instance->CurrentLODLevel
		: Instance->EmitterTemplate->GetBestLODLevel(0);
	const UParticleModuleTypeDataBase* TypeData = LOD ? LOD->TypeDataModule : nullptr;
	return !TypeData
		|| (!TypeData->IsAMeshEmitter() && !TypeData->IsABeamEmitter() && !TypeData->IsARibbonEmitter());
}

void FillRibbonReplaySettings(FDynamicRibbonEmitterReplayData& ReplayData,
	const UParticleModuleTypeDataRibbon& RibbonModule,
	const UParticleSystemComponent* Component)
{
	ReplayData.ParticleStride = 0;
	ReplayData.Scale = FVector::OneVector;
	ReplayData.SheetsPerTrail = std::max(1, RibbonModule.SheetsPerTrail);
	ReplayData.MaxTessellationBetweenParticles = std::max(0, RibbonModule.MaxTessellationBetweenParticles);
	ReplayData.RenderAxisOption = RibbonModule.RenderAxis;
	ReplayData.SourceUpVector = Component ? Component->GetUpVector() : FVector::UpVector;
	ReplayData.TilingDistance = std::max(0.0f, RibbonModule.TilingDistance);
	ReplayData.DistanceTessellationStepSize = std::max(0.0f, RibbonModule.DistanceTessellationStepSize);
	ReplayData.bRenderGeometry = RibbonModule.bRenderGeometry;
	ReplayData.bRenderSpawnPoints = RibbonModule.bRenderSpawnPoints;
	ReplayData.bRenderTangents = RibbonModule.bRenderTangents;
	ReplayData.bRenderTessellation = RibbonModule.bRenderTessellation;
}

void FillRibbonMaterial(FDynamicRibbonEmitterReplayData& ReplayData, const UParticleLODLevel* CurrentLODLevel)
{
	if (!CurrentLODLevel || !CurrentLODLevel->RequiredModule)
	{
		return;
	}

	ReplayData.SortMode = CurrentLODLevel->RequiredModule->SortMode;
	ReplayData.MaterialInterface = CurrentLODLevel->RequiredModule->Material;
	if (UMaterial* Material = CurrentLODLevel->RequiredModule->Material
		? CurrentLODLevel->RequiredModule->Material->GetMaterial()
		: nullptr)
	{
		const EBlendState MaterialBlend = Material->GetBlendState();
		ReplayData.BlendMode = MaterialBlend == EBlendState::Additive
			? EBlendState::Additive
			: EBlendState::AlphaBlend;
	}
}

float DistanceSquared(const FVector& A, const FVector& B)
{
	const FVector Delta = A - B;
	return Delta.Dot(Delta);
}
}

FRibbonEmitterInstance::FRibbonEmitterInstance(UParticleSystemComponent* InComponent)
	: FParticleEmitterInstance(InComponent)
{
}

void FRibbonEmitterInstance::Tick(float DeltaTime, int32 LODLevel, bool bSuppressSpawning)
{
	if (!IsSourceRibbonEnabled())
	{
		SourceTrails.clear();
		FParticleEmitterInstance::Tick(DeltaTime, LODLevel, bSuppressSpawning);
		return;
	}

	FParticleEmitterInstance::Tick(DeltaTime, LODLevel, true); //ribbon Emitter 자체의 spawn rate로 소환하지 않는다.
	if (UParticleModuleTypeDataRibbon* RibbonModule = GetRibbonModule())
	{
		UpdateSourceTrails(DeltaTime, *RibbonModule);
	}
}

void FRibbonEmitterInstance::PostSpawn(FBaseParticle* Particle, float Interp, float SpawnTime)
{
	FParticleEmitterInstance::PostSpawn(Particle, Interp, SpawnTime);

	if (!Particle || TypeDataOffset <= 0)
	{
		return;
	}

	auto* Payload = reinterpret_cast<FRibbonParticlePayload*>(reinterpret_cast<uint8*>(Particle) + TypeDataOffset);
	Payload->SpawnSequence = RibbonSpawnSequence++;
	Payload->TrailIndex = 0;
}

FDynamicEmitterReplayDataBase* FRibbonEmitterInstance::GetReplayData()
{
	UParticleModuleTypeDataRibbon* RibbonModule = GetRibbonModule();
	if (RibbonModule && RibbonModule->bUseSourceEmitter)
	{
		return GetSourceReplayData(*RibbonModule);
	}

	if (!CurrentLODLevel
		|| !CurrentLODLevel->TypeDataModule
		|| !Component
		|| !ParticleIndices
		|| ActiveParticles < 2
		|| TypeDataOffset <= 0)
	{
		return nullptr;
	}

	if (!RibbonModule || !RibbonModule->bRenderGeometry)
	{
		return nullptr;
	}

	TArray<FRibbonBuildPoint> BuildPoints;
	BuildPoints.reserve(ActiveParticles);
	for (int32 ActiveIndex = 0; ActiveIndex < ActiveParticles; ++ActiveIndex)
	{
		const FBaseParticle* Particle = GetParticleDirect(ParticleIndices[ActiveIndex]);
		if (!Particle)
		{
			continue;
		}

		const FRibbonParticlePayload* Payload = reinterpret_cast<const FRibbonParticlePayload*>(
			reinterpret_cast<const uint8*>(Particle) + TypeDataOffset);

		FRibbonBuildPoint BuildPoint;
		BuildPoint.Particle = Particle;
		BuildPoint.Payload = Payload;
		BuildPoint.TrailIndex = Payload->TrailIndex;
		BuildPoints.push_back(BuildPoint);
	}

	if (BuildPoints.size() < 2)
	{
		return nullptr;
	}

	std::stable_sort(BuildPoints.begin(), BuildPoints.end(),
		[](const FRibbonBuildPoint& A, const FRibbonBuildPoint& B)
		{
			if (A.TrailIndex != B.TrailIndex)
			{
				return A.TrailIndex < B.TrailIndex;
			}
			return A.Payload->SpawnSequence < B.Payload->SpawnSequence;
		});

	FDynamicRibbonEmitterReplayData* ReplayData = new FDynamicRibbonEmitterReplayData();
	FillRibbonReplaySettings(*ReplayData, *RibbonModule, Component);

	int32 ConsumedTrailCount = 0;
	for (int32 StartIndex = 0; StartIndex < static_cast<int32>(BuildPoints.size()) && ConsumedTrailCount < RibbonModule->MaxTrailCount;)
	{
		const int32 TrailIndex = BuildPoints[StartIndex].TrailIndex;
		int32 EndIndex = StartIndex + 1;
		while (EndIndex < static_cast<int32>(BuildPoints.size()) && BuildPoints[EndIndex].TrailIndex == TrailIndex)
		{
			++EndIndex;
		}

		const int32 AvailablePointCount = EndIndex - StartIndex;
		if (AvailablePointCount >= 2)
		{
			const int32 ClippedStartIndex = EndIndex - std::min(AvailablePointCount, RibbonModule->MaxParticleInTrailCount);
			FRibbonTrailSection TrailSection;
			TrailSection.FirstPoint = static_cast<int32>(ReplayData->Points.size());

			float DistanceFromStart = 0.0f;
			FVector PreviousPosition = FVector::ZeroVector;
			for (int32 BuildIndex = ClippedStartIndex; BuildIndex < EndIndex; ++BuildIndex)
			{
				const FBaseParticle* Particle = BuildPoints[BuildIndex].Particle;
				const FRibbonParticlePayload* Payload = BuildPoints[BuildIndex].Payload;
				if (!Particle || !Payload)
				{
					continue;
				}

				if (TrailSection.PointCount > 0)
				{
					DistanceFromStart += (PreviousPosition - Particle->Location).Length();
				}

				FRibbonPointData PointData;
				PointData.Position = Particle->Location;
				PointData.Color = FLinearColor(
					Particle->Color.R * RibbonModule->Color.X,
					Particle->Color.G * RibbonModule->Color.Y,
					Particle->Color.B * RibbonModule->Color.Z,
					std::clamp(Particle->Color.A * RibbonModule->Alpha, 0.0f, 1.0f));
				PointData.Width = std::max(0.0f, RibbonModule->Width * std::max(0.0f, Particle->Size.X));
				PointData.DistanceFromStart = DistanceFromStart;
				PointData.SpawnSequence = Payload->SpawnSequence;
				ReplayData->Points.push_back(PointData);

				PreviousPosition = Particle->Location;
				++TrailSection.PointCount;
			}

			if (TrailSection.PointCount >= 2)
			{
				ReplayData->Trails.push_back(TrailSection);
				++ConsumedTrailCount;
			}
			else
			{
				ReplayData->Points.resize(TrailSection.FirstPoint);
			}
		}

		StartIndex = EndIndex;
	}

	if (ReplayData->Trails.empty())
	{
		delete ReplayData;
		return nullptr;
	}

	ReplayData->ActiveParticleCount = static_cast<int32>(ReplayData->Points.size());
	FillRibbonMaterial(*ReplayData, CurrentLODLevel);

	return ReplayData;
}

bool FRibbonEmitterInstance::IsSourceRibbonEnabled() const
{
	const UParticleModuleTypeDataRibbon* RibbonModule = GetRibbonModule();
	return RibbonModule && RibbonModule->bUseSourceEmitter;
}

UParticleModuleTypeDataRibbon* FRibbonEmitterInstance::GetRibbonModule() const
{
	if (!CurrentLODLevel || !CurrentLODLevel->TypeDataModule)
	{
		return nullptr;
	}

	return Cast<UParticleModuleTypeDataRibbon>(CurrentLODLevel->TypeDataModule);
}

FParticleEmitterInstance* FRibbonEmitterInstance::FindSourceEmitter(const UParticleModuleTypeDataRibbon& RibbonModule) const
{
	if (!Component)
	{
		return nullptr;
	}

	const bool bHasExplicitName = IsExplicitSourceName(RibbonModule.SourceEmitterName);
	FParticleEmitterInstance* FirstSpriteEmitter = nullptr;

	for (FParticleEmitterInstance* Sibling : Component->EmitterInstances)
	{
		if (!Sibling || Sibling == this || !Sibling->EmitterTemplate || !IsSpriteEmitterInstance(Sibling))
		{
			continue;
		}

		if (!FirstSpriteEmitter)
		{
			FirstSpriteEmitter = Sibling;
		}

		if (bHasExplicitName && Sibling->EmitterTemplate->GetEmitterName() == RibbonModule.SourceEmitterName)
		{
			return Sibling;
		}
	}

	return bHasExplicitName ? nullptr : FirstSpriteEmitter;
}

void FRibbonEmitterInstance::UpdateSourceTrails(float DeltaTime, const UParticleModuleTypeDataRibbon& RibbonModule)
{
	DeltaTime = std::max(0.0f, DeltaTime);

	for (auto& Pair : SourceTrails)
	{
		FRibbonSourceTrail& Trail = Pair.second;
		Trail.TimeSinceLastSample += DeltaTime;
		for (FRibbonSourceSample& Sample : Trail.Samples)
		{
			Sample.Age += DeltaTime;
		}
	}

	FParticleEmitterInstance* SourceEmitter = FindSourceEmitter(RibbonModule);
	if (SourceEmitter && SourceEmitter->ParticleIndices && SourceEmitter->ActiveParticles > 0)
	{
		const int32 MaxTrailCount = std::max(1, RibbonModule.MaxTrailCount);
		//for
		for (int32 ActiveIndex = 0; ActiveIndex < SourceEmitter->ActiveParticles; ++ActiveIndex)
		{
			const FBaseParticle* SourceParticle = SourceEmitter->GetParticleDirect(SourceEmitter->ParticleIndices[ActiveIndex]);
			if (!SourceParticle || SourceParticle->ParticleId == 0)
			{
				continue;
			}

			auto FoundTrail = SourceTrails.find(SourceParticle->ParticleId);
			if (FoundTrail == SourceTrails.end())
			{
				if (static_cast<int32>(SourceTrails.size()) >= MaxTrailCount)
				{
					continue;
				}

				FRibbonSourceTrail NewTrail;
				NewTrail.SourceParticleId = SourceParticle->ParticleId;
				NewTrail.TrailIndex = NextSourceTrailIndex++;
				NewTrail.LastSamplePosition = SourceParticle->Location;
				FoundTrail = SourceTrails.emplace(SourceParticle->ParticleId, std::move(NewTrail)).first;
				AppendSourceSample(FoundTrail->second, *SourceParticle, RibbonModule, true);
				continue;
			}

			FRibbonSourceTrail& Trail = FoundTrail->second;
			const float MinDistance = std::max(0.0f, RibbonModule.SourceMinSampleDistance);
			const bool bMovedEnough = DistanceSquared(Trail.LastSamplePosition, SourceParticle->Location) >= MinDistance * MinDistance;
			const bool bWaitedEnough = Trail.TimeSinceLastSample >= std::max(0.0f, RibbonModule.SourceSampleInterval);
			AppendSourceSample(Trail, *SourceParticle, RibbonModule, bMovedEnough || bWaitedEnough);
		}
	}

	PruneSourceTrails(RibbonModule);
}

FDynamicEmitterReplayDataBase* FRibbonEmitterInstance::GetSourceReplayData(const UParticleModuleTypeDataRibbon& RibbonModule)
{
	if (!RibbonModule.bRenderGeometry || SourceTrails.empty())
	{
		return nullptr;
	}

	FDynamicRibbonEmitterReplayData* ReplayData = new FDynamicRibbonEmitterReplayData();
	FillRibbonReplaySettings(*ReplayData, RibbonModule, Component);

	TArray<const FRibbonSourceTrail*> SortedTrails;
	SortedTrails.reserve(SourceTrails.size());
	for (const auto& Pair : SourceTrails)
	{
		if (Pair.second.Samples.size() >= 2)
		{
			SortedTrails.push_back(&Pair.second);
		}
	}

	std::stable_sort(SortedTrails.begin(), SortedTrails.end(),
		[](const FRibbonSourceTrail* A, const FRibbonSourceTrail* B)
		{
			return A->TrailIndex < B->TrailIndex;
		});

	const float Lifetime = std::max(0.001f, RibbonModule.SourceTrailLifetime);
	int32 ConsumedTrailCount = 0;
	for (const FRibbonSourceTrail* Trail : SortedTrails)
	{
		if (!Trail || ConsumedTrailCount >= RibbonModule.MaxTrailCount)
		{
			break;
		}

		FRibbonTrailSection TrailSection;
		TrailSection.FirstPoint = static_cast<int32>(ReplayData->Points.size());

		float DistanceFromStart = 0.0f;
		FVector PreviousPosition = FVector::ZeroVector;
		uint32 SpawnSequence = 0;
		for (const FRibbonSourceSample& Sample : Trail->Samples)
		{
			const float Fade = std::clamp(1.0f - Sample.Age / Lifetime, 0.0f, 1.0f);
			if (Fade <= 0.0f)
			{
				continue;
			}

			if (TrailSection.PointCount > 0)
			{
				DistanceFromStart += (Sample.Position - PreviousPosition).Length();
			}

			FRibbonPointData PointData;
			PointData.Position = Sample.Position;
			PointData.Color = FLinearColor(
				Sample.Color.R * RibbonModule.Color.X,
				Sample.Color.G * RibbonModule.Color.Y,
				Sample.Color.B * RibbonModule.Color.Z,
				std::clamp(Sample.Color.A * RibbonModule.Alpha * Fade, 0.0f, 1.0f));
			PointData.Width = std::max(0.0f, Sample.Width * Fade);
			PointData.DistanceFromStart = DistanceFromStart;
			PointData.SpawnSequence = SpawnSequence++;
			ReplayData->Points.push_back(PointData);

			PreviousPosition = Sample.Position;
			++TrailSection.PointCount;
		}

		if (TrailSection.PointCount >= 2)
		{
			ReplayData->Trails.push_back(TrailSection);
			++ConsumedTrailCount;
		}
		else
		{
			ReplayData->Points.resize(TrailSection.FirstPoint);
		}
	}

	if (ReplayData->Trails.empty())
	{
		delete ReplayData;
		return nullptr;
	}

	ReplayData->ActiveParticleCount = static_cast<int32>(ReplayData->Points.size());
	FillRibbonMaterial(*ReplayData, CurrentLODLevel);
	return ReplayData;
}

void FRibbonEmitterInstance::AppendSourceSample(FRibbonSourceTrail& Trail, const FBaseParticle& SourceParticle,
	const UParticleModuleTypeDataRibbon& RibbonModule, bool bForceSample)
{
	const float SourceSize = std::max(0.0f, SourceParticle.Size.X);
	const float Width = std::max(0.0f, RibbonModule.Width * RibbonModule.SourceWidthScale * SourceSize);
	const FLinearColor Color = SourceParticle.Color;

	if (!bForceSample && !Trail.Samples.empty())
	{
		return;
	}

	if (Trail.Samples.empty() && DistanceSquared(SourceParticle.OldLocation, SourceParticle.Location) > 1e-4f)
	{
		FRibbonSourceSample OldSample;
		OldSample.Position = SourceParticle.OldLocation;
		OldSample.Color = Color;
		OldSample.Width = Width;
		OldSample.Age = 0.0f;
		Trail.Samples.push_back(OldSample);
	}

	if (!Trail.Samples.empty() && DistanceSquared(Trail.Samples.back().Position, SourceParticle.Location) <= 1e-4f)
	{
		Trail.LastSamplePosition = SourceParticle.Location;
		Trail.TimeSinceLastSample = 0.0f;
		return;
	}

	FRibbonSourceSample Sample;
	Sample.Position = SourceParticle.Location;
	Sample.Color = Color;
	Sample.Width = Width;
	Sample.Age = 0.0f;
	Trail.Samples.push_back(Sample);

	Trail.LastSamplePosition = SourceParticle.Location;
	Trail.TimeSinceLastSample = 0.0f;
}

void FRibbonEmitterInstance::PruneSourceTrails(const UParticleModuleTypeDataRibbon& RibbonModule)
{
	const float Lifetime = std::max(0.001f, RibbonModule.SourceTrailLifetime);
	const int32 MaxPointCount = std::max(2, RibbonModule.MaxParticleInTrailCount);

	for (auto It = SourceTrails.begin(); It != SourceTrails.end();)
	{
		FRibbonSourceTrail& Trail = It->second;
		Trail.Samples.erase(
			std::remove_if(Trail.Samples.begin(), Trail.Samples.end(),
				[Lifetime](const FRibbonSourceSample& Sample)
				{
					return Sample.Age >= Lifetime;
				}),
			Trail.Samples.end());

		if (static_cast<int32>(Trail.Samples.size()) > MaxPointCount)
		{
			Trail.Samples.erase(Trail.Samples.begin(), Trail.Samples.end() - MaxPointCount);
		}

		if (Trail.Samples.empty())
		{
			It = SourceTrails.erase(It);
		}
		else
		{
			++It;
		}
	}
}
