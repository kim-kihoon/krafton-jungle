#include "Particle/ParticleEmitterInstances.h"

#include "Component/ParticleSystemComponent.h"
#include "Materials/Material.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Profiling/ParticleStats.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <malloc.h>
#include <utility>

namespace
{
bool ShouldSuppressAutomaticSpawning(const UParticleLODLevel* LODLevel)
{
	if (!LODLevel)
	{
		return false;
	}

	for (UParticleModuleEventReceiverBase* Receiver : LODLevel->EventReceiverModules)
	{
		const UParticleModuleEventReceiverSpawn* SpawnReceiver = Cast<UParticleModuleEventReceiverSpawn>(Receiver);
		if (SpawnReceiver && SpawnReceiver->bEnabled && SpawnReceiver->bSpawnOnlyOnEvent)
		{
			return true;
		}
	}
	return false;
}
}

FParticleEmitterInstance::FParticleEmitterInstance(UParticleSystemComponent* InComponent)
	: Component(InComponent)
{
}

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	DataContainer.Free();
	std::free(InstanceData);
	ParticleData = nullptr;
	ParticleIndices = nullptr;
	InstanceData = nullptr;
}

void FParticleEmitterInstance::InitParameters(UParticleEmitter* InTemplate)
{
	EmitterTemplate = InTemplate;
	if (EmitterTemplate)
	{
		EmitterTemplate->ClassifyModulesByRole();
		EmitterTemplate->CacheEmitterModuleInfo();
	}

	SetCurrentLODLevel(0);
	ParticleSize = EmitterTemplate ? EmitterTemplate->ParticleSize : static_cast<int32>(sizeof(FBaseParticle));
	TypeDataOffset = EmitterTemplate ? EmitterTemplate->TypeDataOffset : 0;
	TypeDataInstanceOffset = EmitterTemplate ? EmitterTemplate->TypeDataInstanceOffset : -1;

	if (EmitterTemplate && EmitterTemplate->ReqInstanceBytes > 0)
	{
		if (!InstanceData || EmitterTemplate->ReqInstanceBytes > InstancePayloadSize)
		{
			uint8* NewInstanceData = static_cast<uint8*>(std::realloc(InstanceData, EmitterTemplate->ReqInstanceBytes));
			if (NewInstanceData)
			{
				InstanceData = NewInstanceData;
				InstancePayloadSize = EmitterTemplate->ReqInstanceBytes;
			}
			else
			{
				InstancePayloadSize = 0;
			}
		}

		if (InstanceData)
		{
			std::memset(InstanceData, 0, InstancePayloadSize);
			for (UParticleModule* ParticleModule : EmitterTemplate->ModulesNeedingInstanceData)
			{
				if (ParticleModule)
				{
					ParticleModule->PrepPerInstanceBlock(this, GetModuleInstanceData(ParticleModule));
				}
			}
		}
	}
	else if (InstanceData)
	{
		std::free(InstanceData);
		InstanceData = nullptr;
		InstancePayloadSize = 0;
	}

	PayloadOffset = ParticleSize;
	ParticleSize += static_cast<int32>(RequiredBytes());
	ParticleSize = AlignParticleDataSize(ParticleSize, 16); //why doing this?
	ParticleStride = static_cast<int32>(CalculateParticleStride(static_cast<uint32>(ParticleSize)));
	ActiveParticles = 0;
	ParticleCounter = 0;
	SpawnFraction = 0.0f;
	SecondsSinceCreation = 0.0f;
	EmitterTime = 0.0f;
	LastDeltaTime = 0.0f;
	ResetBurstList();

	const int32 InitialCount = EmitterTemplate ? std::max(EmitterTemplate->InitialAllocationCount, 0) : 0;
	Resize(InitialCount);
}

void FParticleEmitterInstance::RebuildTemplateModuleList()
{
	if (EmitterTemplate)
	{
		EmitterTemplate->ClassifyModulesByRole();
	}
}

