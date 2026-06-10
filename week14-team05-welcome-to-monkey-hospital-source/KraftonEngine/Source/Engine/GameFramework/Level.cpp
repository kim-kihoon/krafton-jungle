#include "GameFramework/Level.h"
#include "Object/Reflection/ObjectFactory.h"
#include <GameFramework/World.h>
#include "Object/GarbageCollection.h"
#include <algorithm>

ULevel::ULevel(UWorld* OwingWorld)
{
	SetWorld(OwingWorld);
	Actors.clear();
}

ULevel::ULevel(const TArray<AActor*>& InActors, UWorld* World)
	: Actors(InActors)
{
	SetWorld(World);
}

ULevel::~ULevel()
{
	if (!HasAnyFlags(RF_BeginDestroy))
	{
		BeginDestroy();
	}
}

void ULevel::BeginDestroy()
{
	if (HasAnyFlags(RF_BeginDestroy))
	{
		return;
	}

	RouteLevelDestroyed();
	SetOuter(nullptr);
	UObject::BeginDestroy();
}

void ULevel::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);
	Collector.AddReferencedObjects(Actors, "Actors");
}


void ULevel::AddActor(AActor* Actor)
{
	if (!Actor) return;

	auto It = std::find(Actors.begin(), Actors.end(), Actor);
	if (It != Actors.end())
	{
		return;
	}
	
	Actor->SetOuter(this);
	Actors.push_back(Actor);
}

void ULevel::RemoveActor(AActor* Actor)
{
	if (!Actor) return;

	auto It = std::find(Actors.begin(), Actors.end(), Actor);
	if (It == Actors.end())
	{
		return;
	}

	Actors.erase(It);
}

void ULevel::Clear()
{
	// Clear is a destructive level operation. Do not orphan actors by only nulling
	// Outer; route actor/component teardown so delegates, render/physics state, and
	// GC pending-kill flags are all consistent.
	RouteLevelDestroyed();
}

void ULevel::Tick(float DeltaTime)
{
	FScopedGarbageCollectionBlocker GCBlocker;

	const TArray<AActor*> ActorSnapshot = Actors;
	for (AActor* Actor : ActorSnapshot)
	{
		if (IsValid(Actor))
		{
			Actor->Tick(DeltaTime);
		}
	}
}

void ULevel::BeginPlay()
{
	FScopedGarbageCollectionBlocker GCBlocker;
	const TArray<AActor*> ActorSnapshot = Actors;
	for (AActor* Actor : ActorSnapshot)
	{
		if (IsValid(Actor) && !Actor->HasActorBegunPlay())
		{
			Actor->BeginPlay();
		}
	}
}

void ULevel::EndPlay()
{
	FScopedGarbageCollectionBlocker GCBlocker;
	const TArray<AActor*> ActorSnapshot = Actors;
	for (AActor* Actor : ActorSnapshot)
	{
		if (IsValid(Actor))
		{
			Actor->EndPlay();
		}
	}
}

void ULevel::RouteLevelDestroyed()
{
	if (bLevelDestroyRouted)
	{
		return;
	}

	bLevelDestroyRouted = true;
	EndPlay();

	TArray<AActor*> ActorsToDestroy;
	ActorsToDestroy.reserve(Actors.size());
	for (AActor* Actor : Actors)
	{
		if (IsAliveObject(Actor))
		{
			ActorsToDestroy.push_back(Actor);
		}
	}

	for (AActor* Actor : ActorsToDestroy)
	{
		if (!IsAliveObject(Actor))
		{
			continue;
		}

		Actor->RouteActorDestroyed();
		Actor->MarkPendingKill();
	}

	Actors.clear();
	OwingWorld.Reset();
	MarkPendingKill();
}
