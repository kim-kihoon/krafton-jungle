#include "Particle/ParticleSystem.h"

#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"

#include <algorithm>

namespace
{
	constexpr float DefaultLODDistanceStep = 1250.0f;

	void DestroyLODLevel(UParticleLODLevel* LOD)
	{
		if (!LOD)
		{
			return;
		}

		for (UParticleModule* Module : LOD->Modules)
		{
			if (Module)
			{
				if (Module == LOD->TypeDataModule)
				{
					LOD->TypeDataModule = nullptr;
				}
				GUObjectArray.DestroyObject(Module);
			}
		}
		LOD->Modules.clear();

		if (LOD->RequiredModule)
		{
			GUObjectArray.DestroyObject(LOD->RequiredModule);
			LOD->RequiredModule = nullptr;
		}
		if (LOD->SpawnModule)
		{
			GUObjectArray.DestroyObject(LOD->SpawnModule);
			LOD->SpawnModule = nullptr;
		}
		if (LOD->TypeDataModule)
		{
			GUObjectArray.DestroyObject(LOD->TypeDataModule);
			LOD->TypeDataModule = nullptr;
		}

		GUObjectArray.DestroyObject(LOD);
	}
}

int32 UParticleSystem::GetLODCount() const
{
	return std::max(1, static_cast<int32>(LODDistances.size()));
}

int32 UParticleSystem::CreateLOD(float Distance)
{
	if (LODDistances.empty())
	{
		LODDistances.push_back(0.0f);
	}

	const int32 NewLODIndex = static_cast<int32>(LODDistances.size());
	const float DefaultDistance = LODDistances.back() + DefaultLODDistanceStep;
	LODDistances.push_back(Distance >= 0.0f ? Distance : DefaultDistance);
	NormalizeLODData();

	return NewLODIndex;
}

bool UParticleSystem::RemoveLOD(int32 LODIndex)
{
	if (LODIndex <= 0 || LODIndex >= GetLODCount())
	{
		return false;
	}

	LODDistances.erase(LODDistances.begin() + LODIndex);
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter && LODIndex < static_cast<int32>(Emitter->LODLevels.size()))
		{
			UParticleLODLevel* RemovedLOD = Emitter->LODLevels[LODIndex];
			Emitter->LODLevels.erase(Emitter->LODLevels.begin() + LODIndex);
			DestroyLODLevel(RemovedLOD);
		}
	}

	NormalizeLODData();
	return true;
}

float UParticleSystem::GetLODDistance(int32 LODIndex) const
{
	if (LODIndex < 0 || LODIndex >= static_cast<int32>(LODDistances.size()))
	{
		return 0.0f;
	}
	return LODDistances[LODIndex];
}

bool UParticleSystem::SetLODDistance(int32 LODIndex, float Distance)
{
	if (LODDistances.empty())
	{
		LODDistances.push_back(0.0f);
	}

	if (LODIndex < 0 || LODIndex >= static_cast<int32>(LODDistances.size()))
	{
		return false;
	}

	LODDistances[LODIndex] = std::max(0.0f, Distance);
	NormalizeLODData();
	return true;
}

//ParticleSystem과 Emitter가 가지는 LODLevels의 갯수를 맞춘다
void UParticleSystem::NormalizeLODData()
{
	if (LODDistances.empty())
	{
		LODDistances.push_back(0.0f);
	}

	const int32 LODCount = static_cast<int32>(LODDistances.size());
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			Emitter->SyncLODLevelsToSystemCount(LODCount);
		}
	}
}
