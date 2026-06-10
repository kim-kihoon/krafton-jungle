#include "ParticleLODLevel.h"

#include "Particle/ParticleModule.h"
#include "Particle/Distributions/DistributionFloatConstant.h"
#include "Particle/Modules/ParticleModuleCollision.h"
#include "Particle/Modules/ParticleModuleEventGenerator.h"
#include "Particle/Modules/ParticleModuleRequired.h"
#include "Particle/Modules/ParticleModuleSpawn.h"
#include "Particle/Modules/ParticleModuleBeamNoise.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam.h"
#include "Particle/TypeData/ParticleModuleTypeDataBase.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Serialization/Archive.h"

namespace
{
	using ELODModuleSyncMode = UParticleLODLevel::ELODModuleSyncMode;

	template<typename T>
	T* DuplicateModuleForLOD(T* SourceModule, UParticleLODLevel* TargetLOD)
	{
		if (!SourceModule || !TargetLOD)
		{
			return nullptr;
		}

		T* DuplicatedModule = Cast<T>(SourceModule->Duplicate(TargetLOD));
		if (DuplicatedModule)
		{
			DuplicatedModule->SetOuter(TargetLOD);
		}

		return DuplicatedModule;
	}

	template<typename T>
	void DestroyLODModule(T*& Module)
	{
		if (!Module)
		{
			return;
		}

		UObjectManager::Get().DestroyObject(Module);
		Module = nullptr;
	}

	uint8 EncodeSyncMode(ELODModuleSyncMode InMode)
	{
		return static_cast<uint8>(InMode);
	}

	ELODModuleSyncMode DecodeSyncMode(uint8 RawMode)
	{
		return (RawMode == EncodeSyncMode(ELODModuleSyncMode::Override))
			? ELODModuleSyncMode::Override
			: ELODModuleSyncMode::InheritFromLOD0;
	}

	template<typename T>
	void SyncCoreModuleSlotFromLOD0(
		T*& TargetModule,
		T* SourceModule,
		UParticleLODLevel* TargetLOD,
		bool bShouldSyncFromLOD0)
	{
		// Core slots only re-materialize from LOD0 while they remain inherited.
		// Once a derived LOD explicitly overrides the slot, preserve it as-is.
		if (!bShouldSyncFromLOD0)
		{
			return;
		}

		DestroyLODModule(TargetModule);
		TargetModule = DuplicateModuleForLOD(SourceModule, TargetLOD);
	}

	void FullCopyRegularModulesFromLOD0(UParticleLODLevel* TargetLOD, UParticleLODLevel* LOD0)
	{
		// Compatibility/recovery path: rebuild the stored derived regular-module
		// graph as a concrete full copy from LOD0 when finer-grained sync metadata
		// is not trustworthy enough yet.
		for (UParticleModule*& Module : TargetLOD->Modules)
		{
			DestroyLODModule(Module);
		}
		TargetLOD->Modules.clear();

		for (UParticleModule* SourceModule : LOD0->Modules)
		{
			UParticleModule* DuplicatedModule = DuplicateModuleForLOD(SourceModule, TargetLOD);
			if (!DuplicatedModule)
			{
				continue;
			}

			TargetLOD->Modules.push_back(DuplicatedModule);
		}

		TargetLOD->ResetRegularModuleSyncModes(ELODModuleSyncMode::InheritFromLOD0);
		TargetLOD->ResetRegularModuleSourceLOD0Indices(-1, true);
	}

	bool IsPlausibleRegularModuleBinding(UParticleModule* DerivedModule, UParticleModule* SourceModule)
	{
		if (!DerivedModule || !SourceModule)
		{
			return false;
		}

		return DerivedModule->GetCategory() == SourceModule->GetCategory() &&
			DerivedModule->GetClass() == SourceModule->GetClass();
	}

