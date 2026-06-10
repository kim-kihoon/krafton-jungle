#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/GarbageCollection.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include <algorithm>

class AActor;
class UActorComponent;
class UObject;
class USceneComponent;
class UStruct;
class UGizmoComponent;
class UWorld;

struct FSelectionDetailTarget
{
	UObject* ObjectPtr = nullptr;
	UStruct* StructType = nullptr;
	void* ContainerPtr = nullptr;

	static FSelectionDetailTarget FromObject(UObject* Object);

	void Reset();
	bool HasTarget() const;
	bool IsValidTarget() const;
};

class FSelectionManager : public FGCObject
{
public:
	void Init();
	void Shutdown();

	void Select(AActor* Actor);
	void SelectRange(AActor* ClickedActor, const TArray<AActor*>& ActorList);
	void ToggleSelect(AActor* Actor);
	void Deselect(AActor* Actor);
	void ClearSelection();
	int32 DeleteSelectedActors();
	void Tick();

	void SelectComponent(USceneComponent* Component);
	USceneComponent* GetSelectedComponent() const;
	void SelectActorDetails(AActor* Actor);
	void SelectActorComponent(UActorComponent* Component);
	UActorComponent* GetSelectedActorComponent() const;
	bool IsComponentDetailsSelected() const;
	const FSelectionDetailTarget* GetPrimaryDetailTarget() const;
	const TArray<FSelectionDetailTarget>& GetSelectedDetailTargets() const { return SelectedDetailTargets; }
	void SetSingleDetailTarget(const FSelectionDetailTarget& Target);

	bool IsSelected(AActor* Actor) const;

	AActor* GetPrimarySelection() const;

	TArray<AActor*> GetSelectedActors() const;
	bool IsEmpty() const { return GetSelectedActors().empty() && GetSelectedComponent() == nullptr; }

	UGizmoComponent* GetGizmo() const;

	void SetGizmoEnabled(bool bEnabled);
	void SetWorld(UWorld* InWorld);
	const char* GetReferencerName() const override { return "FSelectionManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	void PruneInvalidSelection();
	void SyncGizmo();
	void AddActorDetailTarget(AActor* Actor);
	void SetActorProxiesSelected(AActor* Actor, bool bSelected);
	void RefreshSelectedActorCache();
	void RefreshDetailTargetsFromActors();

	TArray<TWeakObjectPtr<AActor>> SelectedActors;
	TArray<AActor*> SelectedActorCache;
	TArray<FSelectionDetailTarget> SelectedDetailTargets;
	TWeakObjectPtr<USceneComponent> SelectedComponent;
	UGizmoComponent* Gizmo = nullptr;
	TWeakObjectPtr<UWorld> World;
	bool bGizmoEnabled = true;
};
