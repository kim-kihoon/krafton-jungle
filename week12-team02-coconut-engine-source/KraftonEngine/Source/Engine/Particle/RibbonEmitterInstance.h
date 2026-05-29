#pragma once
#include "ParticleEmitterInstances.h"

class UParticleModuleTypeDataRibbon;

struct FRibbonSourceSample
{
	FVector Position = FVector::ZeroVector;
	FLinearColor Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	float Width = 1.0f;
	float Age = 0.0f;
};

struct FRibbonSourceTrail
{
	uint32 SourceParticleId = 0;
	int32 TrailIndex = 0;
	float TimeSinceLastSample = 0.0f;
	FVector LastSamplePosition = FVector::ZeroVector;
	TArray<FRibbonSourceSample> Samples;
};

struct FRibbonEmitterInstance : public FParticleEmitterInstance
{
	explicit FRibbonEmitterInstance(UParticleSystemComponent* InComponent);

	void Tick(float DeltaTime, int32 LODLevel, bool bSuppressSpawning) override;
	void PostSpawn(FBaseParticle* Particle, float Interp, float SpawnTime) override;
	FDynamicEmitterReplayDataBase* GetReplayData() override;

private:
	bool IsSourceRibbonEnabled() const;
	UParticleModuleTypeDataRibbon* GetRibbonModule() const;
	FParticleEmitterInstance* FindSourceEmitter(const UParticleModuleTypeDataRibbon& RibbonModule) const;
	void UpdateSourceTrails(float DeltaTime, const UParticleModuleTypeDataRibbon& RibbonModule);
	FDynamicEmitterReplayDataBase* GetSourceReplayData(const UParticleModuleTypeDataRibbon& RibbonModule);
	void AppendSourceSample(FRibbonSourceTrail& Trail, const FBaseParticle& SourceParticle,
		const UParticleModuleTypeDataRibbon& RibbonModule, bool bForceSample);
	void PruneSourceTrails(const UParticleModuleTypeDataRibbon& RibbonModule);

	uint32 RibbonSpawnSequence = 0;
	int32 NextSourceTrailIndex = 0;
	TMap<uint32, FRibbonSourceTrail> SourceTrails;
};
