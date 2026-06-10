#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Singleton.h"
#include "Object/Ptr/ObjectPtr.h"

class UObject;

struct FGCReferenceEdge
{
	UObject* Object = nullptr;
	UObject* Referencer = nullptr;
	const char* ReferenceName = nullptr;
};

class FReferenceCollector
{
public:
	explicit FReferenceCollector(UObject* InReferencer = nullptr)
		: Referencer(InReferencer)
	{
	}

	void AddReferencedObject(UObject* Object, const char* ReferenceName = nullptr);

	template <typename T>
	void AddReferencedObject(const TObjectPtr<T>& ObjectPtr, const char* ReferenceName = nullptr)
	{
		AddReferencedObject(ObjectPtr.GetRaw(), ReferenceName);
	}

	template <typename T>
	void AddReferencedObjects(const TArray<T*>& Objects, const char* ReferenceName = nullptr)
	{
		for (T* Object : Objects)
		{
			AddReferencedObject(Object, ReferenceName);
		}
	}

	template <typename T>
	void AddReferencedObjects(const TArray<TObjectPtr<T>>& Objects, const char* ReferenceName = nullptr)
	{
		for (const TObjectPtr<T>& ObjectPtr : Objects)
		{
			AddReferencedObject(ObjectPtr.GetRaw(), ReferenceName);
		}
	}

private:
	friend class FGarbageCollector;
	friend class FScopedReferenceName;

	UObject* Referencer = nullptr;
	const char* CurrentReferenceName = nullptr;
	TArray<FGCReferenceEdge> Stack;
};

class FScopedReferenceName
{
public:
	FScopedReferenceName(FReferenceCollector& InCollector, const char* InReferenceName)
		: Collector(InCollector)
		, PreviousReferenceName(InCollector.CurrentReferenceName)
	{
		Collector.CurrentReferenceName = InReferenceName;
	}

	~FScopedReferenceName()
	{
		Collector.CurrentReferenceName = PreviousReferenceName;
	}

private:
	FReferenceCollector& Collector;
	const char* PreviousReferenceName = nullptr;
};

class FGCObject
{
public:
	FGCObject();
	virtual ~FGCObject();

	FGCObject(const FGCObject&) = delete;
	FGCObject& operator=(const FGCObject&) = delete;
	FGCObject(FGCObject&&) = delete;
	FGCObject& operator=(FGCObject&&) = delete;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) = 0;
	virtual const char* GetReferencerName() const { return "FGCObject"; }
};

class FGarbageCollector : public TSingleton<FGarbageCollector>
{
	friend class TSingleton<FGarbageCollector>;

public:
	// Full collection: mark roots, queue unreachable / pending-kill objects, then purge ready garbage.
	void CollectGarbage();
	// Diagnostic collection: mark roots and rebuild reference-chain data without destroying anything.
	void CollectGarbageMarkOnly();
	void RequestGarbageCollection();
	bool TryCollectGarbage();
	void PushCollectionBlock();
	void PopCollectionBlock();
	bool IsCollectionBlocked() const { return CollectionBlockDepth > 0; }
	void MarkObject(UObject* Object);
	void AddExternalRoot(FGCObject* Root);
	void RemoveExternalRoot(FGCObject* Root);
	bool GetLastReferenceChain(UObject* Object, TArray<FString>& OutChain) const;
	bool IsCollecting() const { return bIsCollecting; }
	const TArray<UObject*>& GetObjectsPendingPurge() const { return ObjectsPendingPurge; }

private:
	FGarbageCollector() = default;

	void BeginCollection();
	void MarkAllObjectsUnreachable();
	void MarkRoots();
	void MarkObjectFromEdge(const FGCReferenceEdge& Edge);
	void Sweep();
	void QueueGarbageObjects();
	void BeginDestroyQueuedObjects();
	void PurgeReadyGarbageObjects();
	bool IsExternalRootRegistered(FGCObject* Root) const;

private:
	TArray<FGCObject*> ExternalRoots;
	TArray<UObject*> ObjectsPendingPurge;
	TMap<UObject*, FGCReferenceEdge> LastReferenceEdges;
	bool bIsCollecting = false;
	bool bCollectionRequested = false;
	int32 CollectionBlockDepth = 0;
};

class FScopedGarbageCollectionBlocker
{
public:
	FScopedGarbageCollectionBlocker() { FGarbageCollector::Get().PushCollectionBlock(); }
	~FScopedGarbageCollectionBlocker() { FGarbageCollector::Get().PopCollectionBlock(); }

	FScopedGarbageCollectionBlocker(const FScopedGarbageCollectionBlocker&) = delete;
	FScopedGarbageCollectionBlocker& operator=(const FScopedGarbageCollectionBlocker&) = delete;
};
