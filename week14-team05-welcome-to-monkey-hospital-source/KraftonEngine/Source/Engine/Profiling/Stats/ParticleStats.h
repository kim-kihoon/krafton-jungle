#pragma once

#include "Core/Types/CoreTypes.h"
#include "Profiling/Stats/Stats.h"

// =============================================================================
// FParticleStats
//   파티클 런타임 stat 카운터 (ShadowStats 패턴).
//   - 개수/메모리는 gauge: WorldTick 진입부에서 Reset 후 PSC tick 중 누적.
//   - spawn/kill은 이번 프레임 이벤트 누적 (emitter 시뮬레이션에서 증가).
//   - DrawCalls는 자리만 마련 — per-section 라우팅상 실제 draw 지점 확정 후 배선.
//   표시: FOverlayStatSystem::BuildParticleLines / 토글: 콘솔 "stat particles".
// =============================================================================
#if STATS
struct FParticleStats
{
	// --- 개수 (gauge) ---
	static uint32 SpriteParticles;     // 활성 sprite 입자 수
	static uint32 MeshParticles;       // 활성 mesh 입자 수
	static uint32 TotalParticles;      // 활성 입자 합 (모든 타입)
	static uint32 PeakTotalParticles;  // 세션 피크 (Reset에서 유지)

	static uint32 EmitterCount;        // 활성 emitter instance 수
	static uint32 ComponentCount;      // 활성 PSC 수

	// --- 이벤트 (이번 프레임 누적) ---
	static uint32 ParticlesSpawned;
	static uint32 ParticlesKilled;

	// --- 드로우콜: PrepareDrawBuffer에서 빌드된 파티클 섹션 수(제출 기준)로 집계.
	//     emitter당 1섹션 = 1 DrawIndexedInstanced. 실제 RHI 실행 시점 카운트는 추후
	//     draw 실행기에서 GPU 타이밍("ParticleRender")과 함께 승격 가능. ---
	static uint32 DrawCalls;

	// --- 메모리 (bytes, gauge) ---
	static uint64 GTMemoryBytes;       // GT 파티클 버퍼 총 할당 (MemBlock 합)
	static uint64 ActiveDataBytes;     // 실사용 입자 데이터 (ActiveParticles * stride 합)
	static uint64 ReservedDataBytes;   // 예약 입자 데이터 (MaxActiveParticles * stride 합)

	// --- Ribbon RT geometry (per-frame aggregate) ---
	// Sprite/Mesh/Beam도 같은 "RT가 실제로 얼마나 geometry를 냈는가?" 관점으로 본다.
	// Sprite/Mesh는 instance-driven, Beam/Ribbon은 strip/vertex/index aggregate다.
	static uint32 SpriteRTInstances;
	static uint32 SpriteRTVertices;
	static uint32 SpriteRTIndices;
	static uint32 MeshRTInstances;
	static uint32 MeshRTVertices;
	static uint32 MeshRTIndices;
	static uint32 BeamRTStrips;
	static uint32 BeamRTVertices;
	static uint32 BeamRTIndices;

	// RibbonTrailBuilds            : ribbon trail build attempts that reached RT geometry emission
	// RibbonRuntimeCappedBuilds    : builds where requested tessellation was reduced by sample budget
	// RibbonMaxEffectiveTessellation : max post-budget tessellation actually used this frame
	// RibbonControlSegments        : total control-point segments before tessellation
	// RibbonSamplePoints           : total sampled curve points after tessellation budget was applied
	// RibbonVertices / Indices     : final emitted RT geometry totals
	static uint32 RibbonTrailBuilds;
	static uint32 RibbonRuntimeCappedBuilds;
	static uint32 RibbonMaxEffectiveTessellation;
	static uint32 RibbonControlSegments;
	static uint32 RibbonSamplePoints;
	static uint32 RibbonVertices;
	static uint32 RibbonIndices;

	static void AddTotal(uint32 Count)
	{
		TotalParticles += Count;
		if (TotalParticles > PeakTotalParticles)
		{
			PeakTotalParticles = TotalParticles;
		}
	}
	static void AddSprite(uint32 Count) { SpriteParticles += Count; AddTotal(Count); }
	static void AddMesh(uint32 Count)   { MeshParticles   += Count; AddTotal(Count); }
	static void AddOther(uint32 Count)  { AddTotal(Count); }
	static void AddMemory(uint64 Total, uint64 Active, uint64 Reserved)
	{
		GTMemoryBytes     += Total;
		ActiveDataBytes   += Active;
		ReservedDataBytes += Reserved;
	}
	static void AddRibbonGeometry(
		uint32 EffectiveTessellation,
		uint32 ControlSegmentCount,
		uint32 SamplePointCount,
		uint32 VertexCount,
		uint32 IndexCount,
		bool bRuntimeCapped)
	{
		// Per-frame aggregates for Ribbon budget tuning:
		//   control segments -> sample points -> vertices/indices
		// and whether requested tessellation had to be lowered by runtime policy.
		++RibbonTrailBuilds;
		if (bRuntimeCapped)
		{
			++RibbonRuntimeCappedBuilds;
		}
		if (EffectiveTessellation > RibbonMaxEffectiveTessellation)
		{
			RibbonMaxEffectiveTessellation = EffectiveTessellation;
		}
		RibbonControlSegments += ControlSegmentCount;
		RibbonSamplePoints += SamplePointCount;
		RibbonVertices += VertexCount;
		RibbonIndices += IndexCount;
	}
	static void AddSpriteRTGeometry(uint32 InstanceCount, uint32 VertexCount, uint32 IndexCount)
	{
		SpriteRTInstances += InstanceCount;
		SpriteRTVertices += VertexCount;
		SpriteRTIndices += IndexCount;
	}
	static void AddMeshRTGeometry(uint32 InstanceCount, uint32 VertexCount, uint32 IndexCount)
	{
		MeshRTInstances += InstanceCount;
		MeshRTVertices += VertexCount;
		MeshRTIndices += IndexCount;
	}
	static void AddBeamRTGeometry(uint32 StripCount, uint32 VertexCount, uint32 IndexCount)
	{
		BeamRTStrips += StripCount;
		BeamRTVertices += VertexCount;
		BeamRTIndices += IndexCount;
	}

