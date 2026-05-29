#pragma once
#include "ParticleModuleBeamBase.h"
#include "ParticleModuleBeamSource.generated.h"

UCLASS()
class UParticleModuleBeamSource : public UParticleModuleBeamBase
{
public:
	GENERATED_BODY(UParticleModuleBeamSource)

	UPROPERTY(Edit, Category = "Source")
	EBeam2SourceTargetMethod SourceMethod = PEB2STM_Default;

	UPROPERTY(Edit, Category = "Source")
	FName SourceName;

	UPROPERTY(Edit, Category = "Source")
	bool bSourceAbsolute = false;

	UPROPERTY(Edit, Category = "Source")
	bool bLockSource = false;

	UPROPERTY(Edit, Category = "Source")
	FVector Source = FVector::ZeroVector;

	UPROPERTY(Edit, Category = "Source")
	bool bLockSourceTangent = false;

	UPROPERTY(Edit, Category = "Source")
	FVector SourceTangent = FVector::ZeroVector;

	UPROPERTY(Edit, Category = "Source", Min = 0.0f)
	float SourceStrength = 1.0f;

	void Update(const FUpdateContext& UpdateContext) override;
	FVector ResolveSource(const FBeamResolveContext& Context) const;
	FVector ResolveSourceTangent(const FVector& ResolvedSource, const FVector& ResolvedTarget) const;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
	void Spawn(const FSpawnContext& Context) override;
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; } // Piggybacks on TypeData Module
};