	bool CanUseSourceBoundRegularModuleSync(UParticleLODLevel* TargetLOD, UParticleLODLevel* LOD0)
	{
		if (!TargetLOD || !LOD0)
		{
			return false;
		}

		if (TargetLOD->RegularModuleSyncModes.size() != TargetLOD->Modules.size() ||
			TargetLOD->RegularModuleSourceLOD0Indices.size() != TargetLOD->Modules.size())
		{
			return false;
		}

		for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(TargetLOD->Modules.size()); ++ModuleIndex)
		{
			if (TargetLOD->GetRegularModuleSyncMode(ModuleIndex) == ELODModuleSyncMode::Override)
			{
				continue;
			}

			const int32 SourceIndex = TargetLOD->GetRegularModuleSourceLOD0Index(ModuleIndex);
			if (SourceIndex < 0 || SourceIndex >= static_cast<int32>(LOD0->Modules.size()))
			{
				return false;
			}

			if (!IsPlausibleRegularModuleBinding(TargetLOD->Modules[ModuleIndex], LOD0->Modules[SourceIndex]))
			{
				return false;
			}
		}

		return true;
	}

	void SyncRegularModulesFromLOD0(UParticleLODLevel* TargetLOD, UParticleLODLevel* LOD0)
	{
		if (!TargetLOD->HasRegularModuleOverrides() ||
			!CanUseSourceBoundRegularModuleSync(TargetLOD, LOD0))
		{
			// Metadata-driven partial resync is not trustworthy here, so rebuild the
			// derived regular-module graph from LOD0 and reset inherited bindings.
			FullCopyRegularModulesFromLOD0(TargetLOD, LOD0);
			return;
		}

		TArray<UParticleModule*> PreviousModules = TargetLOD->Modules;
		TArray<uint8> PreviousSyncModes = TargetLOD->RegularModuleSyncModes;
		TArray<int32> PreviousSourceLOD0Indices = TargetLOD->RegularModuleSourceLOD0Indices;
		TargetLOD->Modules.clear();
		TargetLOD->RegularModuleSyncModes.clear();
		TargetLOD->RegularModuleSourceLOD0Indices.clear();

		for (int32 SourceIndex = 0; SourceIndex < static_cast<int32>(LOD0->Modules.size()); ++SourceIndex)
		{
			UParticleModule* MatchedInheritedModule = nullptr;
			int32 MatchedPreviousIndex = -1;
			for (int32 PreviousIndex = 0; PreviousIndex < static_cast<int32>(PreviousModules.size()); ++PreviousIndex)
			{
				if (PreviousModules[PreviousIndex] == nullptr ||
					PreviousIndex >= static_cast<int32>(PreviousSyncModes.size()) ||
					PreviousIndex >= static_cast<int32>(PreviousSourceLOD0Indices.size()) ||
					DecodeSyncMode(PreviousSyncModes[PreviousIndex]) != ELODModuleSyncMode::InheritFromLOD0 ||
					PreviousSourceLOD0Indices[PreviousIndex] != SourceIndex)
				{
					continue;
				}

				if (!IsPlausibleRegularModuleBinding(PreviousModules[PreviousIndex], LOD0->Modules[SourceIndex]))
				{
					continue;
				}

				MatchedInheritedModule = PreviousModules[PreviousIndex];
				MatchedPreviousIndex = PreviousIndex;
				break;
			}

			if (MatchedInheritedModule)
			{
				MatchedInheritedModule->SetOuter(TargetLOD);
				TargetLOD->Modules.push_back(MatchedInheritedModule);
				TargetLOD->SetRegularModuleSyncMode(
					static_cast<int32>(TargetLOD->Modules.size()) - 1,
					ELODModuleSyncMode::InheritFromLOD0);
				TargetLOD->SetRegularModuleSourceLOD0Index(
					static_cast<int32>(TargetLOD->Modules.size()) - 1,
					SourceIndex);
				PreviousModules[MatchedPreviousIndex] = nullptr;
				continue;
			}

			UParticleModule* DuplicatedModule = DuplicateModuleForLOD(LOD0->Modules[SourceIndex], TargetLOD);
			if (!DuplicatedModule)
			{
				continue;
			}

			TargetLOD->Modules.push_back(DuplicatedModule);
			TargetLOD->SetRegularModuleSyncMode(
				static_cast<int32>(TargetLOD->Modules.size()) - 1,
				ELODModuleSyncMode::InheritFromLOD0);
			TargetLOD->SetRegularModuleSourceLOD0Index(
				static_cast<int32>(TargetLOD->Modules.size()) - 1,
				SourceIndex);
		}

		for (int32 PreviousIndex = 0;
		     PreviousIndex < static_cast<int32>(PreviousModules.size());
		     ++PreviousIndex)
		{
			const bool bKeepExtraOverrideModule =
				PreviousIndex < static_cast<int32>(PreviousSyncModes.size()) &&
				DecodeSyncMode(PreviousSyncModes[PreviousIndex]) == ELODModuleSyncMode::Override &&
				PreviousModules[PreviousIndex] != nullptr;

			if (!bKeepExtraOverrideModule)
			{
				continue;
			}

			PreviousModules[PreviousIndex]->SetOuter(TargetLOD);
			TargetLOD->Modules.push_back(PreviousModules[PreviousIndex]);
			TargetLOD->SetRegularModuleSyncMode(
				static_cast<int32>(TargetLOD->Modules.size()) - 1,
				ELODModuleSyncMode::Override);
			TargetLOD->SetRegularModuleSourceLOD0Index(
				static_cast<int32>(TargetLOD->Modules.size()) - 1,
				-1);
			PreviousModules[PreviousIndex] = nullptr;
		}

		for (UParticleModule*& PreviousModule : PreviousModules)
		{
			DestroyLODModule(PreviousModule);
		}
	}

	void ResynchronizeInheritedCoreSlotsFromLOD0(UParticleLODLevel* TargetLOD, UParticleLODLevel* LOD0)
	{
		if (!TargetLOD || !LOD0)
		{
			return;
		}

		// Inherited core slots re-materialize directly onto the stored derived LOD
		// graph. Derived overrides remain untouched because their sync flags already
		// opt out at the slot level.
		SyncCoreModuleSlotFromLOD0(TargetLOD->RequiredModule, LOD0->RequiredModule, TargetLOD, TargetLOD->bSyncRequiredModuleFromLOD0);
		SyncCoreModuleSlotFromLOD0(TargetLOD->SpawnModule, LOD0->SpawnModule, TargetLOD, TargetLOD->bSyncSpawnModuleFromLOD0);
		SyncCoreModuleSlotFromLOD0(TargetLOD->TypeDataModule, LOD0->TypeDataModule, TargetLOD, TargetLOD->bSyncTypeDataModuleFromLOD0);
	}

	void ResynchronizeInheritedRegularModulesFromLOD0(UParticleLODLevel* TargetLOD, UParticleLODLevel* LOD0)
	{
		if (!TargetLOD || !LOD0)
		{
			return;
		}

		// Inherited regular modules follow their explicit LOD0 source bindings on
		// the stored derived graph. Explicit override modules stay local to the
		// derived LOD and are preserved. This is the structural part of resync;
		// automatic reduction remains a later, separate shaping pass rather than
		// being treated as an explicit override.
		SyncRegularModulesFromLOD0(TargetLOD, LOD0);
	}

	float GetLODReductionScale(const UParticleLODLevel* TargetLOD)
	{
		if (!TargetLOD || TargetLOD->Level <= 0)
		{
			return 1.0f;
		}

		return (TargetLOD->Level == 1) ? 0.5f : 0.25f;
	}

	float ClampNonNegativeFloat(float Value)
	{
		return (Value < 0.0f) ? 0.0f : Value;
	}

	int32 ClampPositiveInt(int32 Value, int32 MinValue)
	{
		return (Value < MinValue) ? MinValue : Value;
	}

	void ApplySpawnLODReduction(UParticleLODLevel* TargetLOD, float ReductionScale)
	{
		if (!TargetLOD || !TargetLOD->SpawnModule || !TargetLOD->bSyncSpawnModuleFromLOD0)
		{
			return;
		}

		// Keep burst authoring intact for now and reduce only the continuous rate.
		// Arbitrary curves/uniforms can gain dedicated scaling support later.
		if (UDistributionFloatConstant* RateScale = Cast<UDistributionFloatConstant>(TargetLOD->SpawnModule->RateScaleDistribution))
		{
			RateScale->Constant = ClampNonNegativeFloat(RateScale->Constant * ReductionScale);
			return;
		}

		if (UDistributionFloatConstant* Rate = Cast<UDistributionFloatConstant>(TargetLOD->SpawnModule->RateDistribution))
		{
			Rate->Constant = ClampNonNegativeFloat(Rate->Constant * ReductionScale);
		}
	}

	void ApplyRibbonLODReduction(UParticleLODLevel* TargetLOD, float ReductionScale)
	{
		if (!TargetLOD || !TargetLOD->bSyncTypeDataModuleFromLOD0)
		{
			return;
		}

		UParticleModuleTypeDataRibbon* RibbonTypeData = Cast<UParticleModuleTypeDataRibbon>(TargetLOD->TypeDataModule);
		if (!RibbonTypeData)
		{
			return;
		}

		RibbonTypeData->MaxTessellation = ClampPositiveInt(
			static_cast<int32>(RibbonTypeData->MaxTessellation * ReductionScale),
			1);
	}

	void ApplyBeamLODReduction(UParticleLODLevel* TargetLOD, float ReductionScale)
	{
		if (!TargetLOD || !TargetLOD->bSyncTypeDataModuleFromLOD0)
		{
			return;
		}

		UParticleModuleTypeDataBeam* BeamTypeData = Cast<UParticleModuleTypeDataBeam>(TargetLOD->TypeDataModule);
		if (!BeamTypeData)
		{
			return;
		}

		BeamTypeData->InterpolationPoints = ClampPositiveInt(
			static_cast<int32>(BeamTypeData->InterpolationPoints * ReductionScale),
			1);

		for (UParticleModule* Module : TargetLOD->Modules)
		{
			UParticleModuleBeamNoise* BeamNoise = Cast<UParticleModuleBeamNoise>(Module);
			if (!BeamNoise)
			{
				continue;
			}

			BeamNoise->NoiseTessellation = ClampPositiveInt(
				static_cast<int32>(BeamNoise->NoiseTessellation * ReductionScale),
				0);
		}
	}

	void ApplyOptionalFeatureLODReductionBoundary(UParticleLODLevel* TargetLOD)
	{
		if (!TargetLOD)
		{
			return;
		}

		for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(TargetLOD->Modules.size()); ++ModuleIndex)
		{
			if (TargetLOD->GetRegularModuleSyncMode(ModuleIndex) != ELODModuleSyncMode::InheritFromLOD0)
			{
				continue;
			}

			if (Cast<UParticleModuleCollision>(TargetLOD->Modules[ModuleIndex]))
			{
				// Collision can affect gameplay/event semantics, so this first pass
				// keeps it intact and leaves any disable policy explicit and opt-in.
				continue;
			}

			if (Cast<UParticleModuleEventGenerator>(TargetLOD->Modules[ModuleIndex]))
			{
				// Event emission can also be gameplay-visible. Leave it enabled until
				// we introduce an explicit policy for visual-only emitters.
				continue;
			}
		}
	}

	void ApplyDeferredLODReductionPolicy(UParticleLODLevel* TargetLOD)
	{
		if (!TargetLOD)
		{
			return;
		}

		// Structural sync still happens first. Reduction policy is now a distinct
		// phase that turns inherited lower LODs into cheaper materialized variants
		// without collapsing future explicit override boundaries. A later effective
		// runtime LOD build step may eventually own more of this shaping work.
		const float ReductionScale = GetLODReductionScale(TargetLOD);
		if (ReductionScale >= 1.0f)
		{
			return;
		}

		ApplySpawnLODReduction(TargetLOD, ReductionScale);
		ApplyRibbonLODReduction(TargetLOD, ReductionScale);
		ApplyBeamLODReduction(TargetLOD, ReductionScale);
		ApplyOptionalFeatureLODReductionBoundary(TargetLOD);
	}

	void ReapplyInheritedLODReductionFromLOD0(UParticleLODLevel* TargetLOD)
	{
		if (!TargetLOD)
		{
			return;
		}

		// Reduction stays a distinct post-sync shaping phase. It recalculates
		// cheaper inherited lower-LOD variants after source-driven resync and does
		// not redefine which slots/modules are explicit overrides. If an author
		// wants a local value to stop following automatic reduction, that value
		// should move onto an explicit derived override path instead. Today this
		// still mutates the stored materialized derived LOD graph; future runtime
		// effective-LOD materialization may choose to consume the same intent later
		// in a runtime-only build pipeline instead.
		ApplyDeferredLODReductionPolicy(TargetLOD);
	}
}

