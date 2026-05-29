#include "Component/ParticleSystemComponent.h"

#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleEmitterInstances.h"
#include "Particle/ParticleModule.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleEventManager.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleBeamInstances.h"
#include "Render/Particle/ParticleDynamicData.h"
#include "Render/Proxy/ParticleSystemSceneProxy.h"
#include "Particle/ParticleSystemManager.h"
#include "Profiling/ParticleStats.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "GameFramework/World.h"
#include "Particle/ParticleLODContext.h"
#include "Particle/RibbonEmitterInstance.h"

namespace
{
void MoveReplayDataBase(FDynamicEmitterReplayDataBase& Dest, FDynamicEmitterReplayDataBase& Source)
{
	Dest.eEmitterType = Source.eEmitterType;
	Dest.ActiveParticleCount = Source.ActiveParticleCount;
	Dest.ParticleStride = Source.ParticleStride;
	Dest.DataContainer = std::move(Source.DataContainer);
	Dest.Scale = Source.Scale;
	Dest.SortMode = Source.SortMode;
	Dest.EmitterSortPriority = Source.EmitterSortPriority;
}

void MoveRenderableReplayData(FDynamicRenderableEmitterReplayDataBase& Dest, FDynamicRenderableEmitterReplayDataBase& Source)
{
	MoveReplayDataBase(Dest, Source);
	Dest.MaterialInterface = Source.MaterialInterface;
	Dest.BlendMode = Source.BlendMode;
}

void MoveSpriteReplayData(FDynamicSpriteEmitterReplayData& Dest, FDynamicSpriteEmitterReplayData& Source)
{
	MoveRenderableReplayData(Dest, Source);
	Dest.SubImages_Horizontal = Source.SubImages_Horizontal;
	Dest.SubImages_Vertical = Source.SubImages_Vertical;
	Dest.ScreenAlignment = Source.ScreenAlignment;
	Dest.EmitterOrigin = Source.EmitterOrigin;
	Dest.AlphaSource = Source.AlphaSource;
	Dest.AlphaThreshold = Source.AlphaThreshold;
	Dest.AlphaPower = Source.AlphaPower;
	Dest.ColorIntensity = Source.ColorIntensity;
}

void MoveMeshReplayData(FDynamicMeshEmitterReplayData& Dest, FDynamicMeshEmitterReplayData& Source)
{
	MoveRenderableReplayData(Dest, Source);
	Dest.LODLevel = Source.LODLevel;
	Dest.StaticMesh = Source.StaticMesh;
}

void MoveBeamReplayData(FDynamicBeamEmitterReplayData& Dest, FDynamicBeamEmitterReplayData& Source)
{
	MoveRenderableReplayData(Dest, Source);
	Dest.Beams = std::move(Source.Beams);
	Dest.InterpolationPoints = Source.InterpolationPoints;
	Dest.Sheets = Source.Sheets;
	Dest.LogicalBeamCount = Source.LogicalBeamCount;
	Dest.MaxBeamCount = Source.MaxBeamCount;
	Dest.UpVectorStepSize = Source.UpVectorStepSize;
	Dest.TextureTile = Source.TextureTile;
	Dest.TextureTileDistance = Source.TextureTileDistance;
	Dest.bRenderGeometry = Source.bRenderGeometry;
	Dest.bRenderDirectLine = Source.bRenderDirectLine;
	Dest.bRenderLines = Source.bRenderLines;
	Dest.bRenderTessellation = Source.bRenderTessellation;
	Dest.BranchParentName = Source.BranchParentName;
	Dest.TargetData = std::move(Source.TargetData);
}

void MoveRibbonReplayData(FDynamicRibbonEmitterReplayData& Dest, FDynamicRibbonEmitterReplayData& Source)
{
	MoveRenderableReplayData(Dest, Source);
	Dest.Points = std::move(Source.Points);
	Dest.Trails = std::move(Source.Trails);
	Dest.SheetsPerTrail = Source.SheetsPerTrail;
	Dest.MaxTessellationBetweenParticles = Source.MaxTessellationBetweenParticles;
	Dest.RenderAxisOption = Source.RenderAxisOption;
	Dest.SourceUpVector = Source.SourceUpVector;
	Dest.TilingDistance = Source.TilingDistance;
	Dest.DistanceTessellationStepSize = Source.DistanceTessellationStepSize;
	Dest.bRenderGeometry = Source.bRenderGeometry;
	Dest.bRenderSpawnPoints = Source.bRenderSpawnPoints;
	Dest.bRenderTangents = Source.bRenderTangents;
	Dest.bRenderTessellation = Source.bRenderTessellation;
}

FParticleEmitterInstance* CreateEmitterInstance(
	UParticleSystemComponent* Component,
	UParticleEmitter* Emitter)
{
	if (!Emitter)
	{
		return nullptr;
	}

	Emitter->ClassifyModulesByRole();
	UParticleLODLevel* LOD = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (LOD && LOD->TypeDataModule)
	{
		if (LOD->TypeDataModule->IsABeamEmitter()) {
			return new FParticleBeam2EmitterInstance(Component);
		}
		if (LOD->TypeDataModule->IsARibbonEmitter())
		{
			return new FRibbonEmitterInstance(Component);
		}
	}

	return new FParticleEmitterInstance(Component);
}

FDynamicEmitterDataBase* CreateDynamicEmitterData(int32 EmitterIndex, FDynamicEmitterReplayDataBase* ReplayData)
{
	if (!ReplayData)
	{
		return nullptr;
	}

	FDynamicEmitterDataBase* DynamicData = nullptr;

	if (ReplayData->eEmitterType == DET_Mesh)
	{
		FDynamicMeshEmitterData* MeshDynamicData = new FDynamicMeshEmitterData();
		MeshDynamicData->EmitterIndex = EmitterIndex;
		MoveMeshReplayData(MeshDynamicData->MeshSource, *static_cast<FDynamicMeshEmitterReplayData*>(ReplayData));
		DynamicData = MeshDynamicData;
	}
	else if (ReplayData->eEmitterType == DET_Beam2)
	{
		FDynamicBeamEmitterData* BeamDynamicData = new FDynamicBeamEmitterData();
		BeamDynamicData->EmitterIndex = EmitterIndex;
		MoveBeamReplayData(BeamDynamicData->BeamSource, *static_cast<FDynamicBeamEmitterReplayData*>(ReplayData));
		DynamicData = BeamDynamicData;
	}
	else if (ReplayData->eEmitterType == DET_Ribbon)
	{
		FDynamicRibbonEmitterData* RibbonDynamicData = new FDynamicRibbonEmitterData();
		RibbonDynamicData->EmitterIndex = EmitterIndex;
		MoveRibbonReplayData(RibbonDynamicData->RibbonSource, *static_cast<FDynamicRibbonEmitterReplayData*>(ReplayData));
		DynamicData = RibbonDynamicData;
	}
	else
	{
		FDynamicSpriteEmitterData* SpriteDynamicData = new FDynamicSpriteEmitterData();
		SpriteDynamicData->EmitterIndex = EmitterIndex;
		MoveSpriteReplayData(SpriteDynamicData->Source, *static_cast<FDynamicSpriteEmitterReplayData*>(ReplayData));
		DynamicData = SpriteDynamicData;
	}

	delete ReplayData;
	return DynamicData;
}

void DeleteDynamicEmitterData(TArray<FDynamicEmitterDataBase*>& DynamicData)
{
	for (FDynamicEmitterDataBase* EmitterData : DynamicData)
	{
		delete EmitterData;
	}
	DynamicData.clear();
}

uint16 ToTranslucencySortPriority(int32 SortPriority)
{
	return static_cast<uint16>(std::clamp(SortPriority, 0, 65535));
}
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	ResetParticles(true);
}

