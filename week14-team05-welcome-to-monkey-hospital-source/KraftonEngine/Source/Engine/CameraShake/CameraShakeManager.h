#pragma once
#include "Object/GarbageCollection.h"
#include "Core/Types/CoreTypes.h"
#include "Core/Singleton.h"

class UCameraShakeAsset;

class FCameraShakeManager : public TSingleton<FCameraShakeManager>, public FGCObject
{
	friend class TSingleton<FCameraShakeManager>;

public:
	UCameraShakeAsset* Load(const FString& Path);
	UCameraShakeAsset* Find(const FString& Path) const;

	bool Save(UCameraShakeAsset* Asset);

	const char* GetReferencerName() const override { return "FCameraShakeManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void ClearCache();

private:
	TMap<FString, UCameraShakeAsset*> LoadedShakes;
};
