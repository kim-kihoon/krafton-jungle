#pragma once

#include "Particle/ParticleHelper.h"
#include "Particle/ParticleEvents.h"
#include "Core/Types/CollisionTypes.h"
#include "Core/Types/EngineTypes.h"
#include "Math/Transform.h"
#include "ParticleModule.h"
#include "ParticleLODLevel.h"
#include "Particle/Modules/ParticleModuleEventGenerator.h"

class UParticleEmitter;
class UParticleModuleCollision;
class UParticleModuleSpawn;
class UParticleSystemComponent;
class AActor;
struct FBaseParticle;
struct FDynamicEmitterDataBase;
struct FHitResult;

enum class EParticleValueSpace : uint8
{
	Simulation,
	Local,
	World,
};

struct FParticleEventSpawnOverride
{
	bool bUseLocation = false;
	FVector LocationWorld = FVector::ZeroVector;
	bool bInheritVelocity = false;
	FVector VelocityWorld = FVector::ZeroVector;
	float InheritVelocityScale = 1.0f;
};

// =============================================================================
// FParticleEmitterInstance
//   런타임 emitter 인스턴스. 한 PSC 의 emitter 1개당 하나.
//   - 입자 buffer (ParticleData/ParticleIndices) 관리
//   - 모듈 Spawn/Update 호출
//   - GameThread tick 의 끝에서 GetDynamicData() 로 RenderThread 용 snapshot 생성
//   - 즉 "live simulation owner" 이면서, 프레임 끝에는 RT가 소비할 replay contract의
//     GT 쪽 생산자 역할도 맡는다.
//
//   UObject 아님 (raw struct/class). PSC 가 소유 (unique_ptr 동등).
// =============================================================================
class FParticleEmitterInstance
{
public:
	FParticleEmitterInstance() = default;
	virtual ~FParticleEmitterInstance();

	// PSC 가 emitter 에셋 + 자신을 묶어 초기화. 모듈 layout 캐시도 이 시점에 적용.
	virtual void Init(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent);

	// 시뮬레이션 1 step. Spawn → Update → Cull 흐름.
	virtual void Tick(float DeltaTime);

	// 강제 리셋 (입자 전부 제거, accumulated time 초기화).
	virtual void Reset();

	// 현재 활성 입자 수 → snapshot 으로 옮겨 RenderThread 로 전달.
	// 여기서 만들어지는 값은 live particle state 그 자체가 아니라, current render replay
	// LOD 기준으로 해석한 emitter-level render contract다.
	// 호출 측이 unique_ptr 처럼 소유권을 받는다 (PSC 가 매 프레임 delete).
	virtual FDynamicEmitterDataBase* GetDynamicData();

	// 런타임 instance type 식별 (RTTI/dynamic_cast 없이 분기·static_cast 용). 서브클래스가 override.
	virtual EDynamicEmitterType GetType() const { return EDynamicEmitterType::Unknown; }

	// 모듈 → payload offset 조회 헬퍼.
	uint32 GetModuleDataOffset(const UParticleModule* InModule) const;

	// 활성 입자 i 의 BaseParticle 포인터.
	FBaseParticle*       GetParticleAt(uint32 InActiveIndex);
	const FBaseParticle* GetParticleAt(uint32 InActiveIndex) const;
	// emitter-level instance payload block의 시작 주소. Spawn burst state 같이
	// "입자별이 아닌 emitter별 런타임 상태"를 여기에 둔다.
	void*                GetInstancePayloadData();
	const void*          GetInstancePayloadData() const;

	template<typename T>
	T* GetModuleInstancePayload(const UParticleModule* InModule)
	{
		if (!InModule) return nullptr;
		if (!RuntimeStorage.InstanceData) return nullptr;
		if (!ModuleInstanceOffsetMap) return nullptr;

		auto It = ModuleInstanceOffsetMap->find(InModule);
		if (It == ModuleInstanceOffsetMap->end()) return nullptr;
		return reinterpret_cast<T*>(RuntimeStorage.InstanceData + It->second);
	}

