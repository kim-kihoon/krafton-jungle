#pragma once

#include "Particle/ParticleModule.h"
#include "Engine/Particle/Distributions/DistributionFloat.h"

#include "Source/Engine/Particle/Modules/ParticleModuleSubUVMovie.generated.h"

// =============================================================================
// UParticleModuleSubUVMovie
//   Cascade의 SubUV Movie 역할.
//   StartFrame~EndFrame 구간을 flipbook처럼 순차 재생한다.
//
//   FrameRateDistribution은 Particle->RelativeTime(0..1) 기준으로 평가되며,
//   평가된 값은 "초당 SubUV frame 수(FPS)"로 사용한다.
//   FrameRate가 0 이하이면 Particle->RelativeTime 기준으로 구간 전체를 1회 재생한다.
// =============================================================================
UCLASS()
class UParticleModuleSubUVMovie : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleSubUVMovie();

	EModuleCategory GetCategory() const override { return EModuleCategory::SubUV; }
	const char* GetDisplayName() const override { return "SubUV Movie"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;
	void UpdateParticleSubset(FParticleEmitterInstance* Owner, UParticleLODLevel* SimulationLOD,
	                          uint32 ModuleOffset, float DeltaTime,
	                          const TArray<uint32>& ParticleIndices) override;
	uint32 RequiredBytes(UParticleLODLevel* LODLevel) const override;

	struct FSubUVMovieParticlePayload
	{
		int32 RandomFrameOffset = 0;
	};

	UPROPERTY(Edit, Save, Category="SubUV Movie", DisplayName="Start Frame", Min=0.0f)
	int32 StartFrame = 0;

	UPROPERTY(Edit, Save, Category="SubUV Movie", DisplayName="End Frame")
	int32 EndFrame = -1;

	// Evaluated with Particle->RelativeTime. Value means FPS, not frame interval seconds.
	UPROPERTY(Edit, Save, Instanced, Category="SubUV Movie", DisplayName="Frame Rate", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* FrameRateDistribution = nullptr;

	UPROPERTY(Edit, Save, Category="SubUV Movie", DisplayName="Is Looped")
	bool bLooped = true;

	UPROPERTY(Edit, Save, Category="SubUV Movie", DisplayName="Random Start Frame")
	bool bRandomStartFrame = false;
};