void UParticleSystemComponent::BeginPlay()
{
	UFXSystemComponent::BeginPlay();
	InitializeSystem();
}

void UParticleSystemComponent::PostDuplicate()
{
	UFXSystemComponent::PostDuplicate();
	ResolveTemplate();
}

void UParticleSystemComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (!PropertyName)
	{
		return;
	}

	if (std::strcmp(PropertyName, "Particle System Priority") == 0 || std::strcmp(PropertyName, "SortPriority") == 0)
	{
		FParticleSystemSceneProxy* ParticleSceneProxy = GetSceneProxy();
		if (ParticleSceneProxy)
		{
			ParticleSceneProxy->SetTranslucencySortPriority(ToTranslucencySortPriority(SortPriority));
		}
	}
	else if (std::strcmp(PropertyName, "Template") == 0)
	{
		ResetParticles(true);
		ResolveTemplate();
		InitializeSystem();
	}
	else if (std::strcmp(PropertyName, "Show Particles") == 0 || std::strcmp(PropertyName, "bShowParticles") == 0)
	{
		if (SceneProxy)
		{
			FParticleSystemSceneProxy* ParticleProxy = static_cast<FParticleSystemSceneProxy*>(SceneProxy);
			ParticleProxy->SetVisibility(bShowParticles);
		}
	}
}