void UParticleLODLevel::PostDuplicate()
{
	UObject::PostDuplicate();

	if (RequiredModule)
	{
		RequiredModule->SetOuter(this);
		RequiredModule->PostDuplicate();
	}

	if (SpawnModule)
	{
		SpawnModule->SetOuter(this);
		SpawnModule->PostDuplicate();
	}

	if (TypeDataModule)
	{
		TypeDataModule->SetOuter(this);
		TypeDataModule->PostDuplicate();
	}

	for (UParticleModule* Module : Modules)
	{
		if (!Module)
		{
			continue;
		}

		Module->SetOuter(this);
		Module->PostDuplicate();
	}

	NormalizeCoreSlotSyncMetadata();
	NormalizeRegularModuleSyncMetadata();
}

void UParticleLODLevel::UpdateFromLOD0(UParticleLODLevel* LOD0)
{
	if (!LOD0 || LOD0 == this)
	{
		return;
	}

	// LOD0 acts as the master/source for inherited derived-LOD data.
	// This path re-materializes inherited core slots, re-syncs inherited regular
	// modules from their explicit LOD0 source bindings, reapplies reduction for
	// inherited cost shaping, preserves explicit overrides, and falls back to a
	// full-copy rebuild when metadata-driven regular-module resync is untrustworthy.
	// The result is still the stored materialized derived LOD graph that current
	// runtime code consumes directly. A future effective runtime LOD build step
	// could later read the same source/override intent and materialize a separate
	// runtime-only result without changing this authoring-side resync meaning.
	// Authoring interpretation:
	// - "Resync from LOD0" refreshes inherited data only
	// - explicit overrides stay local
	// - reduction is recalculated after inherited data is refreshed
	NormalizeCoreSlotSyncMetadata();
	bEnabled = LOD0->bEnabled;

	ResynchronizeInheritedCoreSlotsFromLOD0(this, LOD0);
	ResynchronizeInheritedRegularModulesFromLOD0(this, LOD0);
	ReapplyInheritedLODReductionFromLOD0(this);
}

