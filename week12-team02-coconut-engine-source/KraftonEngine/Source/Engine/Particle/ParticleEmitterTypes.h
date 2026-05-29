#pragma once

#include "Core/EngineTypes.h"
#include "ParticleEmitterTypes.generated.h"

UENUM()
enum EParticleBurstMethod : int
{
	EPBM_Instant,
	EPBM_Interpolated,
	EPBM_MAX
};

UENUM()
enum EParticleScreenAlignment : int
{
	PSA_Square,
	PSA_Rectangle,
	PSA_Velocity,
	PSA_AwayFromCenter,
	PSA_TypeSpecific,
	PSA_FacingCameraPosition,
	/* PSA_AlongCustomAxis is deprecated */
	PSA_MAX
};

USTRUCT()
struct FParticleBurst
{
	GENERATED_BODY(FParticleBurst)

	UPROPERTY(Edit, Category="ParticleBurst", DisplayName="Count")
	int32 Count = 0;

	UPROPERTY(Edit, Category="ParticleBurst", DisplayName="Count Low")
	int32 CountLow = -1;

	UPROPERTY(Edit, Category="ParticleBurst", DisplayName="Time", Min=0.0, Max=1.0, Speed=0.01)
	float Time = 0.0f;
};