void UParticleSystemComponent::EndPlay()
{
	ResetParticles(true);
	UFXSystemComponent::EndPlay();
}

UFXSystemAsset* UParticleSystemComponent::GetFXSystemAsset() const
{
	return Template.Get();
}

UParticleSystem* UParticleSystemComponent::ResolveTemplate()
{
	if (UParticleSystem* ParticleTemplate = Template.Get())
	{
		return ParticleTemplate;
	}

	const FString TemplatePath = Template.GetPath().ToString();
	if (TemplatePath.empty() || TemplatePath == "None")
	{
		return nullptr;
	}

	UParticleSystem* LoadedTemplate = FParticleSystemManager::Get().Load(TemplatePath);
	if (LoadedTemplate)
	{
		Template = LoadedTemplate;
	}
	return LoadedTemplate;
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* NewTemplate)
{
	if (Template.Get() == NewTemplate)
	{
		return;
	}

	ResetParticles(true);
	Template = NewTemplate;
	InitializeSystem();
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	FParticleSystemSceneProxy* ParticleSceneProxy = new FParticleSystemSceneProxy(this);
	ParticleSceneProxy->SetVisibility(bShowParticles);
	ParticleSceneProxy->SetTranslucencySortPriority(ToTranslucencySortPriority(SortPriority));
	return ParticleSceneProxy;
}

FParticleSystemSceneProxy* UParticleSystemComponent::GetSceneProxy() const
{
	return static_cast<FParticleSystemSceneProxy*>(SceneProxy);
}

void UParticleSystemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::ComponentTick);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ClearParticleEvents();

	UParticleSystem* ParticleTemplate = ResolveTemplate();
	if (!ParticleTemplate)
	{
		return;
	}

	if (EmitterInstances.empty())
	{
		InitParticles();
		//InitializeSystem();
	}

	if (ForcedLODLevel >= 0)
	{
		const int32 MaxLODIndex = LODDistances.empty() ? 0 : static_cast<int32>(LODDistances.size()) - 1;
		LODLevel = std::clamp(ForcedLODLevel, 0, MaxLODIndex);
	}
	else if (UWorld* World = GetWorld())
	{
		LODLevel = DecideLODLevel(World->GetParticleLODContext());
	}
	for (FParticleEmitterInstance* EmitterInstance : EmitterInstances)
	{
		if (EmitterInstance)
		{
			EmitterInstance->Tick(DeltaTime, LODLevel, false);
		}
	}

	ProcessParticleEventReceivers(DeltaTime);
	DispatchParticleEvents();
	FParticleStats::Get().RecordComponent(*this);

	TArray<FDynamicEmitterDataBase*> NewRenderData;
	NewRenderData.reserve(EmitterInstances.size());

	for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(EmitterInstances.size()); ++EmitterIndex)
	{
		FParticleEmitterInstance* EmitterInstance = EmitterInstances[EmitterIndex];
		if (!EmitterInstance)
		{
			continue;
		}

		FDynamicEmitterDataBase* DynamicData = nullptr;
		{
			PARTICLE_SCOPE_STAT(EParticleStatTimer::BuildRenderData);
			DynamicData = CreateDynamicEmitterData(EmitterIndex, EmitterInstance->GetReplayData());
		}
		if (DynamicData)
		{
			NewRenderData.push_back(DynamicData);
		}
	}

	FParticleSystemSceneProxy* ParticleSceneProxy = GetSceneProxy();
	if (!ParticleSceneProxy)
	{
		DeleteDynamicEmitterData(NewRenderData);
		return;
	}

	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::UpdateDynamicData);
		ParticleSceneProxy->UpdateDynamicData(std::move(NewRenderData));
	}
	{
		PARTICLE_SCOPE_STAT(EParticleStatTimer::UpdateMesh);
		ParticleSceneProxy->UpdateMesh();
	}
}

int32 UParticleSystemComponent::DecideLODLevel(const FParticleLODContext& Context) const
{
	if (!Context.bValid || LODDistances.empty())
	{
		return 0;
	}

	const float Distance = (Context.ViewPosition - GetWorldLocation()).Length();
	int32 SelectedLOD = 0;
	for (int32 Index = 0; Index < static_cast<int32>(LODDistances.size()); ++Index)
	{
		if (Distance < LODDistances[Index])
		{
			break;
		}
		SelectedLOD = Index;
	}

	return std::clamp(SelectedLOD, 0, static_cast<int32>(LODDistances.size()) - 1);
}