bool FParticleEmitterInstance::Resize(int32 NewMaxActiveParticles, bool bSetMaxActiveCount)
{
	(void)bSetMaxActiveCount;

	NewMaxActiveParticles = std::max(0, NewMaxActiveParticles);
	if (NewMaxActiveParticles == MaxActiveParticles)
	{
		return true;
	}

	FParticleDataContainer OldData = std::move(DataContainer);
	const int32 NewActiveParticles = std::min(ActiveParticles, NewMaxActiveParticles);

	MaxActiveParticles = NewMaxActiveParticles;
	ActiveParticles = NewActiveParticles;

	DataContainer.Alloc(ParticleStride * MaxActiveParticles, MaxActiveParticles);
	ParticleData = DataContainer.ParticleData;
	ParticleIndices = DataContainer.ParticleIndices;

	if (MaxActiveParticles > 0 && (!ParticleData || !ParticleIndices))
	{
		MaxActiveParticles = 0;
		ActiveParticles = 0;
		return false;
	}

	for (int32 Index = 0; Index < NewActiveParticles; ++Index)
	{
		const uint16 OldDirectIndex = OldData.ParticleIndices ? OldData.ParticleIndices[Index] : static_cast<uint16>(Index);
		std::memcpy(ParticleData + ParticleStride * Index,
			OldData.ParticleData + ParticleStride * OldDirectIndex,
			ParticleSize);
		ParticleIndices[Index] = static_cast<uint16>(Index);
	}

	for (int32 Index = NewActiveParticles; Index < MaxActiveParticles; ++Index)
	{
		ParticleIndices[Index] = static_cast<uint16>(Index);
	}

	return true;
}

void FParticleEmitterInstance::SetCurrentLODLevel(int32 LODLevel)
{
	CurrentLODLevelIndex = std::max(0, LODLevel);
	CurrentLODLevel = EmitterTemplate ? EmitterTemplate->GetBestLODLevel(CurrentLODLevelIndex) : nullptr;
}

void FParticleEmitterInstance::Tick(float DeltaTime, int32 LODLevel, bool bSuppressSpawning)
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::EmitterTick);
	LastDeltaTime = DeltaTime;
	SecondsSinceCreation += DeltaTime;
	EmitterTime += DeltaTime;
	OldLocation = Location;
	Location = Component ? Component->GetWorldLocation() : FVector::ZeroVector;

	SetCurrentLODLevel(LODLevel);
	if (!CurrentLODLevel)
	{
		return;
	}

	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::KillParticles);
		KillParticles();
	}
	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::ResetParticleParameters);
		ResetParticleParameters(DeltaTime);
	}
	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::UpdateModules);
		Tick_ModuleUpdate(DeltaTime, CurrentLODLevel);
	}
	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::Spawn);
		SpawnFraction = Tick_SpawnParticles(DeltaTime, CurrentLODLevel, bSuppressSpawning, false);
	}
	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::PostUpdateModules);
		Tick_ModulePostUpdate(DeltaTime, CurrentLODLevel);
	}
	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::ParticleUpdate);
		UpdateParticles(DeltaTime);
	}
	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::FinalUpdateModules);
		Tick_ModuleFinalUpdate(DeltaTime, CurrentLODLevel);
	}
}

void FParticleEmitterInstance::ResetParticleParameters(float DeltaTime)
{
	for (int32 ActiveIndex = 0; ActiveIndex < ActiveParticles; ++ActiveIndex)
	{
		FBaseParticle* Particle = GetParticleDirect(ParticleIndices[ActiveIndex]);
		if (!Particle)
		{
			continue;
		}

		const bool bJustSpawned = (Particle->Flags & STATE_Particle_JustSpawned) != 0;
		Particle->Flags &= ~STATE_Particle_JustSpawned;

		Particle->Velocity = Particle->BaseVelocity;
		Particle->Size = Particle->BaseSize;
		Particle->RotationRate = Particle->BaseRotationRate;
		Particle->Color = Particle->BaseColor;

		if (!bJustSpawned && Particle->OneOverMaxLifetime > 0.0f)
		{
			Particle->RelativeTime += DeltaTime * Particle->OneOverMaxLifetime;
		}
	}
}

