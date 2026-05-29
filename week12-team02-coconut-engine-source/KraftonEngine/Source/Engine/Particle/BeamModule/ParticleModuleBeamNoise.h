#pragma once
#include "ParticleModuleBeamBase.h"
#include "ParticleModuleBeamNoise.generated.h"

struct FBeam2TypeDataPayload;

struct FBeamNoisePayloadData {
	FVector* NoisePoints;
	float* NoiseTimes;
	int32 NoiseIndex;
	int32 NoiseCount;
	float NextNoiseTime;
};

UCLASS()
class UParticleModuleBeamNoise : public UParticleModuleBeamBase
{
public:
	GENERATED_BODY(UParticleModuleBeamNoise)

	UPROPERTY(Edit, Category = "LowFreq", Min = 0, Max = 64)
	int32 Frequency = 0;

	UPROPERTY(Edit, Category = "LowFreq")
	float FrequencyDistance = 0.0f;

	UPROPERTY(Edit, Category = "LowFreq")
	FVector NoiseRange = FVector::ZeroVector;

	UPROPERTY(Edit, Category = "LowFreq", Min = 0.0f)
	float NoiseSpeed = 0.0f;

	UPROPERTY(Edit, Category = "LowFreq", Min = 0.0f)
	float NoiseLockTime = 0.0f;

	UPROPERTY(Edit, Category = "LowFreq")
	bool bTargetNoise = false;

	void BuildNoiseOffsets(FVector* OutOffsets, float* OutTimes,
		int32 NumPoints, float CurrentTime) const;
	void MoveNoiseOffsets(FVector* CurrentOffsets, const FVector* TargetOffsets,
		int32 NumPoints, float DeltaTime) const;
	// Place NumPoints along the beam's Hermite curve (payload tangents/strengths)
	// and add the per-point random offset. Caller owns OutPoints.
	void ApplyNoiseOffsets(FVector* OutPoints, const FVector* Offsets,
		int32 NumPoints, const FBeam2TypeDataPayload& BeamPayload) const;

	void Update(const FUpdateContext& UpdateContext) override;
	void Spawn(const FSpawnContext& Context) override;
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return sizeof(FBeamNoisePayloadData); }
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};
