#pragma once
#include "ParticleEmitterInstances.h"

class UParticleModuleBeamSource;
class UParticleModuleBeamTarget;
class UParticleModuleBeamNoise;
struct FBeam2TypeDataPayload;

struct FParticleBeam2EmitterInstance : public FParticleEmitterInstance
{
	using FParticleEmitterInstance::FParticleEmitterInstance;

	void InitParameters(UParticleEmitter* InTemplate) override;
	void Tick(float DeltaTime, int32 LODLevel, bool bSuppressSpawning) override;
	FDynamicEmitterReplayDataBase* GetReplayData() override;

	void ResetBeamTravelTime() { BeamTravelTime = 0.f; }

	// Look up a sibling emitter instance on the same component by EmitterName.
	// Returns nullptr if Name is invalid, Component is null, or no match. Used
	// by Source/Target modules to resolve PEB2STM_Emitter and (eventually)
	// PEB2STM_Particle endpoints.
	FParticleEmitterInstance* FindSiblingEmitter(const FName& Name) const;

	UParticleModuleTypeDataBeam2* BeamModule	= nullptr;
	UParticleModuleBeamSource* BeamSourceModule = nullptr;
	UParticleModuleBeamTarget* BeamTargetModule = nullptr;
	UParticleModuleBeamNoise* BeamNoiseModule   = nullptr;

	// Per-emitter arenas backing the FBeamNoisePayloadData views in each
	// particle's payload. Sized MaxActiveParticles * Frequency, lazily on first
	// Spawn. A particle's slice lives at [Slot * Frequency], where Slot is
	// derived from (ParticleBase - ParticleData) / ParticleStride. Pointers in
	// payloads are non-owning and re-derived in BeamNoise Update after any
	// Resize, so the arenas can move freely.
	TArray<FVector> NoisePointArena;
	TArray<FVector> NoiseOffsetArena;
	TArray<FVector> NoiseTargetOffsetArena;
	TArray<float> NoiseTimeArena;

	float BeamTravelTime = 0.f;

private:
	const FBeam2TypeDataPayload* GetBeamPayload(const FBaseParticle* Particle) const;
	FBeam2TypeDataPayload* GetBeamPayload(FBaseParticle* Particle) const;
};
