#pragma once

#include "Object/Object.h"
#include "ParticleLODLevel.generated.h"

class UParticleModule;
class UParticleModuleEventGenerator;
class UParticleModuleEventReceiverBase;
class UParticleModuleRequired;
class UParticleModuleSpawn;
class UParticleModuleSpawnBase;
class UParticleModuleTypeDataBase;

UCLASS()
class UParticleLODLevel : public UObject
{
public:
	GENERATED_BODY(UParticleLODLevel)

	int32 Level = 0;
	uint32 bEnabled : 1 = false;

	UParticleModuleRequired* RequiredModule = nullptr;
	TArray<UParticleModule*> Modules;

	UParticleModuleTypeDataBase* TypeDataModule = nullptr;
	UParticleModuleSpawn* SpawnModule = nullptr;
	UParticleModuleEventGenerator* EventGenerator = nullptr;

	TArray<UParticleModuleSpawnBase*> SpawningModules;
	TArray<UParticleModule*> OnSpawnModules;
	TArray<UParticleModule*> UpdateModules;
	TArray<UParticleModule*> FinalUpdateModules;
	TArray<UParticleModuleEventReceiverBase*> EventReceiverModules;

	uint32 ConvertedModules : 1 = false;
	int32 PeakActiveParticles = 0; //예상 최대 particle값

	virtual void ClassifyModulesByRole();
	virtual int32 CalculateMaxActiveParticleCount();
	int32 GetModuleIndex(UParticleModule* InModule);
	UParticleModule* GetModuleAtIndex(int32 InIndex);
	virtual void SetLevelIndex(int32 InLevelIndex);
};
