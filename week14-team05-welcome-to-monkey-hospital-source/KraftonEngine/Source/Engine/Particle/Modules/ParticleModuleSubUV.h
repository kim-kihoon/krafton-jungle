#pragma once

#include "Particle/ParticleModule.h"
#include "Engine/Particle/Distributions/DistributionFloat.h"

#include "Source/Engine/Particle/Modules/ParticleModuleSubUV.generated.h"

// =============================================================================
// UParticleModuleSubUV
//   Cascade의 SubImage Index 역할.
//   SubImageIndexDistribution을 Particle->RelativeTime(0..1) 기준으로 평가해서
//   현재 보여줄 SubUV atlas frame index를 직접 지정한다.
//
//   - 0번 frame은 유효한 첫 번째 frame이다.
//   - 미설정/fallback은 FBaseParticle::SubImageIndex == -1 로 구분한다.
//   - flipbook 자동 재생은 UParticleModuleSubUVMovie가 담당한다.
// =============================================================================
UCLASS()
class UParticleModuleSubUV : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleSubUV();

	EModuleCategory GetCategory() const override { return EModuleCategory::SubUV; }
	const char* GetDisplayName() const override { return "Sub Image Index"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;
	void UpdateParticleSubset(FParticleEmitterInstance* Owner, UParticleLODLevel* SimulationLOD,
	                          uint32 ModuleOffset, float DeltaTime,
	                          const TArray<uint32>& ParticleIndices) override;

	// Evaluated with Particle->RelativeTime: normalized particle lifetime, 0=birth, 1=death.
	// The evaluated value is floored and clamped to [0, FrameCount - 1].
	UPROPERTY(Edit, Save, Instanced, Category="SubUV", DisplayName="Sub Image Index", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* SubImageIndexDistribution = nullptr;
};
