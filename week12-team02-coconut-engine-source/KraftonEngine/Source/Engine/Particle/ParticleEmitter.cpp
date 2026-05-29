#include "Particle/ParticleEmitter.h"

#include "Particle/ParticleHelper.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"

#include <algorithm>

UParticleLODLevel* UParticleEmitter::DuplicateLODLevelForEmitter(UParticleLODLevel* Source, int32 NewLevelIndex)
{
	UParticleLODLevel* NewLOD = GUObjectArray.CreateObject<UParticleLODLevel>(this);
	if (!NewLOD)
	{
		return nullptr;
	}

	NewLOD->SetLevelIndex(NewLevelIndex);
	NewLOD->bEnabled = true;
	NewLOD->ConvertedModules = Source ? Source->ConvertedModules : false;
	NewLOD->PeakActiveParticles = Source ? Source->PeakActiveParticles : 0;

	NewLOD->RequiredModule = Source && Source->RequiredModule
		? Cast<UParticleModuleRequired>(Source->RequiredModule->CloneForLOD(NewLOD))
		: nullptr;
	if (!NewLOD->RequiredModule)
	{
		NewLOD->RequiredModule = GUObjectArray.CreateObject<UParticleModuleRequired>(NewLOD);
	}

	NewLOD->SpawnModule = Source && Source->SpawnModule
		? Cast<UParticleModuleSpawn>(Source->SpawnModule->CloneForLOD(NewLOD))
		: nullptr;
	if (!NewLOD->SpawnModule)
	{
		NewLOD->SpawnModule = GUObjectArray.CreateObject<UParticleModuleSpawn>(NewLOD);
	}

	NewLOD->Modules.clear();
	NewLOD->TypeDataModule = nullptr;
	NewLOD->EventGenerator = nullptr;
	if (Source)
	{
		NewLOD->Modules.reserve(Source->Modules.size());
		for (UParticleModule* Module : Source->Modules)
		{
			if (UParticleModule* NewModule = Module ? Module->CloneForLOD(NewLOD) : nullptr)
			{
				NewLOD->Modules.push_back(NewModule);
				if (UParticleModuleTypeDataBase* TypeData = Cast<UParticleModuleTypeDataBase>(NewModule))
				{
					NewLOD->TypeDataModule = TypeData;
				}
				if (UParticleModuleEventGenerator* EventGen = Cast<UParticleModuleEventGenerator>(NewModule))
				{
					NewLOD->EventGenerator = EventGen;
				}
			}
		}
	}

	NewLOD->ClassifyModulesByRole();
	return NewLOD;
}

//기존 Asset을 보고 런타임때 참고할 Module배열을 만든다.
void UParticleEmitter::ClassifyModulesByRole()
{
	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		if (LODLevel)
		{
			LODLevel->ClassifyModulesByRole();
		}
	}
}

void UParticleEmitter::SetLODCount(int32 LODCount)
{
	if (LODCount < 0)
	{
		LODCount = 0;
	}
	LODLevels.resize(static_cast<size_t>(LODCount));
}

void UParticleEmitter::SyncLODLevelsToSystemCount(int32 LODCount)
{
	LODCount = std::max(1, LODCount);
	if (LODLevels.size() > LODCount)
	{
		LODLevels.resize(static_cast<size_t>(LODCount));
	}

	while (LODLevels.size() < LODCount)
	{
		const int32 NewLODIndex = static_cast<int32>(LODLevels.size());
		UParticleLODLevel* SourceLOD = GetBestLODLevel(NewLODIndex - 1);
		LODLevels.push_back(DuplicateLODLevelForEmitter(SourceLOD, NewLODIndex));
	}

	for (int32 Index = 0; Index < LODLevels.size(); ++Index)
	{
		if (!LODLevels[Index])
		{
			UParticleLODLevel* SourceLOD = Index > 0 ? GetBestLODLevel(Index - 1) : nullptr;
			LODLevels[Index] = DuplicateLODLevelForEmitter(SourceLOD, Index);
			continue;
		}

		LODLevels[Index]->SetLevelIndex(Index);
		LODLevels[Index]->ClassifyModulesByRole();
	}
}

UParticleLODLevel* UParticleEmitter::GetLODLevel(int32 LODLevel)
{
	if (LODLevel < 0 || LODLevel >= static_cast<int32>(LODLevels.size()))
	{
		return nullptr;
	}
	return LODLevels[LODLevel];
}

UParticleLODLevel* UParticleEmitter::GetBestLODLevel(int32 LODLevel) const
{
	if (LODLevels.empty())
	{
		return nullptr;
	}

	const int32 ClampedLOD = std::clamp(LODLevel, 0, static_cast<int32>(LODLevels.size()) - 1);
	for (int32 Index = ClampedLOD; Index >= 0; --Index)
	{
		UParticleLODLevel* LOD = LODLevels[Index];
		if (LOD && LOD->bEnabled)
		{
			return LOD;
		}
	}

	for (int32 Index = ClampedLOD + 1; Index < static_cast<int32>(LODLevels.size()); ++Index)
	{
		UParticleLODLevel* LOD = LODLevels[Index];
		if (LOD && LOD->bEnabled)
		{
			return LOD;
		}
	}

	return nullptr;
}