	template<typename T>
	const T* GetModuleInstancePayload(const UParticleModule* InModule) const
	{
		if (!InModule) return nullptr;
		if (!RuntimeStorage.InstanceData) return nullptr;
		if (!ModuleInstanceOffsetMap) return nullptr;

		auto It = ModuleInstanceOffsetMap->find(InModule);
		if (It == ModuleInstanceOffsetMap->end()) return nullptr;
		return reinterpret_cast<const T*>(RuntimeStorage.InstanceData + It->second);
	}

	template<typename Func>
	void ForEachActiveParticle(Func&& InFunc)
	{
		for (uint32 i = 0; i < ActiveParticles; ++i)
		{
			if (FBaseParticle* Particle = GetParticleAt(i))
			{
				InFunc(i, *Particle);
			}
		}
	}

	template<typename Func>
	void ForEachActiveParticleReverse(Func&& InFunc)
	{
		for (int32 i = static_cast<int32>(ActiveParticles) - 1; i >= 0; --i)
		{
			if (FBaseParticle* Particle = GetParticleAt(static_cast<uint32>(i)))
			{
				InFunc(static_cast<uint32>(i), *Particle);
			}
		}
	}

	template<typename Func>
	void ForEachParticleSubset(const TArray<uint32>& ParticleIndices, Func&& InFunc)
	{
		for (uint32 ParticleIndex : ParticleIndices)
		{
			if (FBaseParticle* Particle = GetParticleAt(ParticleIndex))
			{
				InFunc(ParticleIndex, *Particle);
			}
		}
	}

	uint32 GetActiveParticleCount() const { return ActiveParticles; }
	uint32 GetMaxParticleCount()    const { return MaxActiveParticles; }
	uint32 GetParticleStride()      const { return ParticleStride; }
	// GT가 들고 있는 이 emitter 입자 버퍼의 총 할당 바이트 (data + indices + instance).
	uint64 GetAllocatedBytes()      const { return static_cast<uint64>(RuntimeStorage.MemBlock.size()); }
	// finite loop emitter가 더 이상 spawn하지 않는 상태인지 / 실제로 완전히 끝났는지.
	bool   IsSpawningComplete()     const;
	bool   IsFinished()             const;
	// graceful deactivate: spawn만 중단하고 기존 입자는 수명대로 소멸시킨다.
	// 켜지면 IsSpawningAllowed()=false, 입자가 0이 되면 IsFinished()=true → PSC가 시스템 완료 처리. Reset()에서 해제.
	void   SetHaltSpawning(bool bInHalt) { bHaltSpawning = bInHalt; }
	// active particle 기준의 느슨한 world-space bounds. 없으면 false를 반환해
	// PSC가 template fixed bounds로 fallback 할 수 있게 한다.
	bool   ComputeDynamicBounds(FVector& OutMin, FVector& OutMax) const;
	// PSC가 선택한 현재 LOD를 런타임 instance에 전달하는 entry point.
	void   SetCurrentLODIndex(int32 InLODIndex);
	int32  GetCurrentLODIndex() const { return CurrentLODIndex; }
	int32  GetParticleSimulationLODIndex(const FBaseParticle& Particle) const;
	UParticleLODLevel* GetParticleSimulationLOD(const FBaseParticle& Particle) const;

	// EventManager 가 디스패치할 큐. EventGenerator 모듈이 push.
	const TArray<FParticleEventSpawnData>&     GetSpawnEvents()     const { return SpawnEvents; }
	const TArray<FParticleEventDeathData>&     GetDeathEvents()     const { return DeathEvents; }
	const TArray<FParticleEventCollideData>&   GetCollisionEvents() const { return CollisionEvents; }
	const TArray<FParticleEventBurstData>&     GetBurstEvents()     const { return BurstEvents; }

	void ClearPendingEvents();
	void EnqueueSpawnEvent(const FParticleEventSpawnData& InEvent);
	void EnqueueDeathEvent(const FParticleEventDeathData& InEvent);
	void EnqueueCollisionEvent(const FParticleEventCollideData& InEvent);
	void EnqueueBurstEvent(const FParticleEventBurstData& InEvent);
	void EmitSpawnEvent(const FParticleEventSpawnData& InEvent);
	void EmitDeathEvent(const FParticleEventDeathData& InEvent);
	void EmitCollisionEvent(const FParticleEventCollideData& InEvent);
	void EmitBurstEvent(const FParticleEventBurstData& InEvent);

