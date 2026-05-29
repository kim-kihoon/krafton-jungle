#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "Particle/ParticleHelper.h"
#include "Profiling/Stats.h"

#define NOMINMAX
#include <Windows.h>

class UParticleSystemComponent;

enum class EParticleStatTimer : uint8
{
	ComponentTick,
	InitParticles,
	BuildInstances,
	EmitterTick,
	KillParticles,
	ResetParticleParameters,
	UpdateModules,
	Spawn,
	OnSpawnModules,
	TypeDataSpawn,
	PostUpdateModules,
	ParticleUpdate,
	FinalUpdateModules,
	BuildRenderData,
	UpdateDynamicData,
	UpdateMesh,
	SortEmitters,
	PackSprites,
	PackMeshes,
	PackBeams,
	PackRibbons,
	RebuildSections,
	UploadSpriteBuffers,
	UploadMeshInstances,
	UploadBeamBuffers,
	UploadRibbonBuffers,
	Count
};

struct FParticleTimerSnapshot
{
	double LastSeconds = 0.0;
	double AvgSeconds = 0.0;
	uint32 CallCount = 0;
};

struct FParticleTypeStats
{
	uint32 EmitterCount = 0;
	uint32 ActiveParticleCount = 0;
	uint64 SimMemoryBytes = 0;
	uint64 PackedBytes = 0;
	uint32 PackedPrimitiveCount = 0;
};

struct FParticleStatsSnapshot
{
	uint32 ComponentCount = 0;
	uint32 EmitterCount = 0;
	uint32 ActiveParticleCount = 0;
	uint32 MaxParticleCount = 0;
	uint64 SimMemoryBytes = 0;
	uint64 PackedBytes = 0;
	FParticleTypeStats TypeStats[static_cast<int32>(DET_Ribbon) + 1];
	FParticleTimerSnapshot Timers[static_cast<int32>(EParticleStatTimer::Count)];
};

class FParticleStats : public TSingleton<FParticleStats>
{
	friend class TSingleton<FParticleStats>;

public:
	void RecordTime(EParticleStatTimer Timer, double ElapsedSeconds);
	void RecordComponent(const UParticleSystemComponent& Component);
	void RecordPacking(EDynamicEmitterType Type, uint32 PrimitiveCount, uint64 Bytes);
	void TakeSnapshot();

	const FParticleStatsSnapshot& GetSnapshot() const { return Snapshot; }
	LARGE_INTEGER GetFrequency() const { return Frequency; }
	static const char* GetTimerName(EParticleStatTimer Timer);
	static const char* GetTypeName(EDynamicEmitterType Type);

private:
	FParticleStats();

	FParticleStatsSnapshot Current;
	FParticleStatsSnapshot Snapshot;
	double TimerWindows[static_cast<int32>(EParticleStatTimer::Count)][STAT_WINDOW_SIZE] = {};
	uint32 TimerWindowHeads[static_cast<int32>(EParticleStatTimer::Count)] = {};
	uint32 TimerWindowCounts[static_cast<int32>(EParticleStatTimer::Count)] = {};
	LARGE_INTEGER Frequency;
};

class FScopedParticleStatTimer
{
public:
	explicit FScopedParticleStatTimer(EParticleStatTimer InTimer)
		: Timer(InTimer)
	{
		QueryPerformanceCounter(&StartTime);
	}

	~FScopedParticleStatTimer()
	{
		LARGE_INTEGER EndTime;
		QueryPerformanceCounter(&EndTime);
		const double Elapsed = static_cast<double>(EndTime.QuadPart - StartTime.QuadPart)
			/ static_cast<double>(FParticleStats::Get().GetFrequency().QuadPart);
		FParticleStats::Get().RecordTime(Timer, Elapsed);
	}

private:
	EParticleStatTimer Timer;
	LARGE_INTEGER StartTime;
};

#if STATS
#define PARTICLE_SCOPE_STAT(Timer) FScopedParticleStatTimer SCOPE_STAT_CONCAT(_ParticleScopedTimer_, __COUNTER__)(Timer)
#else
#define PARTICLE_SCOPE_STAT(Timer) ((void)0)
#endif