void UParticleSystemComponent::SetForcedLODLevel(int32 InLODLevel)
{
	ForcedLODLevel = std::max(0, InLODLevel);
	if (!LODDistances.empty())
	{
		LODLevel = std::clamp(ForcedLODLevel, 0, static_cast<int32>(LODDistances.size()) - 1);
	}
}

void UParticleSystemComponent::ClearForcedLODLevel()
{
	ForcedLODLevel = -1;
}

void UParticleSystemComponent::BuildInstances(UParticleSystem* ParticleSystemTemplate)
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::BuildInstances);
	for (int32 EmitterInstanceIdx = 0; EmitterInstanceIdx < static_cast<int32>(ParticleSystemTemplate->Emitters.size()); ++EmitterInstanceIdx)
	{
		//Particle System안의 Emitter
		UParticleEmitter* Emitter = ParticleSystemTemplate->Emitters[EmitterInstanceIdx];
		if (!Emitter)
		{
			EmitterInstances.push_back(nullptr);
			continue;
		}

		Emitter->CalculateMaxActiveParticleCount(); //최대 몇개의 Particle가질지 계산
		FParticleEmitterInstance* Instance = CreateEmitterInstance(this, Emitter); //TypeDataModule보고 알맞은 Emitter만든다
		Instance->EmitterIndex = EmitterInstanceIdx; //Component의 몇번째 EmitterInstance인지 가르키는 Idx
		Instance->InitParameters(Emitter); //Instance의 ParticleSize를 계산한다.
		Instance->SetCurrentLODLevel(LODLevel); //Instance의 LODLevel설정한다
		EmitterInstances.push_back(Instance);
	}
}

// 어떤 데이터가 바뀌나?
void UParticleSystemComponent::InitParticles()
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::InitParticles);
	ResetParticles(true);

	UParticleSystem* ParticleSystemTemplate = ResolveTemplate();
	if (!ParticleSystemTemplate)
	{
		return;
	}
	//ParticleSystem과 Emitter가 가지는 LODLevels의 갯수를 맞춘다
	ParticleSystemTemplate->NormalizeLODData();
	LODDistances = ParticleSystemTemplate->GetLODDistances();
	const int32 MaxLODIndex = LODDistances.empty() ? 0 : static_cast<int32>(LODDistances.size()) - 1;
	if (ForcedLODLevel >= 0)
	{
		LODLevel = std::clamp(ForcedLODLevel, 0, MaxLODIndex);
	}
	else
	{
		LODLevel = std::clamp(LODLevel, 0, MaxLODIndex);
	}
	
	EmitterInstances.reserve(ParticleSystemTemplate->Emitters.size());
	BuildInstances(ParticleSystemTemplate);//Particle System(원본)과 같은 크기로 Instance들을 만든다.

	LODDistances = ParticleSystemTemplate->GetLODDistances();
}

void UParticleSystemComponent::ResetParticles(bool bEmptyInstances)
{
	for (FParticleEmitterInstance* EmitterInstance : EmitterInstances)
	{
		delete EmitterInstance;
	}

	if (bEmptyInstances)
	{
		EmitterInstances.clear();
	}
	else
	{
		for (FParticleEmitterInstance*& EmitterInstance : EmitterInstances)
		{
			EmitterInstance = nullptr;
		}
	}
}

void UParticleSystemComponent::InitializeSystem()
{
	InitParticles();
}

void UParticleSystemComponent::QueueParticleCollisionEvent(const FParticleEventCollideData& EventData)
{
	if (MaxParticleCollisionEventsPerFrame >= 0
		&& static_cast<int32>(CollisionEvents.size()) >= MaxParticleCollisionEventsPerFrame)
	{
		return;
	}

	CollisionEvents.push_back(EventData);
}

void UParticleSystemComponent::ReportEventSpawn(FName InEventName, float InEmitterTime,
	const FVector& InLocation, const FVector& InVelocity)
{
	FParticleEventSpawnData EventData;
	EventData.EventName = InEventName;
	EventData.EmitterTime = InEmitterTime;
	EventData.Location = InLocation;
	EventData.Velocity = InVelocity;
	SpawnEvents.push_back(EventData);
}

void UParticleSystemComponent::ReportEventDeath(FName InEventName, float InEmitterTime,
	const FVector& InLocation, const FVector& InVelocity, float InParticleTime, const FVector& InDirection)
{
	FParticleEventDeathData EventData;
	EventData.EventName = InEventName;
	EventData.EmitterTime = InEmitterTime;
	EventData.Location = InLocation;
	EventData.Velocity = InVelocity;
	EventData.ParticleTime = InParticleTime;
	EventData.Direction = InDirection;
	DeathEvents.push_back(EventData);
}