void FParticleEmitterInstance::Tick_ModuleUpdate(float DeltaTime, UParticleLODLevel* InCurrentLODLevel)
{
	if (!InCurrentLODLevel)
	{
		return;
	}

	UParticleLODLevel* HighestLODLevel = EmitterTemplate ? EmitterTemplate->GetLODLevel(0) : nullptr;

	// Update modules are processed in order, and the same module in different LOD levels shares the same instance data offset.
	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(InCurrentLODLevel->UpdateModules.size()); ++ModuleIndex)
	{
		UParticleModule* Module = InCurrentLODLevel->UpdateModules[ModuleIndex];
		if (!Module || !Module->bEnabled || !Module->bUpdateModule)
		{
			continue;
		}

		UParticleModule* OffsetModule = (HighestLODLevel && ModuleIndex < static_cast<int32>(HighestLODLevel->UpdateModules.size()))
			? HighestLODLevel->UpdateModules[ModuleIndex]
			: Module;
		UParticleModule::FUpdateContext Context(*this, static_cast<int32>(GetModuleDataOffset(OffsetModule)), DeltaTime);
		Module->Update(Context);
	}
}

void FParticleEmitterInstance::Tick_ModulePostUpdate(float DeltaTime, UParticleLODLevel* InCurrentLODLevel)
{
	if (InCurrentLODLevel && InCurrentLODLevel->TypeDataModule)
	{
		UParticleModule::FUpdateContext Context(*this, TypeDataOffset, DeltaTime);
		InCurrentLODLevel->TypeDataModule->Update(Context);
	}
}

void FParticleEmitterInstance::Tick_ModuleFinalUpdate(float DeltaTime, UParticleLODLevel* InCurrentLODLevel)
{
	if (!InCurrentLODLevel)
	{
		return;
	}

	UParticleLODLevel* HighestLODLevel = EmitterTemplate ? EmitterTemplate->GetLODLevel(0) : nullptr;
	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(InCurrentLODLevel->FinalUpdateModules.size()); ++ModuleIndex)
	{
		UParticleModule* Module = InCurrentLODLevel->FinalUpdateModules[ModuleIndex];
		if (!Module || !Module->bEnabled || !Module->bFinalUpdateModule)
		{
			continue;
		}

		UParticleModule* OffsetModule = (HighestLODLevel && ModuleIndex < static_cast<int32>(HighestLODLevel->FinalUpdateModules.size()))
			? HighestLODLevel->FinalUpdateModules[ModuleIndex]
			: Module;
		UParticleModule::FUpdateContext Context(*this, static_cast<int32>(GetModuleDataOffset(OffsetModule)), DeltaTime);
		Module->FinalUpdate(Context);
	}

	if (InCurrentLODLevel->TypeDataModule && InCurrentLODLevel->TypeDataModule->bEnabled
		&& InCurrentLODLevel->TypeDataModule->bFinalUpdateModule)
	{
		UParticleModule::FUpdateContext Context(*this, TypeDataOffset, DeltaTime);
		InCurrentLODLevel->TypeDataModule->FinalUpdate(Context);
	}
}

float FParticleEmitterInstance::Tick_SpawnParticles(float DeltaTime, UParticleLODLevel* InCurrentLODLevel,
	bool bSuppressSpawning, bool bFirstTime)
{
	(void)InCurrentLODLevel;
	(void)bFirstTime;

	if (bSuppressSpawning || ShouldSuppressAutomaticSpawning(InCurrentLODLevel))
	{
		return SpawnFraction;
	}

	return Spawn(DeltaTime);
}