UParticleLODLevel::ELODModuleSyncMode UParticleLODLevel::GetRegularModuleSyncMode(int32 ModuleIndex) const
{
	if (ModuleIndex < 0 || ModuleIndex >= static_cast<int32>(RegularModuleSyncModes.size()))
	{
		return ELODModuleSyncMode::InheritFromLOD0;
	}

	return DecodeSyncMode(RegularModuleSyncModes[ModuleIndex]);
}

int32 UParticleLODLevel::GetRegularModuleSourceLOD0Index(int32 ModuleIndex) const
{
	if (ModuleIndex < 0 || ModuleIndex >= static_cast<int32>(RegularModuleSourceLOD0Indices.size()))
	{
		return -1;
	}

	return RegularModuleSourceLOD0Indices[ModuleIndex];
}

void UParticleLODLevel::SetRegularModuleSyncMode(int32 ModuleIndex, ELODModuleSyncMode InMode)
{
	if (ModuleIndex < 0)
	{
		return;
	}

	while (static_cast<int32>(RegularModuleSyncModes.size()) <= ModuleIndex)
	{
		RegularModuleSyncModes.push_back(EncodeSyncMode(ELODModuleSyncMode::InheritFromLOD0));
	}

	RegularModuleSyncModes[ModuleIndex] = EncodeSyncMode(InMode);
}

