#include "Particle/TypeData/ParticleModuleTypeDataBeam2.h"

#include "Particle/ParticleBeamInstances.h"
#include "Particle/ParticleHelper.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/BeamModule/ParticleModuleBeamNoise.h"
#include "Particle/BeamModule/ParticleModuleBeamSource.h"
#include "Particle/BeamModule/ParticleModuleBeamTarget.h"

UParticleModule* UParticleModuleTypeDataBeam2::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleTypeDataBeam2* Copy = GUObjectArray.CreateObject<UParticleModuleTypeDataBeam2>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->BeamMethod = BeamMethod;
	Copy->InterpolationPoints = InterpolationPoints;
	Copy->Sheets = Sheets;
	Copy->MaxBeamCount = MaxBeamCount;
	Copy->Speed = Speed;
	Copy->bAlwaysOn = bAlwaysOn;
	Copy->UpVectorStepSize = UpVectorStepSize;
	Copy->Distance = Distance;
	Copy->SourcePoint = SourcePoint;
	Copy->TargetPoint = TargetPoint;
	Copy->Width = Width;
	Copy->TextureTile = TextureTile;
	Copy->TextureTileDistance = TextureTileDistance;
	Copy->Color = Color;
	Copy->Alpha = Alpha;
	Copy->BranchParentName = BranchParentName;
	Copy->TaperMethod = TaperMethod;
	Copy->TaperFactor = TaperFactor;
	Copy->TaperScale = TaperScale;
	Copy->bRenderGeometry = bRenderGeometry;
	Copy->bRenderDirectLine = bRenderDirectLine;
	Copy->bRenderLines = bRenderLines;
	Copy->bRenderTessellation = bRenderTessellation;
	Copy->TargetData = TargetData;
	return Copy;
}

void UParticleModuleTypeDataBeam2::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	FBeam2TypeDataPayload* Payload = reinterpret_cast<FBeam2TypeDataPayload*>(
		reinterpret_cast<uint8*>(Context.ParticleBase) + Context.Offset);

	Payload->SourcePoint = SourcePoint;
	Payload->TargetPoint = (BeamMethod == PEB2M_Distance)
		? SourcePoint + FVector(Distance, 0.0f, 0.0f)
		: TargetPoint;

	const FVector BeamDelta = Payload->TargetPoint - Payload->SourcePoint;
	Payload->SourceTangent      = BeamDelta;
	Payload->TargetTangent      = BeamDelta;
	Payload->SourceStrength     = 1.0f;
	Payload->TargetStrength     = 1.0f;
	Payload->LockSource         = 0;
	Payload->LockTarget         = 0;
	Payload->LockSourceTangent  = 0;
	Payload->LockTargetTangent  = 0;

	// Owner is guaranteed to be a beam instance whenever this TypeData is the
	// LOD's TypeDataModule (constructed together by the emitter pipeline).
	FParticleBeam2EmitterInstance& BeamInstance =
		static_cast<FParticleBeam2EmitterInstance&>(Context.Owner);

	if (BeamInstance.BeamSourceModule)
	{
		BeamInstance.BeamSourceModule->Spawn(Context);
	}
	if (BeamInstance.BeamTargetModule)
	{
		BeamInstance.BeamTargetModule->Spawn(Context);
	}
	if (BeamInstance.BeamNoiseModule)
	{
		BeamInstance.BeamNoiseModule->Spawn(Context);
	}
}
