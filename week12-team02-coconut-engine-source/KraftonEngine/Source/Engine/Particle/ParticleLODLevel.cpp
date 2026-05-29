#include "Particle/ParticleLODLevel.h"

#include "Particle/ParticleModule.h"

#include <algorithm>
#include <cmath>

void UParticleLODLevel::ClassifyModulesByRole()
{
	SpawningModules.clear();
	OnSpawnModules.clear();
	UpdateModules.clear();
	FinalUpdateModules.clear();
	TypeDataModule = nullptr;
	EventGenerator = nullptr;
	EventReceiverModules.clear();

	if (SpawnModule && SpawnModule->bEnabled)
	{
		SpawningModules.push_back(SpawnModule);
	}

	for (UParticleModule* Module : Modules)
	{
		if (!Module || !Module->bEnabled)
		{
			continue;
		}

		if (UParticleModuleSpawnBase* SpawnBase = Cast<UParticleModuleSpawnBase>(Module))
		{
			SpawningModules.push_back(SpawnBase);
		}

		if (UParticleModuleTypeDataBase* TypeData = Cast<UParticleModuleTypeDataBase>(Module))
		{
			TypeDataModule = TypeData;
		}

		if (UParticleModuleEventGenerator* EventGen = Cast<UParticleModuleEventGenerator>(Module))
		{
			EventGenerator = EventGen;
		}

		if (UParticleModuleEventReceiverBase* EventReceiver = Cast<UParticleModuleEventReceiverBase>(Module))
		{
			EventReceiverModules.push_back(EventReceiver);
		}

		// Beam helper modules (Source/Target/Noise/etc.) are driven by their owning
		// TypeData module, not the generic spawn/update loop. Keep them out.
		if (Module->GetModuleType() == EPMT_Beam)
		{
			continue;
		}

		if (Module->IsOnSpawnModule())
		{
			OnSpawnModules.push_back(Module);
		}

		if (Module->IsUpdateModule())
		{
			UpdateModules.push_back(Module);
		}

		if (Module->bFinalUpdateModule)
		{
			FinalUpdateModules.push_back(Module);
		}
	}
}

int32 UParticleLODLevel::CalculateMaxActiveParticleCount()
{
	float ParticleLifetime = 0.0f;
	float MaxSpawnRate = SpawnModule ? SpawnModule->GetEstimatedSpawnRate() : 0.0f;
	int32 MaxBurstCount = SpawnModule ? SpawnModule->GetMaximumBurstCount() : 0;

	for (UParticleModule* Module : Modules)
	{
		if (UParticleModuleLifetimeBase* LifetimeMod = Cast<UParticleModuleLifetimeBase>(Module))
		{
			ParticleLifetime += std::max(0.0f, LifetimeMod->GetMaxLifetime());
		}

		if (UParticleModuleSpawnBase* SpawnMod = Cast<UParticleModuleSpawnBase>(Module))
		{
			MaxSpawnRate += std::max(0.0f, SpawnMod->GetEstimatedSpawnRate());
			MaxBurstCount += std::max(0, SpawnMod->GetMaximumBurstCount());
		}
	}

	if (SpawnModule)
	{
		for (const FParticleBurst& Burst : SpawnModule->BurstList)
		{
			MaxBurstCount += std::max(Burst.Count, Burst.CountLow);
		}
	}

	const float MaxDuration = RequiredModule ? std::max(0.0f, RequiredModule->EmitterDuration) : 0.0f;
	const int32 TotalLoops = RequiredModule ? RequiredModule->EmitterLoops : 0;
	const float TotalDuration = MaxDuration * static_cast<float>(TotalLoops);
	int32 MaxAPC = 0;

	if (TotalDuration != 0.0f)
	{
		if (TotalLoops == 1)
		{
			MaxAPC += static_cast<int32>(std::ceil(std::min(ParticleLifetime, MaxDuration) * MaxSpawnRate));
			MaxAPC += 1;
			MaxAPC += MaxBurstCount;
		}
		else
		{
			if (ParticleLifetime < MaxDuration)
			{
				MaxAPC += static_cast<int32>(std::ceil(ParticleLifetime * MaxSpawnRate));
			}
			else
			{
				MaxAPC += static_cast<int32>(std::ceil(MaxDuration * MaxSpawnRate) * std::ceil(ParticleLifetime));
			}
			MaxAPC += 1;
			MaxAPC += MaxBurstCount;
			if (ParticleLifetime > MaxDuration)
			{
				MaxAPC += MaxBurstCount * static_cast<int32>(std::ceil(ParticleLifetime - MaxDuration));
			}
		}
	}
	else
	{
		if (ParticleLifetime > 0.0f)
		{
			MaxAPC += static_cast<int32>(std::ceil(ParticleLifetime * MaxSpawnRate));
		}
		else
		{
			MaxAPC += static_cast<int32>(std::ceil(MaxSpawnRate));
		}
		MaxAPC += std::max(static_cast<int32>(std::ceil(MaxSpawnRate * 0.032f)), 2);
		MaxAPC += MaxBurstCount;
	}

	PeakActiveParticles = std::max(0, MaxAPC);
	return PeakActiveParticles;
}

int32 UParticleLODLevel::GetModuleIndex(UParticleModule* InModule)
{
	for (int32 Index = 0; Index < static_cast<int32>(Modules.size()); ++Index)
	{
		if (Modules[Index] == InModule)
		{
			return Index;
		}
	}
	return -1;
}

UParticleModule* UParticleLODLevel::GetModuleAtIndex(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= static_cast<int32>(Modules.size()))
	{
		return nullptr;
	}
	return Modules[InIndex];
}

void UParticleLODLevel::SetLevelIndex(int32 InLevelIndex)
{
	Level = InLevelIndex;
}
