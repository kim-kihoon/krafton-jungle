#pragma once

#include "Particle/ParticleModule.h"
#include "Engine/Particle/Distributions/DistributionFloat.h"

#include "Source/Engine/Particle/Modules/ParticleModuleSpawn.generated.h"

// =============================================================================
// UParticleModuleSpawn
//   초당 spawn rate 와 1회 burst 를 정의한다.
//   Rate/RateScale Distribution은 emitter loop time 기준으로 평가한다.
//   Burst Entry.Time도 emitter loop 기준 seconds이다.
// =============================================================================
USTRUCT()
struct FBurstEntry
{
	GENERATED_BODY()

	// Emitter loop 기준 burst 발생 시간(seconds). Particle->RelativeTime이 아니다.
	UPROPERTY(Edit, Save, Category="Spawn", DisplayName="Time")
	float Time = 0.0f;

	UPROPERTY(Edit, Save, Category="Spawn", DisplayName="Count")
	int32 Count = 0;
};

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleSpawn();

	EModuleCategory GetCategory() const override { return EModuleCategory::Spawn; }
	const char*     GetDisplayName() const override { return "Spawn"; }
	bool            IsUnique() const override { return true; }

	struct FSpawnModuleInstancePayload
	{
		float LastProcessedBurstTime = 0.0f;
	};

	virtual void GetRateSpawnAmount(FParticleEmitterInstance* Owner, float DeltaTime, float EmitterTime,
	                                float& OutSpawnAmount) const;
	uint32 RequiredBytesPerInstance() const override { return sizeof(FSpawnModuleInstancePayload); }

	// Evaluated with emitter loop time sampled at the middle of the current tick.
	UPROPERTY(Edit, Save, Instanced, Category="Spawn", DisplayName="Rate Distribution", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* RateDistribution = nullptr;

	// Evaluated with emitter loop time sampled at the middle of the current tick.
	UPROPERTY(Edit, Save, Instanced, Category="Spawn", DisplayName="Rate Scale Distribution", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* RateScaleDistribution = nullptr;

	UPROPERTY(Edit, Save, Category="Spawn", DisplayName="Bursts", Type=Array, Struct=FBurstEntry)
	TArray<FBurstEntry> BurstList;
};