	static void Reset()
	{
		SpriteParticles = 0;
		MeshParticles = 0;
		TotalParticles = 0;
		// PeakTotalParticles 는 의도적으로 유지 (세션 누적 피크)
		EmitterCount = 0;
		ComponentCount = 0;
		ParticlesSpawned = 0;
		ParticlesKilled = 0;
		DrawCalls = 0;
		GTMemoryBytes = 0;
		ActiveDataBytes = 0;
		ReservedDataBytes = 0;
		SpriteRTInstances = 0;
		SpriteRTVertices = 0;
		SpriteRTIndices = 0;
		MeshRTInstances = 0;
		MeshRTVertices = 0;
		MeshRTIndices = 0;
		BeamRTStrips = 0;
		BeamRTVertices = 0;
		BeamRTIndices = 0;
		RibbonTrailBuilds = 0;
		RibbonRuntimeCappedBuilds = 0;
		RibbonMaxEffectiveTessellation = 0;
		RibbonControlSegments = 0;
		RibbonSamplePoints = 0;
		RibbonVertices = 0;
		RibbonIndices = 0;
	}
};

#define PARTICLE_STATS_RESET()                              FParticleStats::Reset()
#define PARTICLE_STATS_ADD_SPRITE(Count)                    FParticleStats::AddSprite(Count)
#define PARTICLE_STATS_ADD_MESH(Count)                      FParticleStats::AddMesh(Count)
#define PARTICLE_STATS_ADD_OTHER(Count)                     FParticleStats::AddOther(Count)
#define PARTICLE_STATS_ADD_SPAWN(Count)                     (FParticleStats::ParticlesSpawned += (Count))
#define PARTICLE_STATS_ADD_KILL(Count)                      (FParticleStats::ParticlesKilled += (Count))
#define PARTICLE_STATS_ADD_EMITTER()                        (FParticleStats::EmitterCount++)
#define PARTICLE_STATS_ADD_COMPONENT()                      (FParticleStats::ComponentCount++)
#define PARTICLE_STATS_ADD_MEMORY(Total, Active, Reserved)  FParticleStats::AddMemory((Total), (Active), (Reserved))
#define PARTICLE_STATS_ADD_DRAW_CALL()                      (FParticleStats::DrawCalls++)
#define PARTICLE_STATS_ADD_DRAW_CALLS(Count)                (FParticleStats::DrawCalls += (Count))
#define PARTICLE_STATS_ADD_SPRITE_RT_GEOMETRY(InstanceCount, VertexCount, IndexCount) \
	FParticleStats::AddSpriteRTGeometry((InstanceCount), (VertexCount), (IndexCount))
#define PARTICLE_STATS_ADD_MESH_RT_GEOMETRY(InstanceCount, VertexCount, IndexCount) \
	FParticleStats::AddMeshRTGeometry((InstanceCount), (VertexCount), (IndexCount))
#define PARTICLE_STATS_ADD_BEAM_RT_GEOMETRY(StripCount, VertexCount, IndexCount) \
	FParticleStats::AddBeamRTGeometry((StripCount), (VertexCount), (IndexCount))
#define PARTICLE_STATS_ADD_RIBBON_GEOMETRY(EffectiveTessellation, ControlSegmentCount, SamplePointCount, VertexCount, IndexCount, bRuntimeCapped) \
	FParticleStats::AddRibbonGeometry((EffectiveTessellation), (ControlSegmentCount), (SamplePointCount), (VertexCount), (IndexCount), (bRuntimeCapped))
#else
#define PARTICLE_STATS_RESET()                              ((void)0)
#define PARTICLE_STATS_ADD_SPRITE(Count)                    ((void)0)
#define PARTICLE_STATS_ADD_MESH(Count)                      ((void)0)
#define PARTICLE_STATS_ADD_OTHER(Count)                     ((void)0)
#define PARTICLE_STATS_ADD_SPAWN(Count)                     ((void)0)
#define PARTICLE_STATS_ADD_KILL(Count)                      ((void)0)
#define PARTICLE_STATS_ADD_EMITTER()                        ((void)0)
#define PARTICLE_STATS_ADD_COMPONENT()                      ((void)0)
#define PARTICLE_STATS_ADD_MEMORY(Total, Active, Reserved)  ((void)0)
#define PARTICLE_STATS_ADD_DRAW_CALL()                      ((void)0)
#define PARTICLE_STATS_ADD_DRAW_CALLS(Count)                ((void)0)
#define PARTICLE_STATS_ADD_SPRITE_RT_GEOMETRY(InstanceCount, VertexCount, IndexCount) ((void)0)
#define PARTICLE_STATS_ADD_MESH_RT_GEOMETRY(InstanceCount, VertexCount, IndexCount) ((void)0)
#define PARTICLE_STATS_ADD_BEAM_RT_GEOMETRY(StripCount, VertexCount, IndexCount) ((void)0)
#define PARTICLE_STATS_ADD_RIBBON_GEOMETRY(EffectiveTessellation, ControlSegmentCount, SamplePointCount, VertexCount, IndexCount, bRuntimeCapped) ((void)0)
#endif
