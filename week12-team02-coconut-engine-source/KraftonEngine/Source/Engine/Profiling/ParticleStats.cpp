#include "Profiling/ParticleStats.h"

#include "Component/ParticleSystemComponent.h"
#include "Particle/ParticleEmitterInstances.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

namespace
{
constexpr int32 ParticleTypeSlotCount = static_cast<int32>(DET_Ribbon) + 1;
constexpr int32 ParticleTimerSlotCount = static_cast<int32>(EParticleStatTimer::Count);

int32 ToTypeIndex(EDynamicEmitterType Type)
{
	const int32 Index = static_cast<int32>(Type);
	return std::clamp(Index, 0, ParticleTypeSlotCount - 1);
}

int32 ToTimerIndex(EParticleStatTimer Timer)
{
	const int32 Index = static_cast<int32>(Timer);
	return std::clamp(Index, 0, ParticleTimerSlotCount - 1);
}

EDynamicEmitterType GetEmitterType(const FParticleEmitterInstance& Instance)
{
	const UParticleLODLevel* LOD = Instance.CurrentLODLevel;
	const UParticleModuleTypeDataBase* TypeData = LOD ? LOD->TypeDataModule : nullptr;
	if (!TypeData)
	{
		return DET_Sprite;
	}
	if (TypeData->IsAMeshEmitter())
	{
		return DET_Mesh;
	}
	if (TypeData->IsABeamEmitter())
	{
		return DET_Beam2;
	}
	if (TypeData->IsARibbonEmitter())
	{
		return DET_Ribbon;
	}
	return DET_Sprite;
}

uint64 EstimateEmitterMemoryBytes(const FParticleEmitterInstance& Instance)
{
	uint64 Bytes = 0;
	Bytes += static_cast<uint64>(std::max(0, Instance.DataContainer.MemBlockSize));
	Bytes += static_cast<uint64>(std::max(0, Instance.InstancePayloadSize));
	Bytes += static_cast<uint64>(Instance.BurstFired.capacity() * sizeof(uint8));
	return Bytes;
}
}

FParticleStats::FParticleStats()
{
	QueryPerformanceFrequency(&Frequency);
}

void FParticleStats::RecordTime(EParticleStatTimer Timer, double ElapsedSeconds)
{
#if STATS
	const int32 Index = ToTimerIndex(Timer);
	FParticleTimerSnapshot& TimerStats = Current.Timers[Index];
	TimerStats.LastSeconds += ElapsedSeconds;
	TimerStats.CallCount++;
#else
	(void)Timer;
	(void)ElapsedSeconds;
#endif
}

void FParticleStats::RecordComponent(const UParticleSystemComponent& Component)
{
#if STATS
	Current.ComponentCount++;
	Current.SimMemoryBytes += static_cast<uint64>(Component.EmitterInstances.capacity() * sizeof(FParticleEmitterInstance*));
	Current.SimMemoryBytes += static_cast<uint64>(Component.LODDistances.capacity() * sizeof(float));
	Current.SimMemoryBytes += static_cast<uint64>(Component.CollisionEvents.capacity() * sizeof(FParticleEventCollideData));

	for (const FParticleEmitterInstance* Instance : Component.EmitterInstances)
	{
		if (!Instance)
		{
			continue;
		}

		const EDynamicEmitterType Type = GetEmitterType(*Instance);
		const int32 TypeIndex = ToTypeIndex(Type);
		const uint64 MemoryBytes = EstimateEmitterMemoryBytes(*Instance);

		Current.EmitterCount++;
		Current.ActiveParticleCount += static_cast<uint32>(std::max(0, Instance->ActiveParticles));
		Current.MaxParticleCount += static_cast<uint32>(std::max(0, Instance->MaxActiveParticles));
		Current.SimMemoryBytes += MemoryBytes;

		FParticleTypeStats& TypeStats = Current.TypeStats[TypeIndex];
		TypeStats.EmitterCount++;
		TypeStats.ActiveParticleCount += static_cast<uint32>(std::max(0, Instance->ActiveParticles));
		TypeStats.SimMemoryBytes += MemoryBytes;
	}
#else
	(void)Component;
#endif
}