void UParticleLODLevel::SetRegularModuleSourceLOD0Index(int32 ModuleIndex, int32 SourceIndex)
{
	if (ModuleIndex < 0)
	{
		return;
	}

	while (static_cast<int32>(RegularModuleSourceLOD0Indices.size()) <= ModuleIndex)
	{
		RegularModuleSourceLOD0Indices.push_back(-1);
	}

	RegularModuleSourceLOD0Indices[ModuleIndex] = SourceIndex;
}

bool UParticleLODLevel::HasRegularModuleOverrides() const
{
	for (uint8 RawMode : RegularModuleSyncModes)
	{
		if (DecodeSyncMode(RawMode) == ELODModuleSyncMode::Override)
		{
			return true;
		}
	}

	return false;
}

void UParticleLODLevel::NormalizeCoreSlotSyncMetadata()
{
	// Required/Spawn cannot be meaningfully "removed" in a derived override state,
	// so malformed null overrides fall back to inherited re-materialization.
	if (!RequiredModule && !bSyncRequiredModuleFromLOD0)
	{
		bSyncRequiredModuleFromLOD0 = true;
	}

	if (!SpawnModule && !bSyncSpawnModuleFromLOD0)
	{
		bSyncSpawnModuleFromLOD0 = true;
	}

	// TypeData uses nullptr as a valid "default sprite path" override, so keep the
	// explicit override state when the derived LOD intentionally clears the slot.
	// That makes "clear the slot and keep sprite/default behavior" a meaningful
	// authoring action instead of an invalid state.
}

