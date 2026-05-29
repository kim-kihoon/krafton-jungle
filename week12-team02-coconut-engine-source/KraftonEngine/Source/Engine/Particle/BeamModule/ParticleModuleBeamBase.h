#pragma once
#include "Particle/ParticleBeamInstances.h"
#include "Particle/ParticleModule.h"
#include "ParticleModuleBeamBase.generated.h"

UENUM()
enum EBeam2SourceTargetMethod : int
{
	PEB2STM_Default,
	PEB2STM_UserSet,
	PEB2STM_Emitter,
	PEB2STM_Particle,
	PEB2STM_Actor,
};

struct FBeamResolveContext
{
	FParticleBeam2EmitterInstance& Owner;
	UParticleModuleTypeDataBeam2& TypeData;
	const FMatrix& ComponentToWorld;
	const FBaseParticle* Particle = nullptr;
	int32 BeamIndex = 0;
	float BeamProgress = 1.0f;
};

UCLASS()
class UParticleModuleBeamBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleBeamBase)

	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }
	EModuleType GetModuleType() const override { return EPMT_Beam; }
};
