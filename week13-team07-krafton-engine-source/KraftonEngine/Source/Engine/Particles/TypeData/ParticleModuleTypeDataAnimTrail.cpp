#include "Particles/TypeData/ParticleModuleTypeDataAnimTrail.h"

#include "Particles/ParticleEmitterInstances.h"
#include "Serialization/Archive.h"

UParticleModuleTypeDataAnimTrail::UParticleModuleTypeDataAnimTrail()
{
	// AnimTrail은 기존 Ribbon 렌더러를 재사용하되, vertex의 Up/Size를 base-tip 방향으로 강제한다.
	RenderAxis = Trails_SourceUp;
	MaxTrailCount = 1;
	MaxParticleInTrailCount = 64;
	MaxTessellationBetweenParticles = 0;
	DistanceTessellationStepSize = 0.0f;
	TilingDistance = 128.0f;
	bSpawnInitialParticle = true;
	bTangentRecalculationEveryFrame = false;
	bEnablePreviousTangentRecalculation = true;
	bRenderGeometry = true;
}

uint32 UParticleModuleTypeDataAnimTrail::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	return sizeof(FRibbonTypeDataPayload);
}

FParticleEmitterInstance* UParticleModuleTypeDataAnimTrail::CreateInstance(UParticleEmitter* InEmitterParent, UParticleSystemComponent& InComponent)
{
	return new FParticleAnimTrailEmitterInstance();
}

void UParticleModuleTypeDataAnimTrail::Serialize(FArchive& Ar)
{
	UParticleModuleTypeDataRibbon::Serialize(Ar);
	Ar << FirstSocketName << SecondSocketName;
	Ar << TrailLifeTime << SampleInterval << MinSampleDistance << WidthScale;
}