float FParticleEmitterInstance::Spawn(float DeltaTime)
{
	if (!CurrentLODLevel)
	{
		return SpawnFraction;
	}

	float SpawnRate = 0.0f;
	int32 SpawnCount = 0;
	int32 BurstCount = 0;
	const float OldLeftover = SpawnFraction;
	bool bProcessSpawnRate = true;
	bool bProcessBurstList = true;
	UParticleLODLevel* HighestLODLevel = EmitterTemplate ? EmitterTemplate->GetLODLevel(0) : nullptr;

	// Spawning modules are processed in order, and the same module in different LOD levels shares the same instance data offset.
	for (int32 SpawnModIndex = 0; SpawnModIndex < static_cast<int32>(CurrentLODLevel->SpawningModules.size()); ++SpawnModIndex)
	{
		UParticleModuleSpawnBase* SpawnModule = CurrentLODLevel->SpawningModules[SpawnModIndex];
		if (!SpawnModule || !SpawnModule->bEnabled)
		{
			continue;
		}

		UParticleModule* OffsetModule = (HighestLODLevel && SpawnModIndex < static_cast<int32>(HighestLODLevel->SpawningModules.size()))
			? HighestLODLevel->SpawningModules[SpawnModIndex]
			: SpawnModule;
		const uint32 Offset = GetModuleDataOffset(OffsetModule);

		int32 Number = 0;
		float Rate = 0.0f;
		if (!SpawnModule->GetSpawnAmount({ *this }, static_cast<int32>(Offset), OldLeftover, DeltaTime, Number, Rate))
		{
			bProcessSpawnRate = false;
		}

		SpawnCount += std::max(0, Number);
		SpawnRate += std::max(0.0f, Rate);

		int32 BurstNumber = 0;
		if (!SpawnModule->GetBurstCount(this, static_cast<int32>(Offset), OldLeftover, DeltaTime, BurstNumber))
		{
			bProcessBurstList = false;
		}
		BurstCount += std::max(0, BurstNumber);
	}

	(void)bProcessSpawnRate;
	if (bProcessBurstList)
	{
		BurstCount += GetCurrentBurstCount(DeltaTime);
	}

	if (SpawnRate <= 0.0f && SpawnCount <= 0 && BurstCount <= 0)
	{
		return SpawnFraction;
	}

	float NewLeftover = OldLeftover + std::max(0.0f, DeltaTime) * SpawnRate;
	int32 Number = static_cast<int32>(std::floor(NewLeftover));
	const float Increment = SpawnRate > 0.0f ? 1.0f / SpawnRate : 0.0f;
	const float StartTime = DeltaTime + OldLeftover * Increment - Increment;
	NewLeftover = NewLeftover - static_cast<float>(Number);
	Number += SpawnCount;

	if (Number > 0 || BurstCount > 0)
	{
		FParticleEventInstancePayload* EventPayload = nullptr;
		if (CurrentLODLevel && CurrentLODLevel->EventGenerator)
		{
			EventPayload = reinterpret_cast<FParticleEventInstancePayload*>(
				GetModuleInstanceData(CurrentLODLevel->EventGenerator));
		}

		// If there is a spawn rate, spawn those particles evenly throughout the tick. 
		SpawnParticles(Number, StartTime, Increment, Location, FVector::ZeroVector, EventPayload);

		// If there are also bursts, the spawn rate-based spawns will come before them.
		SpawnParticles(BurstCount, 0.0f, BurstCount > 0 ? DeltaTime / static_cast<float>(BurstCount) : 0.0f,
			Location, FVector::ZeroVector, EventPayload);
	}

	return NewLeftover;
}

void FParticleEmitterInstance::ResetBurstList()
{
	const UParticleLODLevel* LODLevel = CurrentLODLevel;
	const int32 BurstCount = (LODLevel && LODLevel->SpawnModule)
		? static_cast<int32>(LODLevel->SpawnModule->BurstList.size())
		: 0;
	BurstFired.assign(static_cast<size_t>(std::max(0, BurstCount)), 0);
}

