#pragma once

#include "Particle/ParticleModule.h"
#include "Math/Vector.h"
#include "Engine/Particle/Distributions/DistributionFloat.h"
#include "Engine/Particle/Distributions/DistributionVector.h"

#include "Source/Engine/Particle/Modules/ParticleModuleColor.generated.h"

// =============================================================================
// UParticleModuleColor
//   Initial Color 전용 모듈. Spawn 시 한 번 적용된다.
//   생존 시간에 따른 color/alpha 변화는 UParticleModuleColorOverLife가 담당한다.
// =============================================================================
UCLASS()
class UParticleModuleColor : public UParticleModule
{
public:
	GENERATED_BODY()
	UParticleModuleColor() = default;

	EModuleCategory GetCategory() const override { return EModuleCategory::Color; }
	const char*     GetDisplayName() const override { return "Initial Color"; }

	void Spawn(FParticleEmitterInstance* Owner, uint32 ModuleOffset,
	           float SpawnTime, FBaseParticle* Particle) override;

	// Legacy constant fallback for assets saved before Initial Color used distributions.
	UPROPERTY(Edit, Save, Category="Color", DisplayName="Initial Color")
	FVector4 StartColor = { 1, 1, 1, 1 };

	UPROPERTY(Edit, Save, Instanced, Category="Color", DisplayName="Initial Color", Type=ObjectRef, AllowedClass=UDistributionVector)
	UDistributionVector* StartColorDistribution = nullptr;

	UPROPERTY(Edit, Save, Instanced, Category="Color", DisplayName="Initial Alpha", Type=ObjectRef, AllowedClass=UDistributionFloat)
	UDistributionFloat* StartAlphaDistribution = nullptr;
};
