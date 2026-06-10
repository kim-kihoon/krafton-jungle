#include "ParticleSystemComponent.h"

#include "Particle/ParticleSystem.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleEmitterInstance.h"
#include "Particle/ParticleEventManager.h"
#include "Particle/ParticleSystemManager.h"
#include "GameFramework/World.h"
#include "Object/GarbageCollection.h"
#include "Render/Proxy/Particle/ParticleSystemSceneProxy.h"
#include "Serialization/Archive.h"
#include "Core/Logging/Log.h"
#include "Profiling/Stats/ParticleStats.h"

#include <algorithm>

UParticleSystemComponent::UParticleSystemComponent()  {}
UParticleSystemComponent::~UParticleSystemComponent()
{
	DestroyEmitterInstances();
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	DestroyEmitterInstances();

	Template = InTemplate;
	if (Template && !Template->GetSourcePath().empty())
	{
		TemplatePath = Template->GetSourcePath();
	}
	else if (!Template)
	{
		TemplatePath = "None";
	}
	AccumulatedTime = 0.0f;
	PendingEvents = {};
	bHasWarnedMissingEventManager = false;
	MissingEventManagerTimeSeconds = 0.0f;
	ResetAutomaticLODTransitionState();

	if (Template)
	{
		Template->BuildEmitters();
		ClampCurrentLODIndex();
		CreateEmitterInstances();
	}

	if (bResetOnActivate)
	{
		ResetParticles();
	}

	PushDynamicDataToProxy();
	MarkWorldBoundsDirty();
}

void UParticleSystemComponent::SetEventManager(AParticleEventManager* InMgr)
{
	EventManager = InMgr;
}

AParticleEventManager* UParticleSystemComponent::GetEventManager() const
{
	return EventManager.Get();
}

void UParticleSystemComponent::LoadTemplateFromPath()
{
	const FString& Path = TemplatePath.ToString();
	if (Path.empty() || Path == "None")
	{
		SetTemplate(nullptr);
		return;
	}

	SetTemplate(FParticleSystemManager::Get().Load(Path));
}

void UParticleSystemComponent::Activate(bool bReset)
{
	bActive = true;
	RefreshEventManagerBinding();
	ResetAutomaticLODTransitionState();

	if (Template && EmitterInstances.empty())
	{
		Template->BuildEmitters();
		ClampCurrentLODIndex();
		CreateEmitterInstances();
	}

	// 재활성화 시 halt 해제 — 이전 Deactivate(graceful)로 spawn이 막혀 있을 수 있다.
	// (bReset 경로의 ResetParticles 도 해제하지만, bReset 없이 Activate 되는 경우까지 보장.)
	for (FParticleEmitterInstance* Inst : EmitterInstances)
		if (Inst) Inst->SetHaltSpawning(false);

	if (bReset || bResetOnActivate)
	{
		ResetParticles();
	}

	PushDynamicDataToProxy();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UParticleSystemComponent::Deactivate()
{
	// graceful: 신규 spawn만 중단하고 기존 입자는 수명대로 소멸시킨다(tick 유지).
	// 모든 입자가 사라지면 TickComponent 의 IsSystemFinished 가 bActive=false + OnSystemFinished 를 처리.
	// (즉시 정리가 필요하면 ResetParticles() 를 별도로 호출.)
	for (FParticleEmitterInstance* Inst : EmitterInstances)
		if (Inst) Inst->SetHaltSpawning(true);
	MissingEventManagerTimeSeconds = 0.0f;
	ResetAutomaticLODTransitionState();
}

void UParticleSystemComponent::ResetParticles()
{
	for (FParticleEmitterInstance* Inst : EmitterInstances)
	{
		if (Inst) Inst->Reset();
	}
	AccumulatedTime = 0.0f;
	PendingEvents = {};
	bHasWarnedMissingEventManager = false;
	MissingEventManagerTimeSeconds = 0.0f;
	ResetAutomaticLODTransitionState();

	PushDynamicDataToProxy();
	MarkWorldBoundsDirty();
}

void UParticleSystemComponent::BeginPlay()
{
	UPrimitiveComponent::BeginPlay();
	RefreshEventManagerBinding();

	if (Template && EmitterInstances.empty())
	{
		Template->BuildEmitters();
		ClampCurrentLODIndex();
	}

	if (bAutoActivate) Activate(bResetOnActivate);
}

void UParticleSystemComponent::EndPlay()
{
	Deactivate();
	DestroyEmitterInstances();
	SetEventManager(nullptr);

	UPrimitiveComponent::EndPlay();
}

void UParticleSystemComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	UPrimitiveComponent::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(Template, "UParticleSystemComponent.Template");
}

void UParticleSystemComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                             FActorComponentTickFunction& ThisTickFunction)
{
	UPrimitiveComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetEventManager())
	{
		// BeginPlay ordering can leave PSC briefly unbound even though a manager registers
		// later in the same runtime startup path. Re-query the provider, but do not do
		// any discovery or spawning here.
		RefreshEventManagerBinding();
	}

	if (!bActive) return;
	if (!Template) return;

	// PSC tick 전체(LOD 선택 + 시뮬 + 이벤트 디스패치 + dynamic data push) CPU 비용.
	SCOPE_STAT_CAT("ParticleTick", "Particles");

	if (EmitterInstances.empty())
	{
		Template->BuildEmitters();
		ClampCurrentLODIndex();
		CreateEmitterInstances();
	}

	if (Template->bUseAutomaticLOD)
	{
		// Phase 3 keeps automatic mode authoritative during runtime ticking, but
		// now stabilizes raw distance-based selection with hysteresis and delay.
		UpdateAutomaticLODSelection(DeltaTime);
	}
	else
	{
		ResetAutomaticLODTransitionState();
	}

	// PSC가 현재 선택한 LOD를 source of truth로 들고 있고, instance는 매 tick 그 값을 따른다.
	ApplyCurrentLODToEmitterInstances();

#if STATS
	// 파티클 개수/메모리 게이지 누적. TickInterval skip 프레임에도 반영되도록 시뮬레이션
	// 게이트(아래 AccumulatedTime 체크) 이전에 집계한다. 값은 직전 tick 기준 — 오버레이 표시 1프레임 lag.
	PARTICLE_STATS_ADD_COMPONENT();
	for (const FParticleEmitterInstance* Inst : EmitterInstances)
	{
		if (!Inst) continue;
		PARTICLE_STATS_ADD_EMITTER();

		const uint32 Active = Inst->GetActiveParticleCount();
		switch (Inst->GetType())
		{
		case EDynamicEmitterType::Sprite: PARTICLE_STATS_ADD_SPRITE(Active); break;
		case EDynamicEmitterType::Mesh:   PARTICLE_STATS_ADD_MESH(Active);   break;
		default:                          PARTICLE_STATS_ADD_OTHER(Active);  break;
		}

		const uint64 Stride        = Inst->GetParticleStride();
		const uint64 ActiveBytes   = static_cast<uint64>(Active) * Stride;
		const uint64 ReservedBytes = static_cast<uint64>(Inst->GetMaxParticleCount()) * Stride;
		PARTICLE_STATS_ADD_MEMORY(Inst->GetAllocatedBytes(), ActiveBytes, ReservedBytes);
	}