int32 FParticleEmitterInstance::GetCurrentBurstCount(float DeltaTime)
{
	if (!CurrentLODLevel || !CurrentLODLevel->SpawnModule)
	{
		return 0;
	}

	const TArray<FParticleBurst>& BurstList = CurrentLODLevel->SpawnModule->BurstList;
	if (BurstFired.size() < BurstList.size())
	{
		BurstFired.resize(BurstList.size(), 0);
	}

	const float PreviousEmitterTime = std::max(0.0f, EmitterTime - std::max(0.0f, DeltaTime));
	int32 BurstCount = 0;
	for (int32 BurstIndex = 0; BurstIndex < static_cast<int32>(BurstList.size()); ++BurstIndex)
	{
		if (BurstFired[BurstIndex] != 0)
		{
			continue;
		}

		const FParticleBurst& Burst = BurstList[BurstIndex];
		const float BurstTime = std::max(0.0f, Burst.Time);
		const bool bFireThisTick = (BurstTime > PreviousEmitterTime && BurstTime <= EmitterTime)
			|| (PreviousEmitterTime == 0.0f && BurstTime == 0.0f && EmitterTime >= 0.0f);
		if (!bFireThisTick)
		{
			continue;
		}

		BurstFired[BurstIndex] = 1;
		BurstCount += std::max(0, Burst.Count);
	}

	return BurstCount;
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation,
	const FVector& InitialVelocity, FParticleEventInstancePayload* EventPayload)
{
	if (Count <= 0)
	{
		return;
	}

	if (ActiveParticles + Count > MaxActiveParticles)
	{
		Resize(std::max(ActiveParticles + Count, std::max(1, MaxActiveParticles * 2)));
	}
	if (!ParticleData || !ParticleIndices)
	{
		return;
	}

	float SpawnTime = StartTime;
	if (CurrentLODLevel && CurrentLODLevel->EventGenerator && EventPayload)
	{
		CurrentLODLevel->EventGenerator->HandleParticleBurst(this, EventPayload, Count);
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 DirectIndex = ParticleIndices ? ParticleIndices[ActiveParticles] : ActiveParticles;
		DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * DirectIndex);
		std::memset(&Particle, 0, ParticleSize);

		PreSpawn(&Particle, InitialLocation, InitialVelocity);

		if (CurrentLODLevel)
		{
			UParticleLODLevel* HighestLODLevel = EmitterTemplate ? EmitterTemplate->GetLODLevel(0) : nullptr;
			{
				PARTICLE_SCOPE_STAT(EParticleStatTimer::OnSpawnModules);
				for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(CurrentLODLevel->OnSpawnModules.size()); ++ModuleIndex)
				{
					UParticleModule* Module = CurrentLODLevel->OnSpawnModules[ModuleIndex];
					if (!Module)
					{
						continue;
					}

					UParticleModule* OffsetModule = (HighestLODLevel && ModuleIndex < static_cast<int32>(HighestLODLevel->OnSpawnModules.size()))
						? HighestLODLevel->OnSpawnModules[ModuleIndex]
						: Module;
					UParticleModule::FSpawnContext Context(*this, static_cast<int32>(GetModuleDataOffset(OffsetModule)), SpawnTime, &Particle);
					Module->Spawn(Context);
				}
			}

			if (CurrentLODLevel->TypeDataModule)
			{
				PARTICLE_SCOPE_STAT(EParticleStatTimer::TypeDataSpawn);
				UParticleModule::FSpawnContext Context(*this, TypeDataOffset, SpawnTime, &Particle);
				CurrentLODLevel->TypeDataModule->Spawn(Context);
			}
		}

		PostSpawn(&Particle, 0.0f, SpawnTime);

		if (CurrentLODLevel && CurrentLODLevel->EventGenerator && EventPayload)
		{
			CurrentLODLevel->EventGenerator->HandleParticleSpawned(this, EventPayload, &Particle);
		}

		++ActiveParticles;
		SpawnTime += Increment;
	}
}

