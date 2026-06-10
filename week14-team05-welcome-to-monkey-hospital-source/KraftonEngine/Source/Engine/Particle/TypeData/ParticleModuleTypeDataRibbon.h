#pragma once

#include "Particle/TypeData/ParticleModuleTypeDataBase.h"

#include "Source/Engine/Particle/TypeData/ParticleModuleTypeDataRibbon.generated.h"

UENUM()
enum EParticleSourceSelectionMethod : int
{
	EPSSM_Random,
	EPSSM_Sequential,
	EPSSM_MAX,
};

UENUM()
enum ETrail2SourceMethod : int
{
	PET2SRCM_Default,
	PET2SRCM_Particle,
	PET2SRCM_Actor,
	PET2SRCM_MAX,
};

UENUM()
enum ETrailsRenderAxisOption : int
{
	Trails_CameraUp,
	Trails_SourceUp,
	Trails_WorldUp,
	Trails_MAX,
};

// =============================================================================
// UParticleModuleTypeDataRibbon
//   Ribbon Emitter — 시간 순으로 sample 된 입자들을 이어 quad-strip 으로 렌더.
//   Beam 과 비슷하지만 endpoint 가 고정이 아닌, "emitter 가 이동한 자취" 가 ribbon.
// =============================================================================
UCLASS()
class UParticleModuleTypeDataRibbon : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY()
	UParticleModuleTypeDataRibbon() = default;

	const char* GetDisplayName() const override { return "TypeData (Ribbon)"; }

	FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* InComponent) override;

	UPROPERTY(Edit, Save, Category="Ribbon", DisplayName="Max Tessellation", Min=1.0f, Max=64.0f)
	int32 MaxTessellation = 8;

	UPROPERTY(Edit, Save, Category="Ribbon", DisplayName="Tangent Tension", Min=0.0f, Max=1.0f)
	float TangentTension = 0.5f;

	UPROPERTY(Edit, Save, Category="Ribbon", DisplayName="Tiles Per Trail")
	float TilesPerTrail = 1.0f;
};
