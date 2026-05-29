#include "Particle/BeamModule/ParticleModuleBeamSource.h"

#include "Component/ParticleSystemComponent.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleHelper.h"
#include "Particle/BeamModule/ParticleModuleBeamTarget.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam2.h"

#include <algorithm>

UParticleModule* UParticleModuleBeamSource::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleBeamSource* Copy = GUObjectArray.CreateObject<UParticleModuleBeamSource>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->SourceMethod    = SourceMethod;
	Copy->SourceName      = SourceName;
	Copy->bSourceAbsolute = bSourceAbsolute;
	Copy->bLockSource     = bLockSource;
	Copy->Source          = Source;
	Copy->bLockSourceTangent = bLockSourceTangent;
	Copy->SourceTangent      = SourceTangent;
	Copy->SourceStrength     = SourceStrength;
	return Copy;
}

FVector UParticleModuleBeamSource::ResolveSource(const FBeamResolveContext& Context) const
{
	const FMatrix WorldToComponent = Context.ComponentToWorld.GetInverse();
	auto ToPayloadSpace = [&](const FVector& Position, bool bAbsolute)
	{
		return bAbsolute ? WorldToComponent.TransformPositionWithW(Position) : Position;
	};

	switch (SourceMethod)
	{
	case PEB2STM_Default:
		return Context.TypeData.SourcePoint;
	case PEB2STM_UserSet:
		return ToPayloadSpace(Source, bSourceAbsolute);
	case PEB2STM_Emitter:
		// Sibling emitter on the same component; fall back to TypeData default
		// when SourceName is unset or no sibling matches.
		if (FParticleEmitterInstance* Sibling = Context.Owner.FindSiblingEmitter(SourceName))
		{
			return WorldToComponent.TransformPositionWithW(Sibling->Location);
		}
		return Context.TypeData.SourcePoint;
	case PEB2STM_Particle:
		// TODO: pick a particle from the sibling emitter named by SourceName and
		// use its world location. Needs both the sibling lookup (have it) and a
		// per-frame particle pick policy (round-robin / oldest / index). Until
		// that exists, fall back to the TypeData default so behavior is
		// predictable rather than silently using the current beam's particle.
		return Context.TypeData.SourcePoint;
	case PEB2STM_Actor:
		// TODO: resolve a scene actor (typically by SourceName) and use its
		// world location. Needs an actor registry hook that doesn't exist on
		// this engine yet. Default fallback for now.
		return Context.TypeData.SourcePoint;
	default:
		return Context.TypeData.SourcePoint;
	}
}

FVector UParticleModuleBeamSource::ResolveSourceTangent(const FVector& ResolvedSource, const FVector& ResolvedTarget) const
{
	return SourceTangent.IsNearlyZero()
		? ResolvedTarget - ResolvedSource
		: SourceTangent;
}

void UParticleModuleBeamSource::Update(const FUpdateContext& UpdateContext)
{
	FParticleBeam2EmitterInstance& BeamOwner = static_cast<FParticleBeam2EmitterInstance&>(UpdateContext.Owner);
	if (!BeamOwner.BeamModule || !BeamOwner.Component || UpdateContext.Offset < 0)
	{
		return;
	}

	const int32 PayloadEnd = UpdateContext.Offset + static_cast<int32>(sizeof(FBeam2TypeDataPayload));
	if (PayloadEnd > BeamOwner.ParticleSize)
	{
		return;
	}

	const FMatrix& ComponentToWorld = BeamOwner.Component->GetWorldMatrix();
	for (int32 Index = 0; Index < BeamOwner.ActiveParticles; ++Index)
	{
		FBaseParticle* Particle = BeamOwner.ParticleIndices
			? BeamOwner.GetParticleDirect(BeamOwner.ParticleIndices[Index])
			: nullptr;
		if (!Particle)
		{
			continue;
		}

		FBeam2TypeDataPayload* Payload = reinterpret_cast<FBeam2TypeDataPayload*>(
			reinterpret_cast<uint8*>(Particle) + UpdateContext.Offset);

		Payload->LockSource = bLockSource ? 1 : 0;
		if (Payload->LockSource == 0)
		{
			FBeamResolveContext ResolveContext{ BeamOwner, *BeamOwner.BeamModule, ComponentToWorld, Particle, Index, 1.0f };
			Payload->SourcePoint = ResolveSource(ResolveContext);
		}

		Payload->LockSourceTangent = bLockSourceTangent ? 1 : 0;
		if (Payload->LockSourceTangent == 0)
		{
			Payload->SourceTangent = ResolveSourceTangent(Payload->SourcePoint, Payload->TargetPoint);
			Payload->SourceStrength = std::max(0.0f, SourceStrength);
		}

		if (!BeamOwner.BeamTargetModule || !BeamOwner.BeamTargetModule->bEnabled)
		{
			const FVector BeamDelta = Payload->TargetPoint - Payload->SourcePoint;
			Payload->TargetTangent = BeamDelta;
			Payload->TargetStrength = 1.0f;
		}
	}
}

void UParticleModuleBeamSource::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	FBeam2TypeDataPayload* Payload = reinterpret_cast<FBeam2TypeDataPayload*>(
		reinterpret_cast<uint8*>(Context.ParticleBase) + Context.Offset);

	Payload->LockSource        = bLockSource ? 1 : 0;
	Payload->LockSourceTangent = bLockSourceTangent ? 1 : 0;

	FParticleBeam2EmitterInstance& BeamOwner = static_cast<FParticleBeam2EmitterInstance&>(Context.Owner);
	if (!BeamOwner.BeamModule || !BeamOwner.Component)
	{
		return;
	}

	const FMatrix& ComponentToWorld = BeamOwner.Component->GetWorldMatrix();
	FBeamResolveContext ResolveContext{
		BeamOwner,
		*BeamOwner.BeamModule,
		ComponentToWorld,
		Context.ParticleBase,
		BeamOwner.ActiveParticles,
		1.0f
	};
	Payload->SourcePoint = ResolveSource(ResolveContext);

	Payload->SourceTangent = ResolveSourceTangent(Payload->SourcePoint, Payload->TargetPoint);
	Payload->SourceStrength = std::max(0.0f, SourceStrength);

	if (!BeamOwner.BeamTargetModule || !BeamOwner.BeamTargetModule->bEnabled)
	{
		const FVector BeamDelta = Payload->TargetPoint - Payload->SourcePoint;
		Payload->TargetTangent = BeamDelta;
		Payload->TargetStrength = 1.0f;
	}
}