	// Event Receiver가 이벤트 위치/속도를 기준으로 이 emitter에 파티클을 생성할 때 사용한다.
	// 일반 SpawnModule의 rate 제한과 bHaltSpawning에는 묶이지 않는다.
	int32 SpawnFromEvent(int32 Count, const FParticleEventSpawnOverride& SpawnOverride);
	void KillAllParticles(bool bStopSpawning);
	void HandleReceivedEvents(
		const TArray<FParticleEventSpawnData>& InSpawnEvents,
		const TArray<FParticleEventDeathData>& InDeathEvents,
		const TArray<FParticleEventCollideData>& InCollisionEvents,
		const TArray<FParticleEventBurstData>& InBurstEvents);

	UParticleEmitter*           GetEmitter()       const { return Emitter; }
	UParticleSystemComponent*   GetComponent()     const { return Component; }
	UParticleLODLevel*          GetCurrentLOD()    const;
	float                       GetEmitterTimeSeconds() const { return EmitterTimeSeconds; }
	// 현재 loop 안에서의 emitter time(seconds). Initial Distribution의 SpawnTime 기준이다.
	float                       GetCurrentLoopTimeSeconds() const { return CurrentLoopTimeSeconds; }
	bool                        UsesLocalSpace()   const;
	// simulation space는 Required.bUseLocalSpace에 따라 local/world 중 하나로 고정된다.
	// 모듈은 입력 값이 어느 공간에서 왔는지만 선언하고, 실제 해석은 이 API가 맡는다.
	FVector                     ConvertVectorToSimulation(const FVector& V, EParticleValueSpace SourceSpace) const;
	FVector                     ConvertVectorFromSimulation(const FVector& V, EParticleValueSpace TargetSpace) const;
	FVector                     ConvertPositionToSimulation(const FVector& P, EParticleValueSpace SourceSpace) const;
	FVector                     ConvertPositionFromSimulation(const FVector& P, EParticleValueSpace TargetSpace) const;

	// World ↔ Local 헬퍼 (Required.bUseLocalSpace 따라 분기).
	FTransform GetComponentToWorld() const;

protected:
	// --- 내부 도우미 (subclass 가 override 가능) ----------------------------------
	// SpawnFraction 누적 → 정수 입자 수로 변환하여 SpawnInternal 호출.
	virtual void SpawnParticles(float DeltaTime);

	// BurstList를 emitter instance 시간 기준으로 처리한다.
	// SpawnModule은 burst 설정만 보유하고, 실제 burst runtime state는 instance payload에 둔다.
	int32 SpawnBurstParticles(UParticleModuleSpawn* SpawnModule, float DeltaTime, int32& InOutSpawnBudget);

	// 새 입자 N 개를 만든다. 내부적으로 모듈의 Spawn() 을 차례로 호출.
	// StartTime/Increment는 언리얼 Cascade의 SpawnParticles(Count, StartTime, Increment)처럼
	// 각 particle의 emitter-loop spawn time을 계산하기 위한 값이다.
	virtual int32 SpawnInternal(
		int32 Count,
		float StartTime,
		float Increment,
		float StepDeltaTime,
		const FParticleEventSpawnOverride* SpawnOverride = nullptr);

	// 활성 입자 전체에 대해 기본 적분을 수행한 뒤, spawn-time simulation LOD 기준으로
	// subset dispatch된 모듈 Update + Collision + Kill/compaction을 처리한다.
	// Over-Life 모듈은 여기서 증가한 Particle->RelativeTime(0..1)을 기준으로 Distribution을 평가한다.
	virtual void UpdateParticles(float DeltaTime);

	// Collision은 일반 module Update가 아니라 explicit simulation phase에서 해결한다.
	// Module은 authoring/settings와 payload init을 맡고, 실제 world hit query와 response는
	// emitter instance가 담당한다.
	virtual void ResolveParticleCollisions(float DeltaTime, const TArray<TArray<uint32>>& ActiveParticleBuckets);

	// MaxActiveParticles 변경 시 ParticleData/Indices 를 재할당.
	void ResizeParticleData(uint32 NewMax);