void UParticleSystemComponent::ReportEventCollision(FName InEventName, float InEmitterTime,
	const FVector& InLocation, const FVector& InDirection, const FVector& InVelocity,
	float InParticleTime, const FVector& InNormal, float InHitTime)
{
	FParticleEventCollideData EventData;
	EventData.EventName = InEventName;
	EventData.EmitterTime = InEmitterTime;
	EventData.Location = InLocation;
	EventData.Direction = InDirection;
	EventData.Velocity = InVelocity;
	EventData.ParticleTime = InParticleTime;
	EventData.ParticleRelativeTime = InParticleTime;
	EventData.Normal = InNormal;
	EventData.HitTime = InHitTime;
	CollisionEvents.push_back(EventData);
}

void UParticleSystemComponent::ReportEventBurst(FName InEventName, float InEmitterTime,
	int32 InParticleCount, const FVector& InLocation)
{
	FParticleEventBurstData EventData;
	EventData.EventName = InEventName;
	EventData.EmitterTime = InEmitterTime;
	EventData.ParticleCount = InParticleCount;
	EventData.Location = InLocation;
	BurstEvents.push_back(EventData);
}

void UParticleSystemComponent::DispatchParticleCollisionEvents()
{
	if (bDispatchingParticleCollisionEvents || CollisionEvents.empty() || !OnParticleCollide.IsBound())
	{
		return;
	}

	bDispatchingParticleCollisionEvents = true;
	const TArray<FParticleEventCollideData> EventsToDispatch = CollisionEvents;
	for (const FParticleEventCollideData& EventData : EventsToDispatch)
	{
		OnParticleCollide.Broadcast(this, EventData);
	}
	bDispatchingParticleCollisionEvents = false;
}

void UParticleSystemComponent::ClearParticleCollisionEvents()
{
	CollisionEvents.clear();
}

void UParticleSystemComponent::ProcessParticleEventReceivers(float DeltaTime)
{
	const TArray<FParticleEventSpawnData> SpawnSnapshot = SpawnEvents;
	const TArray<FParticleEventDeathData> DeathSnapshot = DeathEvents;
	const TArray<FParticleEventCollideData> CollisionSnapshot = CollisionEvents;
	const TArray<FParticleEventBurstData> BurstSnapshot = BurstEvents;

	auto ProcessEvent = [this, DeltaTime](FParticleEventData& EventData)
	{
		for (FParticleEmitterInstance* EmitterInstance : EmitterInstances)
		{
			if (!EmitterInstance || !EmitterInstance->CurrentLODLevel)
			{
				continue;
			}

			for (UParticleModuleEventReceiverBase* Receiver : EmitterInstance->CurrentLODLevel->EventReceiverModules)
			{
				if (Receiver && Receiver->bEnabled && Receiver->WillProcessParticleEvent(EventData.Type))
				{
					Receiver->ProcessParticleEvent(EmitterInstance, EventData, DeltaTime);
				}
			}
		}
	};

	for (FParticleEventSpawnData EventData : SpawnSnapshot)
	{
		ProcessEvent(EventData);
	}
	for (FParticleEventDeathData EventData : DeathSnapshot)
	{
		ProcessEvent(EventData);
	}
	for (FParticleEventCollideData EventData : CollisionSnapshot)
	{
		ProcessEvent(EventData);
	}
	for (FParticleEventBurstData EventData : BurstSnapshot)
	{
		ProcessEvent(EventData);
	}
}

void UParticleSystemComponent::DispatchParticleEvents()
{
	DispatchParticleCollisionEvents();

	UWorld* World = GetWorld();
	AParticleEventManager* EventManager = World ? World->MyParticleEventManager : nullptr;
	if (!EventManager)
	{
		return;
	}

	if (!SpawnEvents.empty())
	{
		EventManager->HandleParticleSpawnEvents(this, SpawnEvents);
	}
	if (!DeathEvents.empty())
	{
		EventManager->HandleParticleDeathEvents(this, DeathEvents);
	}
	if (!CollisionEvents.empty())
	{
		EventManager->HandleParticleCollisionEvents(this, CollisionEvents);
	}
	if (!BurstEvents.empty())
	{
		EventManager->HandleParticleBurstEvents(this, BurstEvents);
	}
}

void UParticleSystemComponent::ClearParticleEvents()
{
	SpawnEvents.clear();
	DeathEvents.clear();
	ClearParticleCollisionEvents();
	BurstEvents.clear();
}
