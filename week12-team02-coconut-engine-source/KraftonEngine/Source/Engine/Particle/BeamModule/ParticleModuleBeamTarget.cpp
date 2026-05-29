#include "Particle/BeamModule/ParticleModuleBeamTarget.h"

#include "Component/ParticleSystemComponent.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleHelper.h"
#include "Particle/BeamModule/ParticleModuleBeamSource.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam2.h"

#include <algorithm>

UParticleModule* UParticleModuleBeamTarget::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleBeamTarget* Copy = GUObjectArray.CreateObject<UParticleModuleBeamTarget>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->TargetMethod    = TargetMethod;
	Copy->TargetName      = TargetName;
	Copy->bTargetAbsolute = bTargetAbsolute;
	Copy->bLockTarget     = bLockTarget;
	Copy->Target          = Target;
	Copy->bLockTargetTangent = bLockTargetTangent;
	Copy->TargetTangent      = TargetTangent;
	Copy->TargetStrength     = TargetStrength;
	return Copy;
}

FVector UParticleModuleBeamTarget::ResolveTarget(const FBeamResolveContext& Context, const FVector& ResolvedSource) const
{
	const FMatrix WorldToComponent = Context.ComponentToWorld.GetInverse();
	auto ToPayloadSpace = [&](const FVector& Position, bool bAbsolute)
	{
		return bAbsolute ? WorldToComponent.TransformPositionWithW(Position) : Position;
	};

	auto DefaultTarget = [&]()
	{
		return Context.TypeData.BeamMethod == PEB2M_Distance
			? ResolvedSource + FVector(Context.TypeData.Distance, 0.0f, 0.0f)
			: Context.TypeData.TargetPoint;
	};

	switch (TargetMethod)
	{
	case PEB2STM_Default:
		return DefaultTarget();
	case PEB2STM_UserSet:
		return ToPayloadSpace(Target, bTargetAbsolute);
	case PEB2STM_Emitter:
		// Sibling emitter on the same component; fall back to the BeamMethod
		// default when TargetName is unset or no sibling matches.
		if (FParticleEmitterInstance* Sibling = Context.Owner.FindSiblingEmitter(TargetName))
		{
			return WorldToComponent.TransformPositionWithW(Sibling->Location);
		}
		return DefaultTarget();
	case PEB2STM_Particle:
		// TODO: pick a particle from the sibling emitter named by TargetName and
		// use its world location. Needs both the sibling lookup (have it) and a
		// per-frame particle pick policy. Default fallback for now.
		return DefaultTarget();
	case PEB2STM_Actor:
		// TODO: resolve a scene actor (typically by TargetName) and use its
		// world location. Needs an actor registry hook that doesn't exist on
		// this engine yet. Default fallback for now.
		return DefaultTarget();
	default:
		return Context.TypeData.TargetPoint;
	}
}

FVector UParticleModuleBeamTarget::ResolveTargetTangent(const FVector& ResolvedSource, const FVector& ResolvedTarget) const
{
	// Hermite default: tangent at target equals the source→target direction, so
	// a zero-tangent setup yields a straight line.
	return TargetTangent.IsNearlyZero()
		? (ResolvedTarget - ResolvedSource)
		: TargetTangent;
}

void UParticleModuleBeamTarget::Update(const FUpdateContext& UpdateContext)
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

		Payload->LockTarget = bLockTarget ? 1 : 0;
		if (Payload->LockTarget == 0)
		{
			FBeamResolveContext ResolveContext{ BeamOwner, *BeamOwner.BeamModule, ComponentToWorld, Particle, Index, 1.0f };
			Payload->TargetPoint = ResolveTarget(ResolveContext, Payload->SourcePoint);
		}

		const FVector BeamDelta = Payload->TargetPoint - Payload->SourcePoint;
		if (BeamOwner.BeamSourceModule && BeamOwner.BeamSourceModule->bEnabled)
		{
			Payload->LockSourceTangent = BeamOwner.BeamSourceModule->bLockSourceTangent ? 1 : 0;
			if (Payload->LockSourceTangent == 0)
			{
				Payload->SourceTangent = BeamOwner.BeamSourceModule->ResolveSourceTangent(
					Payload->SourcePoint, Payload->TargetPoint);
				Payload->SourceStrength = std::max(0.0f, BeamOwner.BeamSourceModule->SourceStrength);
			}
		}
		else if (Payload->LockSourceTangent == 0)
		{
			Payload->SourceTangent = BeamDelta;
			Payload->SourceStrength = 1.0f;
		}

		Payload->LockTargetTangent = bLockTargetTangent ? 1 : 0;
		if (Payload->LockTargetTangent == 0)
		{
			Payload->TargetTangent = ResolveTargetTangent(Payload->SourcePoint, Payload->TargetPoint);
			Payload->TargetStrength = std::max(0.0f, TargetStrength);
		}
	}
}

void UParticleModuleBeamTarget::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	FBeam2TypeDataPayload* Payload = reinterpret_cast<FBeam2TypeDataPayload*>(
		reinterpret_cast<uint8*>(Context.ParticleBase) + Context.Offset);

	Payload->LockTarget        = bLockTarget ? 1 : 0;
	Payload->LockTargetTangent = bLockTargetTangent ? 1 : 0;

	FParticleBeam2EmitterInstance& BeamOwner = static_cast<FParticleBeam2EmitterInstance&>(Context.Owner);
	if (BeamOwner.BeamModule && BeamOwner.Component)
	{
		const FMatrix& ComponentToWorld = BeamOwner.Component->GetWorldMatrix();
		FBeamResolveContext ResolveContext{
			BeamOwner,
			*BeamOwner.BeamModule,
			ComponentToWorld,
			Context.ParticleBase,
			BeamOwner.ActiveParticles,
			1.0f
		};
		Payload->TargetPoint = ResolveTarget(ResolveContext, Payload->SourcePoint);
	}

	// Default tangent re-derivation now that the endpoints reflect both modules.
	const FVector BeamDelta = Payload->TargetPoint - Payload->SourcePoint;
	if (BeamOwner.BeamSourceModule && BeamOwner.BeamSourceModule->bEnabled)
	{
		Payload->LockSourceTangent = BeamOwner.BeamSourceModule->bLockSourceTangent ? 1 : 0;
		Payload->SourceTangent = BeamOwner.BeamSourceModule->ResolveSourceTangent(
			Payload->SourcePoint, Payload->TargetPoint);
		Payload->SourceStrength = std::max(0.0f, BeamOwner.BeamSourceModule->SourceStrength);
	}
	else if (Payload->LockSourceTangent == 0)
	{
		Payload->SourceTangent = BeamDelta;
		Payload->SourceStrength = 1.0f;
	}

	Payload->TargetTangent = ResolveTargetTangent(Payload->SourcePoint, Payload->TargetPoint);
	Payload->TargetStrength = std::max(0.0f, TargetStrength);
}