void FParticleEmitterInstance::KillParticle(int32 Index)
{
	if (Index < 0 || Index >= ActiveParticles)
	{
		return;
	}

	const int32 LastActiveIndex = ActiveParticles - 1;
	const uint16 RemovedDirectIndex = ParticleIndices[Index];
	FBaseParticle* RemovedParticle = GetParticleDirect(RemovedDirectIndex);
	if (RemovedParticle && CurrentLODLevel && CurrentLODLevel->EventGenerator)
	{
		FParticleEventInstancePayload* EventPayload = reinterpret_cast<FParticleEventInstancePayload*>(
			GetModuleInstanceData(CurrentLODLevel->EventGenerator));
		CurrentLODLevel->EventGenerator->HandleParticleKilled(this, EventPayload, RemovedParticle);
	}

	if (Index != LastActiveIndex)
	{
		ParticleIndices[Index] = ParticleIndices[LastActiveIndex];
	}
	ParticleIndices[LastActiveIndex] = RemovedDirectIndex;

	--ActiveParticles;
}

void FParticleEmitterInstance::KillParticles()
{
	for (int32 ActiveIndex = ActiveParticles - 1; ActiveIndex >= 0; --ActiveIndex)
	{
		FBaseParticle* Particle = GetParticleDirect(ParticleIndices[ActiveIndex]);
		if (Particle && Particle->RelativeTime >= 1.0f)
		{
			KillParticle(ActiveIndex);
		}
	}
}

void FParticleEmitterInstance::UpdateParticles(float DeltaTime)
{
	for (int32 ActiveIndex = 0; ActiveIndex < ActiveParticles; ++ActiveIndex)
	{
		FBaseParticle* Particle = GetParticleDirect(ParticleIndices[ActiveIndex]);
		if (!Particle)
		{
			continue;
		}

		if ((Particle->Flags & STATE_Particle_JustSpawned) != 0)
		{
			continue;
		}

		Particle->OldLocation = Particle->Location;
		if ((Particle->Flags & STATE_Particle_FreezeTranslation) == 0)
		{
			Particle->Location = Particle->Location + Particle->Velocity * DeltaTime;
		}
		if ((Particle->Flags & STATE_Particle_FreezeRotation) == 0)
		{
			Particle->Rotation = Particle->Rotation + Particle->RotationRate * DeltaTime;
		}
	}
}

FDynamicEmitterReplayDataBase* FParticleEmitterInstance::GetReplayData()
{
	if (ActiveParticles <= 0)
	{
		return nullptr;
	}

	FDynamicEmitterReplayDataBase* NewEmitterReplayData = nullptr;
	if (CurrentLODLevel && CurrentLODLevel->TypeDataModule && CurrentLODLevel->TypeDataModule->IsAMeshEmitter())
	{
		NewEmitterReplayData = new FDynamicMeshEmitterReplayData();
	}
	else
	{
		NewEmitterReplayData = new FDynamicSpriteEmitterReplayData();
	}

	if (!FillReplayData(*NewEmitterReplayData))
	{
		delete NewEmitterReplayData;
		return nullptr;
	}

	return NewEmitterReplayData;
}

