#pragma once

#include "Particle/ParticleModule.h"
#include "Engine/Particle/Distributions/DistributionFloat.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleBeamNoise.generated.h"

// =============================================================================
// UParticleModuleBeamNoise
//   Cascade Beam Noise module. Beam 경로 변위/빈도/흐름 속도를 TypeData에서 분리한다.
//   모든 Distribution은 EmitterTime 기준으로 평가된다.
// =============================================================================
UCLASS()
class UParticleModuleBeamNoise : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleBeamNoise();

	EModuleCategory GetCategory() const override { return EModuleCategory::Beam; }
	const char*     GetDisplayName() const override { return "Beam Noise"; }
	bool            IsUnique() const override { return true; }

	float EvaluateNoiseRange(float EmitterTime, UObject* Data = nullptr) const;
	float EvaluateNoiseFrequency(float EmitterTime, UObject* Data = nullptr) const;
	float EvaluateNoiseSpeed(float EmitterTime, UObject* Data = nullptr) const;
	FVector ResolveNoiseDirection(const FParticleEmitterInstance* Owner, float EmitterTime) const;

	// Beam 중심선에서 벗어나는 최대 거리. 0이면 직선 Beam이다.
	UPROPERTY(Edit, Save, Instanced, Category="Beam Noise", DisplayName="Noise Range", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* NoiseRangeDistribution = nullptr;

	UPROPERTY(Edit, Save, Instanced, Category="Beam Noise", DisplayName="Noise Direction", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* NoiseDirectionDistribution = nullptr;

	// Beam 길이 기준 wave/noise 반복 수.
	UPROPERTY(Edit, Save, Instanced, Category="Beam Noise", DisplayName="Frequency", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* FrequencyDistribution = nullptr;

	// 시간에 따른 noise phase 이동 속도. 0이면 정적인 모양이다.
	UPROPERTY(Edit, Save, Instanced, Category="Beam Noise", DisplayName="Noise Speed", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* NoiseSpeedDistribution = nullptr;

	// 현재 renderer는 smooth sine 기반으로 처리한다. false면 향후 jagged/noise table용 플래그로 남긴다.
	UPROPERTY(Edit, Save, Category="Beam Noise", DisplayName="Smooth")
	bool bSmooth = true;

	// InterpolationPoints보다 큰 경우 Beam strip 분할 수를 늘려 noise가 더 촘촘하게 보이게 한다.
	UPROPERTY(Edit, Save, Category="Beam Noise", DisplayName="Noise Tessellation", Min=0.0f, Max=128.0f)
	int32 NoiseTessellation = 0;
};