void FParticleStats::RecordPacking(EDynamicEmitterType Type, uint32 PrimitiveCount, uint64 Bytes)
{
#if STATS
	const int32 TypeIndex = ToTypeIndex(Type);
	Current.PackedBytes += Bytes;
	FParticleTypeStats& TypeStats = Current.TypeStats[TypeIndex];
	TypeStats.PackedPrimitiveCount += PrimitiveCount;
	TypeStats.PackedBytes += Bytes;
#else
	(void)Type;
	(void)PrimitiveCount;
	(void)Bytes;
#endif
}

void FParticleStats::TakeSnapshot()
{
#if STATS
	for (int32 TimerIndex = 0; TimerIndex < ParticleTimerSlotCount; ++TimerIndex)
	{
		const double FrameTime = Current.Timers[TimerIndex].LastSeconds;
		double* Window = TimerWindows[TimerIndex];
		uint32& WindowHead = TimerWindowHeads[TimerIndex];
		uint32& WindowCount = TimerWindowCounts[TimerIndex];

		Window[WindowHead] = FrameTime;
		WindowHead = (WindowHead + 1) % STAT_WINDOW_SIZE;
		if (WindowCount < STAT_WINDOW_SIZE)
		{
			WindowCount++;
		}

		double Sum = 0.0;
		for (uint32 i = 0; i < WindowCount; ++i)
		{
			Sum += Window[i];
		}
		Current.Timers[TimerIndex].AvgSeconds = WindowCount > 0 ? Sum / WindowCount : 0.0;
	}

	Snapshot = Current;
	Current = FParticleStatsSnapshot();
#endif
}

const char* FParticleStats::GetTimerName(EParticleStatTimer Timer)
{
	switch (Timer)
	{
	case EParticleStatTimer::ComponentTick: return "Component Tick";
	case EParticleStatTimer::InitParticles: return "Init Particles";
	case EParticleStatTimer::BuildInstances: return "Build Instances";
	case EParticleStatTimer::EmitterTick: return "Emitter Tick";
	case EParticleStatTimer::KillParticles: return "Kill Particles";
	case EParticleStatTimer::ResetParticleParameters: return "Reset Params";
	case EParticleStatTimer::UpdateModules: return "Update Modules";
	case EParticleStatTimer::Spawn: return "Spawn";
	case EParticleStatTimer::OnSpawnModules: return "OnSpawn Modules";
	case EParticleStatTimer::TypeDataSpawn: return "TypeData Spawn";
	case EParticleStatTimer::PostUpdateModules: return "PostUpdate Modules";
	case EParticleStatTimer::ParticleUpdate: return "Particle Update";
	case EParticleStatTimer::FinalUpdateModules: return "FinalUpdate Modules";
	case EParticleStatTimer::BuildRenderData: return "Build RenderData";
	case EParticleStatTimer::UpdateDynamicData: return "Update DynamicData";
	case EParticleStatTimer::UpdateMesh: return "Update Mesh";
	case EParticleStatTimer::SortEmitters: return "Sort Emitters";
	case EParticleStatTimer::PackSprites: return "Pack Sprite";
	case EParticleStatTimer::PackMeshes: return "Pack Mesh";
	case EParticleStatTimer::PackBeams: return "Pack Beam";
	case EParticleStatTimer::PackRibbons: return "Pack Ribbon";
	case EParticleStatTimer::RebuildSections: return "Rebuild Sections";
	case EParticleStatTimer::UploadSpriteBuffers: return "Upload Sprite";
	case EParticleStatTimer::UploadMeshInstances: return "Upload Mesh";
	case EParticleStatTimer::UploadBeamBuffers: return "Upload Beam";
	case EParticleStatTimer::UploadRibbonBuffers: return "Upload Ribbon";
	default: return "Unknown";
	}
}

const char* FParticleStats::GetTypeName(EDynamicEmitterType Type)
{
	switch (Type)
	{
	case DET_Sprite: return "Sprite";
	case DET_Mesh: return "Mesh";
	case DET_Beam2: return "Beam";
	case DET_Ribbon: return "Ribbon";
	default: return "Unknown";
	}
}