bool FParticleEmitterInstance::FillReplayData(FDynamicEmitterReplayDataBase& OutData)
{
	if (ActiveParticles <= 0 || !ParticleData || !ParticleIndices || ParticleStride <= 0)
	{
		return false;
	}

	OutData.ActiveParticleCount = ActiveParticles;
	OutData.ParticleStride = ParticleStride;
	OutData.Scale = FVector::OneVector;
	OutData.SortMode = EParticleSortMode::PSORTMODE_None;

	if (CurrentLODLevel && CurrentLODLevel->RequiredModule)
	{
		OutData.SortMode = CurrentLODLevel->RequiredModule->SortMode;
		if (FDynamicRenderableEmitterReplayDataBase* RenderableData = dynamic_cast<FDynamicRenderableEmitterReplayDataBase*>(&OutData))
		{
			RenderableData->MaterialInterface = CurrentLODLevel->RequiredModule->Material;
			if (UMaterial* Material = CurrentLODLevel->RequiredModule->Material
				? CurrentLODLevel->RequiredModule->Material->GetMaterial()
				: nullptr)
			{
				RenderableData->BlendMode = Material->GetBlendState();
			}
		}
		if (FDynamicSpriteEmitterReplayData* SpriteData = dynamic_cast<FDynamicSpriteEmitterReplayData*>(&OutData))
		{
			SpriteData->ScreenAlignment = static_cast<uint8>(CurrentLODLevel->RequiredModule->ScreenAlignment);
			SpriteData->EmitterOrigin = Location + CurrentLODLevel->RequiredModule->EmitterOrigin;
			SpriteData->SubImages_Horizontal = std::max(1, CurrentLODLevel->RequiredModule->SubImages_Horizontal);
			SpriteData->SubImages_Vertical = std::max(1, CurrentLODLevel->RequiredModule->SubImages_Vertical);
			SpriteData->AlphaSource = static_cast<uint32>(std::clamp(CurrentLODLevel->RequiredModule->AlphaSource, 0, 1));
			SpriteData->AlphaThreshold = std::clamp(CurrentLODLevel->RequiredModule->AlphaThreshold, 0.0f, 1.0f);
			SpriteData->AlphaPower = std::max(0.001f, CurrentLODLevel->RequiredModule->AlphaPower);
			SpriteData->ColorIntensity = std::max(0.0f, CurrentLODLevel->RequiredModule->ColorIntensity);
		}
	}

	if (CurrentLODLevel && CurrentLODLevel->TypeDataModule && CurrentLODLevel->TypeDataModule->IsAMeshEmitter())
	{
		OutData.eEmitterType = DET_Mesh;
		if (FDynamicMeshEmitterReplayData* MeshData = dynamic_cast<FDynamicMeshEmitterReplayData*>(&OutData))
		{
			if (UParticleModuleTypeDataMesh* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(CurrentLODLevel->TypeDataModule))
			{
				MeshData->StaticMesh = MeshTypeData->Mesh;
			}
		}
	}
	else
	{
		OutData.eEmitterType = DET_Sprite;
	}

	OutData.DataContainer.Alloc(ParticleStride * ActiveParticles, ActiveParticles);
	if (!OutData.DataContainer.ParticleData || !OutData.DataContainer.ParticleIndices)
	{
		return false;
	}

	for (int32 Index = 0; Index < ActiveParticles; ++Index)
	{
		const uint16 DirectIndex = ParticleIndices[Index];
		std::memcpy(OutData.DataContainer.ParticleData + ParticleStride * Index,
			ParticleData + ParticleStride * DirectIndex,
			ParticleSize);
		OutData.DataContainer.ParticleIndices[Index] = static_cast<uint16>(Index);
	}

	return true;
}

uint32 FParticleEmitterInstance::RequiredBytes()
{
	return 0;
}

uint32 FParticleEmitterInstance::GetModuleDataOffset(UParticleModule* Module)
{
	if (!EmitterTemplate || !Module)
	{
		return 0;   
	}

	const auto Offset = EmitterTemplate->ModuleOffsetMap.find(Module);
	return Offset != EmitterTemplate->ModuleOffsetMap.end() ? Offset->second : 0;
}

uint8* FParticleEmitterInstance::GetModuleInstanceData(UParticleModule* Module)
{
	if (!EmitterTemplate || !InstanceData || !Module)
	{
		return nullptr;
	}

	const auto Offset = EmitterTemplate->ModuleInstanceOffsetMap.find(Module);
	if (Offset == EmitterTemplate->ModuleInstanceOffsetMap.end() || Offset->second >= static_cast<uint32>(InstancePayloadSize))
	{
		return nullptr;
	}

	return InstanceData + Offset->second;
}

uint8* FParticleEmitterInstance::GetTypeDataModuleInstanceData()
{
	if (!InstanceData || TypeDataInstanceOffset < 0 || TypeDataInstanceOffset >= InstancePayloadSize)
	{
		return nullptr;
	}

	return InstanceData + TypeDataInstanceOffset;
}