	// RT replay snapshot은 아직 per-particle SimulationLODIndex bucket이 아니라
	// emitter-level "현재 render view"로 생성한다. 이 helper는 그 current render
	// LOD basis를 simulation continuity와 구분해 드러내기 위한 경계다.
	UParticleLODLevel* GetRenderReplayLODLevel() const;

	// shared GT replay build entry point.
	//   - particle snapshot copy
	//   - current render replay LOD resolve
	//   - RequiredModule 기반 base render contract fill
	// type-specific shaping metadata는 각 subclass GetDynamicData()에서 이어서 채운다.
	void FillReplayData(FDynamicEmitterReplayDataBase& OutData) const;

private:
	struct FEmitterNearbyCollisionCache
	{
		bool bValid = false;
		bool bHasNearbyCollisionEvidence = false;
		FVector CachedBoundsCenter = FVector::ZeroVector;
		FVector CachedBoundsExtent = FVector::ZeroVector;
		float LastRefreshTimeSeconds = -1.0f;
		int32 CachedLODIndex = 0;
		int32 EvidenceProbeCount = 0;
	};

	struct FParticleCollisionDebugStats
	{
		// ---- outer collision policy / workload gating ----
		int32 ActiveParticles = 0;
		int32 CurrentLODIndex = 0;
		int32 EffectiveBudget = 0;
		int32 CandidateCount = 0;
		int32 HighPriorityCandidateCount = 0;
		int32 FallbackCandidateCount = 0;
		int32 QueriedCount = 0;
		int32 SkippedByState = 0;
		int32 SkippedByEarlyOut = 0;
		int32 SkippedByBudget = 0;

		// ---- simulation-contract interpretation / accepted-hit outcomes ----
		int32 NoHitCount = 0;
		int32 AcceptedHitCount = 0;
		int32 SuppressedAsNoiseCount = 0;
		int32 EmittedEventCount = 0;
		int32 EventGatedCount = 0;
		int32 StopLikeLowSpeedCount = 0;
		int32 KilledImmediateCount = 0;
		int32 FrozenAfterLimitCount = 0;
		int32 IgnoredFurtherCollisionsCount = 0;
		int32 EmitterPrunedCount = 0;
		int32 EmitterPruneProbeHitCount = 0;
		int32 EmitterPruneProbeMissCount = 0;
		int32 NearbyCacheRefreshCount = 0;
		int32 NearbyCacheReuseCount = 0;
		int32 NearbyEvidenceHitCount = 0;
		int32 NearbyEvidenceMissCount = 0;
		int32 NearbyEvidenceProbeCount = 0;
		bool bCollisionFullyDisabledForLOD = false;
		bool bCollisionEventGatedForLOD = false;
		bool bUsingCollisionOuterPolicyOverride = false;
		bool bNearbyCollisionCacheValid = false;
		bool bNearbyCollisionEvidence = false;
	};

	struct FResolvedCollisionOuterPolicy
	{
		bool bCollisionFullyDisabled = false;
		bool bEmitCollisionEvents = true;
		bool bUsingAuthoringOverride = false;
		int32 CollisionQueryBudget = 0;
	};

