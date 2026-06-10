#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleBeamSource.generated.h"

// =============================================================================
// UParticleModuleBeamSource
//   Cascade Beam Source module. Beam 시작점을 TypeData에서 분리해 저장/평가한다.
//   SourceDistribution은 EmitterTime 기준으로 평가된다.
// =============================================================================
UCLASS()
class UParticleModuleBeamSource : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleBeamSource();

	UENUM()
	enum class EBeam2SourceMethod : uint8
	{
		Default = 0,
		UserSet,
		Emitter,
	};

	EModuleCategory GetCategory() const override { return EModuleCategory::Beam; }
	const char*     GetDisplayName() const override { return "Beam Source"; }
	bool            IsUnique() const override { return true; }

	FVector ResolveSource(const FParticleEmitterInstance* Owner, float EmitterTime, const FVector& DefaultSource) const;
	FVector ResolveSourceTangent(const FParticleEmitterInstance* Owner, float EmitterTime) const;

	// Default: TypeData/default endpoint를 사용한다. UserSet: SourceDistribution을 사용한다.
	// Emitter: emitter/component origin을 source로 사용한다.
	UPROPERTY(Edit, Save, Category="Beam Source", DisplayName="Source Method", Enum=EBeam2SourceMethod)
	EBeam2SourceMethod SourceMethod = EBeam2SourceMethod::Default;

	// Evaluated with EmitterTime. UserSet일 때 source 위치로 사용된다.
	UPROPERTY(Edit, Save, Instanced, Category="Beam Source", DisplayName="Source", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* SourceDistribution = nullptr;

	UPROPERTY(Edit, Save, Instanced, Category="Beam Source", DisplayName="Source Tangent", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* SourceTangentDistribution = nullptr;

	// true면 SourceDistribution을 world position으로 보고 simulation space로 변환한다.
	// false면 local position으로 보고 Required.bUseLocalSpace 정책에 맞춰 변환한다.
	UPROPERTY(Edit, Save, Category="Beam Source", DisplayName="Source Absolute")
	bool bSourceAbsolute = false;

	// true면 최초 평가한 source를 유지한다. 런타임 lock state는 EmitterInstance가 보유한다.
	UPROPERTY(Edit, Save, Category="Beam Source", DisplayName="Lock Source")
	bool bLockSource = false;
};
