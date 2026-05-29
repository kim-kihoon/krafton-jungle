#pragma once
#include "ParticleModuleBeamBase.h"
#include "ParticleModuleBeamTarget.generated.h"

UCLASS()
class UParticleModuleBeamTarget : public UParticleModuleBeamBase
{
public:
	GENERATED_BODY(UParticleModuleBeamTarget)

	UPROPERTY(Edit, Category = "Target")
	EBeam2SourceTargetMethod TargetMethod = PEB2STM_Default;

	UPROPERTY(Edit, Category = "Target")
	FName TargetName;

	UPROPERTY(Edit, Category = "Target")
	bool bTargetAbsolute = false;

	UPROPERTY(Edit, Category = "Target")
	bool bLockTarget = false;

	UPROPERTY(Edit, Category = "Target")
	FVector Target = FVector(100.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Category = "Target")
	bool bLockTargetTangent = false;

	UPROPERTY(Edit, Category = "Target")
	FVector TargetTangent = FVector::ZeroVector;

	UPROPERTY(Edit, Category = "Target", Min = 0.0f)
	float TargetStrength = 1.0f;

	void Update(const FUpdateContext& UpdateContext) override;
	FVector ResolveTarget(const FBeamResolveContext& Context, const FVector& ResolvedSource) const;
	FVector ResolveTargetTangent(const FVector& ResolvedSource, const FVector& ResolvedTarget) const;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
	void Spawn(const FSpawnContext& Context) override;
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; } // Piggybacks on TypeData Module
};