	uint32 GetInitialParticleCapacity() const;
	uint32 GrowParticleCapacity(uint32 Current, uint32 Required) const;
	bool IsParticleKilled(const FBaseParticle* Particle) const;
	void ClearSpawnedFlag(FBaseParticle* Particle) const;
	const class UParticleModuleRequired* GetRequiredModule() const;
	// Convenience lookup for the emitter's current-LOD collision authoring.
	// This is useful for outer policy defaults, not a replacement for
	// per-particle simulation-LOD collision module resolution.
	const UParticleModuleCollision* GetCollisionModule() const;
	const UParticleModuleCollision* GetCollisionModule(const UParticleLODLevel* LOD) const;
	const UParticleModuleCollision* ResolveCollisionOuterPolicyModule(
		const TArray<TArray<uint32>>& ActiveParticleBuckets) const;
	FResolvedCollisionOuterPolicy ResolveCollisionOuterPolicyForCurrentLOD() const;
	void InitializeCollisionOuterPolicyDebugStats(
		FParticleCollisionDebugStats& OutStats,
		int32 CollisionBudget) const;
	void BuildActiveParticleSimulationLODBuckets(TArray<TArray<uint32>>& OutBuckets) const;
	bool FinalizeParticleCollisionWithoutQuery(
		FBaseParticle& Particle,
		const UParticleModuleCollision& CollisionModule,
		uint32 ModuleOffset) const;
	bool ShouldSkipParticleCollisionForBudget(
		const FBaseParticle& Particle,
		const UParticleModuleCollision& CollisionModule,
		uint32 ModuleOffset,
		float DeltaTime) const;
	float GetParticleCollisionPriorityScore(
		const FBaseParticle& Particle,
		const UParticleModuleCollision& CollisionModule,
		uint32 ModuleOffset,
		float DeltaTime) const;
	bool IsHighPriorityCollisionCandidate(float PriorityScore) const;
	bool ShouldEmitCollisionEventForAcceptedHit(
		const UParticleModuleCollision& CollisionModule) const;
	void EmitCollisionEventForAcceptedHit(
		const FBaseParticle& Particle,
		const FVector& CollisionNormal,
		const FVector& ImpactVelocityWorld,
		const FHitResult& Hit,
		float CollisionTimeSeconds);
	bool BuildParticleCollisionQuerySegment(
		const FBaseParticle& Particle,
		FVector& OutStartWorld,
		FVector& OutTravelDirection,
		float& OutTravelDistance) const;
	ECollisionChannel GetParticleCollisionQueryChannel(
		const UParticleModuleCollision& CollisionModule) const;
	const AActor* GetParticleCollisionQueryIgnoreActor() const;
	bool PerformParticleCollisionQuery(
		const FVector& StartWorld,
		const FVector& TravelDirection,
		float TravelDistance,
		const UParticleModuleCollision& CollisionModule,
		FHitResult& OutHit) const;
	bool ShouldPruneEmitterCollisionByBounds(
		const UParticleModuleCollision& CollisionModule,
		FParticleCollisionDebugStats* DebugStats);
	bool HasNearbyCollisionForEmitterBounds(
		const UParticleModuleCollision& CollisionModule,
		const FVector& BoundsMin,
		const FVector& BoundsMax,
		FParticleCollisionDebugStats* DebugStats);
	void RefreshEmitterNearbyCollisionCache(
		const UParticleModuleCollision& CollisionModule,
		FParticleCollisionDebugStats* DebugStats);
	bool GatherEmitterNearbyCollisionEvidence(
		const UParticleModuleCollision& CollisionModule,
		const FVector& BoundsCenter,
		const FVector& BoundsExtent,
		int32& OutEvidenceProbeCount);
	bool ShouldDebugParticleCollisions() const;
	void DebugDrawParticleCollisionQuery(
		const FVector& StartWorld,
		const FVector& EndWorld,
		const FColor& Color) const;
	void DebugDrawEmitterCollisionPruneProbe(
		const FVector& StartWorld,
		const FVector& EndWorld,
		const FColor& Color) const;
	void DebugDrawEmitterNearbyCollisionProbe(
		const FVector& StartWorld,
		const FVector& EndWorld,
		const FColor& Color) const;
	void DebugDrawParticleCollisionHit(
		const FHitResult& Hit,
		const FVector& CollisionNormal,
		const FColor& PointColor,
		const FColor& NormalColor) const;
	void DebugLogParticleCollisionStats(const FParticleCollisionDebugStats& Stats) const;
	bool ResolveSingleParticleCollision(
		FBaseParticle& Particle,
		const UParticleModuleCollision& CollisionModule,
		uint32 ModuleOffset,
		float DeltaTime,
		FParticleCollisionDebugStats* DebugStats);
	bool ApplyImmediateParticleCollisionResponse(
		FBaseParticle& Particle,
		const UParticleModuleCollision& CollisionModule,
		const FHitResult& Hit,
		const FVector& ImpactVelocity,
		FParticleCollisionDebugStats* DebugStats) const;
	bool ShouldProcessCollisionsForCurrentLOD() const;
	bool IsCollisionFullyDisabledForCurrentLOD() const;
	bool ShouldEmitCollisionEventsForCurrentLOD() const;
	int32 GetCollisionCheckBudgetForCurrentLOD() const;
	void ApplyEventSpawnOverride(FBaseParticle& Particle, const FParticleEventSpawnOverride& SpawnOverride) const;
	void HandleReceivedEvent(const FParticleEventDataBase& Event);
	bool IsSpawningAllowed() const;
	void AdvanceLoopState(float DeltaTime);