void UParticleLODLevel::ResetRegularModuleSyncModes(ELODModuleSyncMode DefaultMode)
{
	RegularModuleSyncModes.clear();

	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
	{
		RegularModuleSyncModes.push_back(EncodeSyncMode(DefaultMode));
	}
}

void UParticleLODLevel::ResetRegularModuleSourceLOD0Indices(int32 DefaultSourceIndex, bool bMapToCurrentIndex)
{
	RegularModuleSourceLOD0Indices.clear();

	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
	{
		RegularModuleSourceLOD0Indices.push_back(bMapToCurrentIndex ? ModuleIndex : DefaultSourceIndex);
	}
}

void UParticleLODLevel::NormalizeRegularModuleSyncMetadata()
{
	if (RegularModuleSyncModes.size() != Modules.size())
	{
		ResetRegularModuleSyncModes();
	}

	if (RegularModuleSourceLOD0Indices.size() != Modules.size())
	{
		ResetRegularModuleSourceLOD0Indices();
	}

	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
	{
		if (GetRegularModuleSyncMode(ModuleIndex) == ELODModuleSyncMode::Override)
		{
			// Override modules keep no inherited source binding. Reset-to-inherited
			// style tools should restore both the sync mode and the source binding.
			SetRegularModuleSourceLOD0Index(ModuleIndex, -1);
			continue;
		}

		if (GetRegularModuleSourceLOD0Index(ModuleIndex) < 0)
		{
			SetRegularModuleSourceLOD0Index(ModuleIndex, ModuleIndex);
		}
	}
}

bool UParticleLODLevel::ValidateModules() const
{
	if (!RequiredModule) return false;
	if (!SpawnModule) return false;

	if (RequiredModule->GetCategory() != UParticleModule::EModuleCategory::Required) return false;
	if (SpawnModule->GetCategory() != UParticleModule::EModuleCategory::Spawn) return false;
	if (TypeDataModule && TypeDataModule->GetCategory() != UParticleModule::EModuleCategory::TypeData) return false;

	for (int32 i = 0; i < static_cast<int32>(Modules.size()); ++i)
	{
		UParticleModule* A = Modules[i];
		if (!A) return false;

		const auto Category = A->GetCategory();

		if (Category == UParticleModule::EModuleCategory::Required ||
			Category == UParticleModule::EModuleCategory::Spawn ||
			Category == UParticleModule::EModuleCategory::TypeData)
		{
			return false;
		}

		for (int32 j = i + 1; j < static_cast<int32>(Modules.size()); ++j)
		{
			UParticleModule* B = Modules[j];
			if (!B) return false;

			if (A == B) return false;
		}
	}

	return true;
}