#endif

	// 매 프레임 Tick이 아닌, 일정 간격마다 몰아서 Tick
	// 실제로 시뮬레이션에 넘길 DeltaTime
	float StepDeltaTime = DeltaTime;

	if (TickInterval > 0.0f)
	{
		AccumulatedTime += DeltaTime;

		if (AccumulatedTime < TickInterval)
		{
			return;
		}

		StepDeltaTime = AccumulatedTime;
		AccumulatedTime = 0.0f;
	}

	if (!GetEventManager())
	{
		MissingEventManagerTimeSeconds += StepDeltaTime;
	}
	else
	{
		MissingEventManagerTimeSeconds = 0.0f;
	}

	for (FParticleEmitterInstance* Inst : EmitterInstances)
	{
		if (!Inst) continue;

		Inst->Tick(StepDeltaTime);

		const auto& SpawnEvents = Inst->GetSpawnEvents();
		PendingEvents.Spawn.insert(PendingEvents.Spawn.end(), SpawnEvents.begin(), SpawnEvents.end());

		const auto& DeathEvents = Inst->GetDeathEvents();
		PendingEvents.Death.insert(PendingEvents.Death.end(), DeathEvents.begin(), DeathEvents.end());

		const auto& CollisionEvents = Inst->GetCollisionEvents();
		PendingEvents.Collision.insert(PendingEvents.Collision.end(), CollisionEvents.begin(), CollisionEvents.end());

		const auto& BurstEvents = Inst->GetBurstEvents();
		PendingEvents.Burst.insert(PendingEvents.Burst.end(), BurstEvents.begin(), BurstEvents.end());

		Inst->ClearPendingEvents();
	}

	DispatchEventsToReceivers();
	DispatchEventsToManager();

	PushDynamicDataToProxy();
	MarkWorldBoundsDirty();

	// finite emitter가 모두 spawn 종료 + active particle 0 상태가 되면 시스템 완료로 본다.
	if (IsSystemFinished())
	{
		bActive = false;
		OnSystemFinished.Broadcast(this);
	}
}

void UParticleSystemComponent::CreateRenderState()
{
	if (Template && EmitterInstances.empty())
	{
		Template->BuildEmitters();
		ClampCurrentLODIndex();
		CreateEmitterInstances();
	}
	UPrimitiveComponent::CreateRenderState();

	PushDynamicDataToProxy();
}

void UParticleSystemComponent::DestroyRenderState()
{
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		FParticleSystemSceneProxy* ParticleProxy = static_cast<FParticleSystemSceneProxy*>(Proxy);
		ParticleProxy->SetDynamicData(nullptr);
	}

	UPrimitiveComponent::DestroyRenderState();
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}

void UParticleSystemComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (!PropertyName) return;

	if (std::strcmp(PropertyName, "TemplatePath") == 0 ||
		std::strcmp(PropertyName, "Template") == 0)
	{
		LoadTemplateFromPath();
		return;
	}

	if (std::strcmp(PropertyName, "CurrentLODIndex") == 0 ||
		std::strcmp(PropertyName, "LOD Level") == 0)
	{
		ClampCurrentLODIndex();

		ApplyCurrentLODToEmitterInstances();

		PushDynamicDataToProxy();
		MarkWorldBoundsDirty();
		return;
	}

	if (std::strcmp(PropertyName, "TickInterval") == 0 || 
		std::strcmp(PropertyName, "Tick Interval (sec)") == 0)
	{
		if (TickInterval < 0.0f) TickInterval = 0.0f;
		AccumulatedTime = 0.0f;
		return;
	}

	if (std::strcmp(PropertyName, "bAutoActivate") == 0 ||
		std::strcmp(PropertyName, "Auto Activate") == 0)
	{
		// AutoActivate는 보통 BeginPlay/Create 시점 정책이라
		// 편집 즉시 Activate/Deactivate하지 않는 게 안전함.
		return;
	}

	if (std::strcmp(PropertyName, "bResetOnActivate") == 0 ||
		std::strcmp(PropertyName, "Reset On Activate") == 0)
	{
		// 단순 정책값. 즉시 재생성 불필요.
		return;
	}
}

void UParticleSystemComponent::PostDuplicate()
{
	UPrimitiveComponent::PostDuplicate();

	DestroyEmitterInstances();
	AccumulatedTime = 0.0f;
	PendingEvents = {};
	bHasWarnedMissingEventManager = false;
	MissingEventManagerTimeSeconds = 0.0f;
	ResetAutomaticLODTransitionState();

	ClampCurrentLODIndex();

	LoadTemplateFromPath();

	if (bAutoActivate)
	{
		Activate(true);
	}
	else
	{
		PushDynamicDataToProxy();
		MarkWorldBoundsDirty();
	}
}

void UParticleSystemComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		DestroyEmitterInstances();
		AccumulatedTime = 0.0f;
		PendingEvents = {};
		bHasWarnedMissingEventManager = false;
		MissingEventManagerTimeSeconds = 0.0f;
		ResetAutomaticLODTransitionState();

		ClampCurrentLODIndex();

		LoadTemplateFromPath();

		MarkWorldBoundsDirty();
	}
}

void UParticleSystemComponent::UpdateWorldAABB() const
{
	if (!Template)
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	const FVector WorldOrigin = GetWorldLocation();
	bool bHasDynamicBounds = false;

	if (!Template->bUseFixedRelativeBoundingBox)
	{
		// fixed bounds를 끄면 emitter별 dynamic bounds를 합산한다.
		// active particle이 없는 emitter는 false를 반환하고 자연스럽게 제외된다.
		for (const FParticleEmitterInstance* Inst : EmitterInstances)
		{
			if (!Inst)
			{
				continue;
			}

			FVector EmitterBoundsMin;
			FVector EmitterBoundsMax;
			if (!Inst->ComputeDynamicBounds(EmitterBoundsMin, EmitterBoundsMax))
			{
				continue;
			}

			if (!bHasDynamicBounds)
			{
				WorldAABBMinLocation = EmitterBoundsMin;
				WorldAABBMaxLocation = EmitterBoundsMax;
				bHasDynamicBounds = true;
				continue;
			}

			WorldAABBMinLocation.X = std::min(WorldAABBMinLocation.X, EmitterBoundsMin.X);
			WorldAABBMinLocation.Y = std::min(WorldAABBMinLocation.Y, EmitterBoundsMin.Y);
			WorldAABBMinLocation.Z = std::min(WorldAABBMinLocation.Z, EmitterBoundsMin.Z);
			WorldAABBMaxLocation.X = std::max(WorldAABBMaxLocation.X, EmitterBoundsMax.X);
			WorldAABBMaxLocation.Y = std::max(WorldAABBMaxLocation.Y, EmitterBoundsMax.Y);
			WorldAABBMaxLocation.Z = std::max(WorldAABBMaxLocation.Z, EmitterBoundsMax.Z);
		}
	}

	if (!bHasDynamicBounds)
	{
		// dynamic bounds가 없거나, 자산이 fixed relative bounding box 사용을 강제하면
		// 기존 template bounds를 component origin 기준으로 그대로 사용한다.
		WorldAABBMinLocation = WorldOrigin + Template->SystemBoundsMin;
		WorldAABBMaxLocation = WorldOrigin + Template->SystemBoundsMax;
	}

	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

FParticleEmitterInstance* UParticleSystemComponent::GetEmitterInstance(int32 Index) const
{
	if (Index < 0 || Index >= static_cast<int32>(EmitterInstances.size())) return nullptr;
	return EmitterInstances[Index];
}

void UParticleSystemComponent::RebuildInstances(bool bReset)
{
	if (Template)
	{
		Template->BuildEmitters();
		ClampCurrentLODIndex();
		CreateEmitterInstances();
	}
	else
	{
		DestroyEmitterInstances();
	}

	if (bReset)
	{
		ResetParticles();
	}

	PushDynamicDataToProxy();
	MarkWorldBoundsDirty();
}

UParticleSystemComponent::FDynamicData* UParticleSystemComponent::BuildDynamicData()
{
	FDynamicData* Data = new FDynamicData();

	for (FParticleEmitterInstance* Inst : EmitterInstances)
	{
		if (!Inst) continue;

		FDynamicEmitterDataBase* EmitterData = Inst->GetDynamicData();
		if (!EmitterData) continue;

		Data->Emitters.push_back(EmitterData);
	}

	return Data;
}

void UParticleSystemComponent::CreateEmitterInstances()
{
	DestroyEmitterInstances();
	RefreshEventManagerBinding();
	ResetAutomaticLODTransitionState();

	if (!Template) return;

	Template->BuildEmitters();
	ClampCurrentLODIndex();

	for (UParticleEmitter* Emitter : Template->Emitters)
	{
		if (!Emitter) continue;
		if (!Emitter->bEnabled) continue;

		FParticleEmitterInstance* Inst = Emitter->CreateInstance(this);
		if (!Inst) continue;

		Inst->Init(Emitter, this);
		Inst->SetCurrentLODIndex(CurrentLODIndex);
		EmitterInstances.push_back(Inst);
	}
}

void UParticleSystemComponent::RefreshEventManagerBinding()
{
	SetEventManager(FParticleSystemManager::Get().GetDefaultEventManager());
	if (GetEventManager())
	{
		MissingEventManagerTimeSeconds = 0.0f;
	}
}

void UParticleSystemComponent::ResetAutomaticLODTransitionState()
{
	PendingAutomaticLODIndex = -1;
	PendingAutomaticLODTimeSeconds = 0.0f;
}

void UParticleSystemComponent::UpdateAutomaticLODSelection(float DeltaTime)
{
	if (!Template || !Template->bUseAutomaticLOD)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FMinimalViewInfo ActivePOV;
	if (!World->GetActivePOV(ActivePOV))
	{
		// Preview/tool or partial-runtime contexts may not have an active POV yet.
		// Keep the current manual/runtime-selected index unchanged in that case,
		// and drop any pending transition because the source view is unavailable.
		ResetAutomaticLODTransitionState();
		return;
	}

	const float DistanceToView = FVector::Distance(ActivePOV.Location, GetWorldLocation());
	const int32 RawTargetLODIndex = Template->GetLODIndexForDistance(DistanceToView);
	int32 StabilizedTargetLODIndex = CurrentLODIndex;
	const float Hysteresis = std::max(0.0f, Template->LODDistanceHysteresis);

	if (RawTargetLODIndex > CurrentLODIndex)
	{
		while (StabilizedTargetLODIndex < RawTargetLODIndex)
		{
			const int32 NextLODIndex = StabilizedTargetLODIndex + 1;
			const float EnterDistance = Template->GetLODDistance(NextLODIndex) + Hysteresis;
			if (DistanceToView < EnterDistance)
			{
				break;
			}

			StabilizedTargetLODIndex = NextLODIndex;
		}
	}
	else if (RawTargetLODIndex < CurrentLODIndex)
	{
		while (StabilizedTargetLODIndex > RawTargetLODIndex)
		{
			const float ReturnDistance = std::max(
				0.0f,
				Template->GetLODDistance(StabilizedTargetLODIndex) - Hysteresis);
			if (DistanceToView >= ReturnDistance)
			{
				break;
			}

			--StabilizedTargetLODIndex;
		}
	}

	if (StabilizedTargetLODIndex == CurrentLODIndex)
	{
		ResetAutomaticLODTransitionState();
		return;
	}

	const float SwitchDelay = std::max(0.0f, Template->LODSwitchDelay);
	if (SwitchDelay <= 0.0f)
	{
		CurrentLODIndex = StabilizedTargetLODIndex;
		ClampCurrentLODIndex();
		ResetAutomaticLODTransitionState();
		return;
	}

	if (PendingAutomaticLODIndex != StabilizedTargetLODIndex)
	{
		PendingAutomaticLODIndex = StabilizedTargetLODIndex;
		PendingAutomaticLODTimeSeconds = 0.0f;
		return;
	}

	PendingAutomaticLODTimeSeconds += std::max(0.0f, DeltaTime);
	if (PendingAutomaticLODTimeSeconds < SwitchDelay)
	{
		return;
	}

	CurrentLODIndex = PendingAutomaticLODIndex;
	ClampCurrentLODIndex();
	ResetAutomaticLODTransitionState();
}

void UParticleSystemComponent::DestroyEmitterInstances()
{
	for (FParticleEmitterInstance* Inst : EmitterInstances) delete Inst;
	EmitterInstances.clear();
	PendingEvents = {};
	ResetAutomaticLODTransitionState();
}

void UParticleSystemComponent::DispatchEventsToReceivers()
{
	if (PendingEvents.Spawn.empty() &&
		PendingEvents.Death.empty() &&
		PendingEvents.Collision.empty() &&
		PendingEvents.Burst.empty())
	{
		return;
	}

	// PendingEvents는 이번 PSC tick에서 모은 이벤트 snapshot이다.
	// Receiver가 여기서 새 파티클을 spawn해도 그 spawn event를 같은 프레임에
	// 다시 receiver로 보내지 않으므로 recursive event 폭주를 피할 수 있다.
	for (FParticleEmitterInstance* Inst : EmitterInstances)
	{
		if (!Inst)
		{
			continue;
		}

		Inst->HandleReceivedEvents(
			PendingEvents.Spawn,
			PendingEvents.Death,
			PendingEvents.Collision,
			PendingEvents.Burst);
	}
}

void UParticleSystemComponent::DispatchEventsToManager()
{
	AParticleEventManager* Manager = GetEventManager();
	if (!Manager)
	{
		const float WarningDelaySeconds = 0.5f;
		if (!bHasWarnedMissingEventManager &&
			MissingEventManagerTimeSeconds >= WarningDelaySeconds)
		{
			UE_LOG("[ParticleSystemComponent] No default ParticleEventManager is registered for this PSC. Particle playback/rendering can continue without it, but persistent missing-manager state means external particle events will not be delivered. This can be valid in preview/tool contexts; runtime gameplay that expects event delivery should register a manager.");
			bHasWarnedMissingEventManager = true;
		}

		// EventManager is optional for basic particle playback/rendering, but external
		// gameplay/event-delivery use cases expect runtime registration. We still drain
		// undelivered events in this phase instead of buffering or falling back locally.
		PendingEvents = {};
		return;
	}

	MissingEventManagerTimeSeconds = 0.0f;
	Manager->HandleParticleSpawnEvents    (this, PendingEvents.Spawn);
	Manager->HandleParticleDeathEvents    (this, PendingEvents.Death);
	Manager->HandleParticleCollisionEvents(this, PendingEvents.Collision);
	Manager->HandleParticleBurstEvents    (this, PendingEvents.Burst);
	PendingEvents = {};
}

void UParticleSystemComponent::SetCurrentLODIndex(int32 InLODIndex)
{
	CurrentLODIndex = InLODIndex;
	ClampCurrentLODIndex();
	ResetAutomaticLODTransitionState();
	ApplyCurrentLODToEmitterInstances();

	PushDynamicDataToProxy();
	MarkWorldBoundsDirty();
}

void UParticleSystemComponent::ClampCurrentLODIndex()
{
	if (CurrentLODIndex < 0)
	{
		CurrentLODIndex = 0;
	}

	if (!Template)
	{
		return;
	}

	Template->EnsureLODDistances();

	const int32 MaxLODCount = Template->GetMaxLODCount();
	if (MaxLODCount <= 0)
	{
		CurrentLODIndex = 0;
		return;
	}

	CurrentLODIndex = std::clamp(CurrentLODIndex, 0, MaxLODCount - 1);
}

void UParticleSystemComponent::ApplyCurrentLODToEmitterInstances()
{
	ClampCurrentLODIndex();

	// LOD selection is computed elsewhere (manual setter or stabilized automatic
	// selection), and this function only propagates the already-chosen index.
	for (FParticleEmitterInstance* Inst : EmitterInstances)
	{
		if (!Inst)
		{
			continue;
		}

		Inst->SetCurrentLODIndex(CurrentLODIndex);
	}
}

bool UParticleSystemComponent::IsSystemFinished() const
{
	if (EmitterInstances.empty())
	{
		return false;
	}

	for (const FParticleEmitterInstance* Inst : EmitterInstances)
	{
		if (!Inst || !Inst->IsFinished())
		{
			return false;
		}
	}

	return true;
}

void UParticleSystemComponent::PushDynamicDataToProxy()
{
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		FParticleSystemSceneProxy* ParticleProxy = static_cast<FParticleSystemSceneProxy*>(Proxy);
		ParticleProxy->SetDynamicData(BuildDynamicData());
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
}