bool UParticleEmitter::CalculateMaxActiveParticleCount()
{
	PeakActiveParticles = 0;
	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		if (LODLevel)
		{
			PeakActiveParticles = std::max(PeakActiveParticles, LODLevel->CalculateMaxActiveParticleCount());
		}
	}
	InitialAllocationCount = PeakActiveParticles;
	return true;
}

void UParticleEmitter::CalculateTypeParticleSizeAndOffsets(UParticleModuleTypeDataBase* HighTypeData)
{
	if (HighTypeData)
	{
		const int32 ReqBytes = static_cast<int32>(HighTypeData->RequiredBytes(nullptr));
		if (ReqBytes > 0)
		{
			TypeDataOffset = ParticleSize;
			ParticleSize += ReqBytes;
		}

		const int32 TempInstanceBytes = static_cast<int32>(HighTypeData->RequiredBytesPerInstance());
		if (TempInstanceBytes > 0)
		{
			TypeDataInstanceOffset = ReqInstanceBytes;
			ReqInstanceBytes += TempInstanceBytes;
		}
	}
}

bool UParticleEmitter::CalculatePerParticleSizeAndOffsets(UParticleModuleTypeDataBase* HighTypeData, int32 ModuleIdx, UParticleModule* ParticleModule)
{
	if (!ParticleModule || ParticleModule->GetModuleType() == EPMT_TypeData)
	{
		return true;
	}

	const int32 ReqBytes = static_cast<int32>(ParticleModule->RequiredBytes(HighTypeData));
	if (ReqBytes > 0)
	{
		//ModuleIdx에 있는 Module 넣는다.
		ModuleOffsetMap.emplace(ParticleModule, static_cast<uint32>(ParticleSize));
		//TODO : LOD배열 전체 순회는 별로 좋지 않은것 같은데,,
		//다른 LOD에 같은 ModuleIdx에있는 아이들의 Offset도 넣어준다
		for (int32 LODIdx = 1; LODIdx < static_cast<int32>(LODLevels.size()); ++LODIdx)
		{
			UParticleLODLevel* CurLODLevel = LODLevels[LODIdx];
			if (!CurLODLevel)
			{
				continue;
			}

			UParticleModule* LODModule = CurLODLevel->GetModuleAtIndex(ModuleIdx);
			if (LODModule)
			{
				ModuleOffsetMap.emplace(LODModule, static_cast<uint32>(ParticleSize));
			}
		}
		ParticleSize += ReqBytes;
	}
	return false;
}

void UParticleEmitter::CalculatePerInstanceParticleSizeAndOffset(int32 ModuleIdx, UParticleModule* ParticleModule, const int32 TempInstanceBytes)
{
	if (TempInstanceBytes > 0)
	{
		ModuleInstanceOffsetMap.emplace(ParticleModule, static_cast<uint32>(ReqInstanceBytes));
		ModulesNeedingInstanceData.push_back(ParticleModule);

		for (int32 LODIdx = 1; LODIdx < static_cast<int32>(LODLevels.size()); ++LODIdx)
		{
			UParticleLODLevel* CurLODLevel = LODLevels[LODIdx];
			if (!CurLODLevel)
			{
				continue;
			}

			UParticleModule* LODModule = CurLODLevel->GetModuleAtIndex(ModuleIdx);
			if (LODModule)
			{
				ModuleInstanceOffsetMap.emplace(LODModule, static_cast<uint32>(ReqInstanceBytes));
			}
		}

		ReqInstanceBytes += TempInstanceBytes;
	}
}

//각 모듈의 per-particle payload offset과 per-instance payload offset구하는과정
void UParticleEmitter::CacheEmitterModuleInfo()
{
	ModuleOffsetMap.clear();
	ModuleInstanceOffsetMap.clear();
	ModulesNeedingInstanceData.clear();

	ParticleSize = sizeof(FBaseParticle);
	ReqInstanceBytes = 0;
	TypeDataOffset = 0;
	TypeDataInstanceOffset = -1;

	UParticleLODLevel* HighLODLevel = GetLODLevel(0);
	if (!HighLODLevel)
	{
		return;
	}

	HighLODLevel->ClassifyModulesByRole();

	UParticleModuleTypeDataBase* HighTypeData = HighLODLevel->TypeDataModule;
	CalculateTypeParticleSizeAndOffsets(HighTypeData);

	for (int32 ModuleIdx = 0; ModuleIdx < static_cast<int32>(HighLODLevel->Modules.size()); ++ModuleIdx)
	{
		UParticleModule* ParticleModule = HighLODLevel->Modules[ModuleIdx];
		if (CalculatePerParticleSizeAndOffsets(HighTypeData, ModuleIdx, ParticleModule)) continue;

		const int32 TempInstanceBytes = static_cast<int32>(ParticleModule->RequiredBytesPerInstance());
		CalculatePerInstanceParticleSizeAndOffset(ModuleIdx, ParticleModule, TempInstanceBytes);
	}
}

bool UParticleEmitter::HasAnyEnabledLODs() const
{
	for (const UParticleLODLevel* LODLevel : LODLevels)
	{
		if (LODLevel && LODLevel->bEnabled)
		{
			return true;
		}
	}
	return false;
}
