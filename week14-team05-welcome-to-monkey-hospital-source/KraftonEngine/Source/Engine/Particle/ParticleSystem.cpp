#include "ParticleSystem.h"

#include "ParticleLODLevel.h"
#include "Particle/ParticleEmitter.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
	constexpr float DefaultAutomaticLODDistanceStep = 1000.0f;
	constexpr float MinimumAutomaticLODDistanceHysteresis = 0.0f;
	constexpr float MinimumAutomaticLODSwitchDelay = 0.0f;
}

void UParticleSystem::OnPostLoad(FArchive& /*Ar*/)
{
	BuildEmitters();
	EnsureLODDistances();
}

UObject* UParticleSystem::Duplicate(UObject* NewOuter) const
{
	return UObject::Duplicate(NewOuter);
}

void UParticleSystem::PostDuplicate()
{
	UObject::PostDuplicate();

	// Instanced 배열의 하위 객체들은 UObject::Duplicate()에서 루트처럼
	// PostDuplicate()가 자동 호출되지 않는다. ParticleSystem이 소유한
	// Emitter graph를 여기서 재귀적으로 정리한다.
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		Emitter->SetOuter(this);
		Emitter->PostDuplicate();
	}

	BuildEmitters();
	EnsureLODDistances();
}

void UParticleSystem::NormalizeAutomaticLODTuning()
{
	// Automatic LOD quality depends at least as much on authored thresholds as on
	// the runtime selection code. Keep defaults conservative and predictable, then
	// tune in this order: distances first, hysteresis second, switch delay third.
	// This normalization only keeps values sane; it does not replace per-effect
	// authoring judgment or reduction-policy-aware tuning.
	if (LODDistanceHysteresis < MinimumAutomaticLODDistanceHysteresis)
	{
		LODDistanceHysteresis = MinimumAutomaticLODDistanceHysteresis;
	}

	if (LODSwitchDelay < MinimumAutomaticLODSwitchDelay)
	{
		LODSwitchDelay = MinimumAutomaticLODSwitchDelay;
	}
}

UParticleEmitter* UParticleSystem::AddEmitter()      
{ 
	UParticleEmitter* NewEmitter = UObjectManager::Get().CreateObject<UParticleEmitter>(this);
	if (!NewEmitter) return nullptr;

	NewEmitter->EmitterName = MakeUniqueEmitterName();
	NewEmitter->InitializeDefaultLODLevel();

	Emitters.push_back(NewEmitter);
	EnsureLODDistances();
	return NewEmitter;
}

void UParticleSystem::RemoveEmitter(int32 Index)
{
	if (Index < 0 || Index >= static_cast<int32>(Emitters.size())) return;

	// TODO: 우선 참조 제거, 나중에 UObjectManager::DestroyObject까지 하면 LOD/Module 자식 정리 정책도 같이 잡아야 함
	Emitters.erase(Emitters.begin() + Index);
	EnsureLODDistances();
}

void UParticleSystem::MoveEmitter(int32 FromIndex, int32 ToIndex)
{
	const int32 Count = static_cast<int32>(Emitters.size());
	if (FromIndex < 0 || FromIndex >= Count) return;
	if (ToIndex < 0 || ToIndex >= Count) return;
	if (FromIndex == ToIndex) return;

	UParticleEmitter* Moving = Emitters[FromIndex];
	Emitters.erase(Emitters.begin() + FromIndex);
	Emitters.insert(Emitters.begin() + ToIndex, Moving);
	EnsureLODDistances();
}

UParticleEmitter* UParticleSystem::GetEmitter(int32 Index) const
{
	if (Index < 0 || Index >= static_cast<int32>(Emitters.size())) return nullptr;
	return Emitters[Index];
}

void UParticleSystem::BuildEmitters()
{
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter) continue;

		Emitter->EnsureLODCoreModules();

		bool bHasValidLOD = false;
		for (int32 LODIndex = 0; LODIndex < Emitter->GetLODCount(); ++LODIndex)
		{
			UParticleLODLevel* LOD = Emitter->GetLODLevel(LODIndex);
			if (LOD && LOD->ValidateModules())
			{
				bHasValidLOD = true;
			}
		}

		if (!bHasValidLOD)
		{
			continue;
		}

		Emitter->CacheEmitterModuleInfo();
	}

	EnsureLODDistances();
}