bool UParticleLODLevel::AddModule(UParticleModule* InModule)
{
	if (!InModule) return false;

	switch (InModule->GetCategory())
	{
	case UParticleModule::EModuleCategory::Required:
	{
		auto* Required = Cast<UParticleModuleRequired>(InModule);
		if (!Required) return false;
		if (RequiredModule && RequiredModule != Required) return false;

		RequiredModule = Required;
		RequiredModule->SetOuter(this);
		// Explicit assignment means this derived LOD now owns the slot override.
		bSyncRequiredModuleFromLOD0 = false;
		return true;
	}
	case UParticleModule::EModuleCategory::Spawn:
	{
		auto* Spawn = Cast<UParticleModuleSpawn>(InModule);
		if (!Spawn) return false;
		if (SpawnModule && SpawnModule != Spawn) return false;

		SpawnModule = Spawn;
		SpawnModule->SetOuter(this);
		// Keep automatic reduction aligned with the sync flag: inherited slots can
		// still be reduced, explicit derived spawn overrides should be preserved.
		// Authoring meaning: changing this slot locally opts out of "follow LOD0
		// then reduce" and turns the slot into a local derived decision.
		bSyncSpawnModuleFromLOD0 = false;
		return true;
	}
	case UParticleModule::EModuleCategory::TypeData:
	{
		auto* TypeData = Cast<UParticleModuleTypeDataBase>(InModule);
		if (!TypeData) return false;
		if (TypeDataModule && TypeDataModule != TypeData) return false;

		TypeDataModule = TypeData;
		TypeDataModule->SetOuter(this);
		// TypeData override can intentionally diverge from LOD0, including later
		// using nullptr to fall back to the sprite/default rendering path.
		bSyncTypeDataModuleFromLOD0 = false;
		return true;
	}
		
	default :
		break;
	}

	for (UParticleModule* Existing : Modules)
	{
		if (Existing == InModule) return false;

		// Regular module 중에서도 Beam Source/Target/Noise처럼 단일 슬롯 성격인
		// 모듈은 같은 클래스가 중복 추가되지 않게 막는다.
		if (Existing && InModule->IsUnique() && Existing->GetClass() == InModule->GetClass())
		{
			return false;
		}
	}

	InModule->SetOuter(this);
	Modules.push_back(InModule);
	// Regular modules added directly to a derived LOD are authored as local
	// overrides first; inherited source binding is only meaningful after a reset
	// to inherited / resync-style action restores the LOD0 relationship.
	SetRegularModuleSyncMode(static_cast<int32>(Modules.size()) - 1, ELODModuleSyncMode::Override);
	SetRegularModuleSourceLOD0Index(static_cast<int32>(Modules.size()) - 1, -1);
	return true;
}

bool UParticleLODLevel::RemoveModule(UParticleModule* InModule)
{
	if (!InModule) return false;

	if (RequiredModule == InModule)
	{
		return false;	// 삭제 불가
	}

	if (SpawnModule == InModule)
	{
		return false;	// 삭제 불가
	}

	if (TypeDataModule == InModule)
	{
		TypeDataModule = nullptr;
		// Clearing type data is a valid explicit override: the derived LOD falls
		// back to the sprite/default path instead of re-materializing from LOD0.
		bSyncTypeDataModuleFromLOD0 = false;
		return true;
	}

	for (int32 i = 0; i < static_cast<int32>(Modules.size()); ++i)
	{
		if (Modules[i] == InModule)
		{
			Modules.erase(Modules.begin() + i);
			if (i < static_cast<int32>(RegularModuleSyncModes.size()))
			{
				RegularModuleSyncModes.erase(RegularModuleSyncModes.begin() + i);
			}
			if (i < static_cast<int32>(RegularModuleSourceLOD0Indices.size()))
			{
				RegularModuleSourceLOD0Indices.erase(RegularModuleSourceLOD0Indices.begin() + i);
			}
			return true;
		}
	}

	return false;
}
