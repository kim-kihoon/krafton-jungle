#pragma once
#include "Object/Object.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Source/Engine/GameFramework/Level.generated.h"
#include <memory>

class AActor;
class UWorld;
class FSpatialPartition;

UCLASS()
class ULevel :
    public UObject
{
public:
	GENERATED_BODY()
	ULevel() = default;
	ULevel(UWorld* OwingWorld);
	ULevel(const TArray<AActor*>& Actors, UWorld* OwingWorld);
	~ULevel();
	void BeginDestroy() override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	void AddActor(AActor* Actor);
	void RemoveActor(AActor* Actor);
	void Clear();

	const TArray<AActor*>& GetActors() const { return Actors; }
	UWorld* GetWorld() const { return OwingWorld.Get(); }
	void SetWorld(UWorld* World) { OwingWorld.Reset(World);}

	void BeginPlay();
	void EndPlay();
	void RouteLevelDestroyed();
	void Tick(float DeltaTime);
private:
	FName LevelName;
	TArray<AActor*> Actors;
	TWeakObjectPtr<UWorld> OwingWorld;
	bool bLevelDestroyRouted = false;
};

