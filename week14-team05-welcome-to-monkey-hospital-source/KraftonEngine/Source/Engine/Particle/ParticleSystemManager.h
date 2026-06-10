#pragma once
#include "Object/GarbageCollection.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Core/Types/CoreTypes.h"
#include "Core/Singleton.h"
#include "ParticleSystem.h"
#include "Asset/AssetRegistry.h"

class AParticleEventManager;

class FParticleSystemManager : public TSingleton<FParticleSystemManager>, public FGCObject
{
	friend class TSingleton<FParticleSystemManager>;

public:
	UParticleSystem* Load(const FString& Path);
	UParticleSystem* Find(const FString& Path) const;
	bool Save(UParticleSystem* Asset);

	void RefreshAvailableParticleSystems();
	const TArray<FAssetListItem>& GetAvailableParticleSystemFiles() const;

	// Higher-level runtime/bootstrap code registers the current default event manager here.
	// Current policy is a single default manager. This manager is non-owning and
	// exposed for PSC injection. Basic particle playback/rendering does not require it,
	// but runtime gameplay that expects external particle event delivery should register one.
	// nullptr is a valid "not registered yet" state.
	void SetDefaultEventManager(AParticleEventManager* InManager);
	AParticleEventManager* GetDefaultEventManager() const;

	const char* GetReferencerName() const override { return "FParticleSystemManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void ClearCache();

private:
	TMap<FString, UParticleSystem*> LoadedParticleSystems;
	TArray<FAssetListItem> AvailableParticleSystemFiles;

	// Non-owning reference provided by a higher-level runtime/bootstrap layer.
	// ParticleSystemManager does not create/destroy it; PSC consumes this provider state.
	TWeakObjectPtr<AParticleEventManager> DefaultEventManager;
};