uint32 FParticleEmitterInstance::CalculateParticleStride(uint32 InParticleSize)
{
	return InParticleSize;
}

FBaseParticle* FParticleEmitterInstance::GetParticleDirect(int32 DirectIndex) const
{
	if (!ParticleData || DirectIndex < 0 || DirectIndex >= MaxActiveParticles)
	{
		return nullptr;
	}
	return reinterpret_cast<FBaseParticle*>(ParticleData + ParticleStride * DirectIndex);
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	if (!Particle)
	{
		return;
	}

	Particle->OldLocation = InitialLocation;
	Particle->Location = InitialLocation;
	Particle->BaseVelocity = InitialVelocity;
	Particle->Velocity = InitialVelocity;
	Particle->Rotation = FVector::ZeroVector;
	Particle->BaseRotationRate = FVector::ZeroVector;
	Particle->RotationRate = FVector::ZeroVector;
	Particle->BaseSize = FVector::OneVector;
	Particle->Size = FVector::OneVector;
	Particle->Color = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	Particle->BaseColor = Particle->Color;
	Particle->RelativeTime = 0.0f;
	Particle->OneOverMaxLifetime = 1.0f;
	Particle->ParticleId = 0;
	Particle->Flags = 0;
}

void FParticleEmitterInstance::PostSpawn(FBaseParticle* Particle, float Interp, float SpawnTime)
{
	if (!Particle)
	{
		return;
	}

	if (CurrentLODLevel && CurrentLODLevel->RequiredModule && !CurrentLODLevel->RequiredModule->bUseLocalSpace)
	{
		const FVector EmitterMove = OldLocation - Location;
		if (EmitterMove.Dot(EmitterMove) > 1.0f)
		{
			Particle->Location = Particle->Location + EmitterMove * Interp;
		}
	}

	Particle->OldLocation = Particle->Location;
	Particle->Location = Particle->Location + Particle->Velocity * SpawnTime;

	Particle->Flags |= ((ParticleCounter++) & STATE_CounterMask);
	Particle->ParticleId = ParticleCounter;
	Particle->Flags |= STATE_Particle_JustSpawned;
}

void FParticleEmitterInstance::AddCollisionEvent(const FBaseParticle& Particle, uint16 DirectIndex, const FVector& HitLocation,
	const FVector& HitNormal, float HitTime, bool bParticleWasKilled)
{
	if (!Component)
	{
		return;
	}

	FParticleEventCollideData EventData;
	EventData.EmitterIndex = EmitterIndex;
	EventData.ParticleIndex = DirectIndex;
	EventData.ParticleDirectIndex = DirectIndex;
	EventData.ParticleId = Particle.ParticleId;
	EventData.Location = HitLocation;
	EventData.OldLocation = Particle.OldLocation;
	EventData.Velocity = Particle.Velocity;
	EventData.Direction = Particle.Velocity; 
	EventData.Direction.Normalize();
	EventData.Normal = HitNormal;
	EventData.EmitterTime = EmitterTime;
	EventData.ParticleRelativeTime = Particle.RelativeTime;
	EventData.ParticleTime = Particle.RelativeTime;
	EventData.HitTime = HitTime;
	EventData.bParticleWasKilled = bParticleWasKilled;

	bool bEventGenerated = false;
	if (CurrentLODLevel && CurrentLODLevel->EventGenerator)
	{
		FParticleEventInstancePayload* EventPayload = reinterpret_cast<FParticleEventInstancePayload*>(
			GetModuleInstanceData(CurrentLODLevel->EventGenerator));
		bEventGenerated = CurrentLODLevel->EventGenerator->HandleParticleCollision(this, EventPayload,
			const_cast<FBaseParticle*>(&Particle), HitLocation, HitNormal, HitTime);
	}

	if (!bEventGenerated)
	{
		Component->QueueParticleCollisionEvent(EventData);
	}
}