	template<typename Func>
	void ForEachEventGenerator(Func&& InFunc) const
	{
		UParticleLODLevel* LOD = GetCurrentLOD();
		if (!LOD)
		{
			return;
		}

		for (UParticleModule* Module : LOD->Modules)
		{
			if (!Module || !Module->IsEnabled())
			{
				continue;
			}

			if (const auto* EventGenerator = Cast<UParticleModuleEventGenerator>(Module))
			{
				InFunc(EventGenerator);
			}
		}
	}

protected:
	UParticleEmitter*         Emitter     = nullptr;
	UParticleSystemComponent* Component   = nullptr;

	// 모듈 payload offset 캐시 — emitter 가 미리 계산해 둔 것을 가져와 빠른 조회.
	const TMap<const UParticleModule*, uint32>* ModuleOffsetMap = nullptr;
	const TMap<const UParticleModule*, uint32>* ModuleInstanceOffsetMap = nullptr;

	// 입자 buffer (BaseParticle + payload) flat layout.
	uint32           ParticleStride       = sizeof(FBaseParticle);
	uint32           MaxActiveParticles   = 0;
	uint32           ActiveParticles      = 0;
	FParticleStorage RuntimeStorage;

	// Spawn 누적 (분수 입자 carry-over)
	float SpawnFraction      = 0.0f;
	float EmitterTimeSeconds = 0.0f; // 누적 sim time
	float CurrentLoopTimeSeconds = 0.0f;
	int32 LoopCount          = 0;
	int32 CurrentLODIndex    = 0;
	FVector LastCollisionPruneBoundsCenter = FVector::ZeroVector;
	bool bHasLastCollisionPruneBoundsCenter = false;
	FEmitterNearbyCollisionCache NearbyCollisionCache;
	// graceful deactivate 플래그 — true면 신규 spawn 중단(기존 입자는 계속 tick·소멸). Reset()에서 해제.
	bool  bHaltSpawning      = false;

	// 이벤트 큐 (EventGenerator 모듈이 push, PSC/EventManager 가 drain).
	TArray<FParticleEventSpawnData>     SpawnEvents;
	TArray<FParticleEventDeathData>     DeathEvents;
	TArray<FParticleEventCollideData>   CollisionEvents;
	TArray<FParticleEventBurstData>     BurstEvents;
};

// -----------------------------------------------------------------------------
// FParticleSpriteEmitterInstance / FParticleMeshEmitterInstance /
// FParticleBeamEmitterInstance / FParticleRibbonEmitterInstance
//   TypeData 모듈의 종류에 따라 emitter::CreateInstance() 가 선택해 만든다.
//   각 subclass 가 GetDynamicData() 에서 본인 type 의 ReplayData 를 채워 반환.
// -----------------------------------------------------------------------------
class FParticleSpriteEmitterInstance : public FParticleEmitterInstance
{
public:
	FDynamicEmitterDataBase* GetDynamicData() override;
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Sprite; }
};

class FParticleMeshEmitterInstance : public FParticleEmitterInstance
{
public:
	FDynamicEmitterDataBase* GetDynamicData() override;
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Mesh; }
};

class FParticleBeamEmitterInstance : public FParticleEmitterInstance
{
public:
	FDynamicEmitterDataBase* GetDynamicData() override;
	void Reset() override;
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Beam; }
	// Beam 은 source/target 두 endpoint 가 필요. EventGenerator/외부에서 지정.
	void SetEndpoints(const FVector& InSource, const FVector& InTarget);
	void ResetEndpointLocks();
protected:
	FVector SourcePoint = { 0, 0, 0 };
	FVector TargetPoint = { 0, 0, 0 };
	FVector LockedSourcePoint = { 0, 0, 0 };
	FVector LockedTargetPoint = { 0, 0, 0 };
	bool bHasLockedSourcePoint = false;
	bool bHasLockedTargetPoint = false;
	bool bHasExplicitEndpoints = false;
};

class FParticleRibbonEmitterInstance : public FParticleEmitterInstance
{
public:
	FDynamicEmitterDataBase* GetDynamicData() override;
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Ribbon; }
};
