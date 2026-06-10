#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleBeamTarget.generated.h"

// =============================================================================
// UParticleModuleBeamTarget
//   Cascade Beam Target module. Beam 끝점을 TypeData에서 분리해 저장/평가한다.
//   TargetDistribution은 EmitterTime 기준으로 평가된다.
// =============================================================================
UCLASS()
class UParticleModuleBeamTarget : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleBeamTarget();

	UENUM()
	enum class EBeam2TargetMethod : uint8
	{
		Default = 0,
		UserSet,
		Emitter,
		Distance,
	};

	EModuleCategory GetCategory() const override { return EModuleCategory::Beam; }
	const char*     GetDisplayName() const override { return "Beam Target"; }
	bool            IsUnique() const override { return true; }

	FVector ResolveTarget(const FParticleEmitterInstance* Owner, float EmitterTime,
	                      const FVector& Source, const FVector& DefaultTarget,
	                      float Distance) const;
	FVector ResolveTargetTangent(const FParticleEmitterInstance* Owner, float EmitterTime) const;

	// Default: TypeData/기본 target을 사용한다. UserSet: TargetDistribution을 사용한다.
	// Distance: source + local X * Distance를 target으로 사용한다.
	UPROPERTY(Edit, Save, Category="Beam Target", DisplayName="Target Method", Enum=EBeam2TargetMethod)
	EBeam2TargetMethod TargetMethod = EBeam2TargetMethod::Default;

	// Evaluated with EmitterTime. UserSet일 때 target 위치로 사용된다.
	UPROPERTY(Edit, Save, Instanced, Category="Beam Target", DisplayName="Target", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* TargetDistribution = nullptr;

	UPROPERTY(Edit, Save, Instanced, Category="Beam Target", DisplayName="Target Tangent", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* TargetTangentDistribution = nullptr;

	// true면 TargetDistribution을 world position으로 보고 simulation space로 변환한다.
	// false면 local position으로 보고 Required.bUseLocalSpace 정책에 맞춰 변환한다.
	UPROPERTY(Edit, Save, Category="Beam Target", DisplayName="Target Absolute")
	bool bTargetAbsolute = false;

	// true면 최초 평가한 target을 유지한다. 런타임 lock state는 EmitterInstance가 보유한다.
	UPROPERTY(Edit, Save, Category="Beam Target", DisplayName="Lock Target")
	bool bLockTarget = false;
};