int32 UParticleSystem::GetMaxLODCount() const
{
	int32 MaxLODCount = 0;

	for (const UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		MaxLODCount = std::max(MaxLODCount, Emitter->GetLODCount());
	}

	return MaxLODCount;
}

void UParticleSystem::EnsureLODDistances()
{
	NormalizeAutomaticLODTuning();

	const int32 LODCount = std::max(1, GetMaxLODCount());

	while (static_cast<int32>(LODDistances.size()) < LODCount)
	{
		const int32 NewIndex = static_cast<int32>(LODDistances.size());
		float NewDistance = 0.0f;

		if (NewIndex > 0)
		{
			const float PreviousDistance = !LODDistances.empty()
				? LODDistances.back()
				: 0.0f;
			NewDistance = PreviousDistance + DefaultAutomaticLODDistanceStep;
		}

		LODDistances.push_back(NewDistance);
	}

	while (static_cast<int32>(LODDistances.size()) > LODCount)
	{
		LODDistances.pop_back();
	}

	if (!LODDistances.empty())
	{
		// LOD0 always starts at distance zero. Additional thresholds should be read
		// as authoring breakpoints, not as guaranteed "good" values for every effect.
		// Beam/ribbon-sensitive effects often need more conservative thresholds, and
		// strong lower-LOD reduction may require switching farther out. Close-range
		// gameplay VFX, general gameplay VFX, and background/environment VFX may all
		// want different threshold families even when they share the same runtime code.
		LODDistances[0] = 0.0f;
	}

	for (int32 Index = 1; Index < static_cast<int32>(LODDistances.size()); ++Index)
	{
		if (LODDistances[Index] < LODDistances[Index - 1])
		{
			LODDistances[Index] = LODDistances[Index - 1];
		}
	}
}

int32 UParticleSystem::GetLODIndexForDistance(float Distance) const
{
	if (LODDistances.empty())
	{
		return 0;
	}

	const float SafeDistance = std::max(0.0f, Distance);
	int32 ResultLODIndex = 0;

	for (int32 Index = 0; Index < static_cast<int32>(LODDistances.size()); ++Index)
	{
		if (SafeDistance >= LODDistances[Index])
		{
			ResultLODIndex = Index;
			continue;
		}

		break;
	}

	const int32 MaxLODIndex = std::max(0, GetMaxLODCount() - 1);
	return std::clamp(ResultLODIndex, 0, MaxLODIndex);
}

float UParticleSystem::GetLODDistance(int32 LODIndex) const
{
	if (LODIndex < 0 || LODIndex >= static_cast<int32>(LODDistances.size()))
	{
		return 0.0f;
	}

	return LODDistances[LODIndex];
}

void UParticleSystem::SetLODDistance(int32 LODIndex, float Distance)
{
	if (LODIndex < 0)
	{
		return;
	}

	EnsureLODDistances();

	if (LODIndex >= static_cast<int32>(LODDistances.size()))
	{
		return;
	}

	LODDistances[LODIndex] = std::max(0.0f, Distance);

	if (!LODDistances.empty())
	{
		LODDistances[0] = 0.0f;
	}

	for (int32 Index = 1; Index < static_cast<int32>(LODDistances.size()); ++Index)
	{
		if (LODDistances[Index] < LODDistances[Index - 1])
		{
			LODDistances[Index] = LODDistances[Index - 1];
		}
	}
}

FString UParticleSystem::MakeUniqueEmitterName()
{
	int32 Index = 0;

	while (true)
	{
		FString Candidate = "Emitter " + std::to_string(Index);

		bool bExists = false;
		for (UParticleEmitter* Emitter : Emitters)
		{
			if (Emitter && Emitter->EmitterName == Candidate)
			{
				bExists = true;
				break;
			}
		}

		if (!bExists)
		{
			return Candidate;
		}

		++Index;
	}
}
