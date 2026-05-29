#pragma once

#include "Core/EngineTypes.h"
#include "Object/Object.h"
#include "Particle/ParticleEmitterTypes.h"
#include "ParticleEmitter.generated.h"

class UParticleLODLevel;
class UParticleModule;
class UParticleModuleTypeDataBase;
class UMaterialInterface;

UCLASS()
class UParticleEmitter : public UObject
{
public:
	GENERATED_BODY(UParticleEmitter)

	FName EmitterName;

	TArray<UParticleLODLevel*> LODLevels;

	int32 PeakActiveParticles = 0;
	int32 InitialAllocationCount = 0;

	TMap<UParticleModule*, uint32> ModuleOffsetMap;
	TMap<UParticleModule*, uint32> ModuleInstanceOffsetMap;

	int32 ParticleSize = 0;
	int32 ReqInstanceBytes = 0; //Instance당 Byte
	int32 TypeDataOffset = 0;
	int32 TypeDataInstanceOffset = -1;

	TArray<UParticleModule*> ModulesNeedingInstanceData;

	virtual void ClassifyModulesByRole();
	virtual void SetEmitterName(FName Name) { EmitterName = Name; }
	virtual FName& GetEmitterName() { return EmitterName; }
	virtual void SetLODCount(int32 LODCount);
	virtual void SyncLODLevelsToSystemCount(int32 LODCount);
	virtual UParticleLODLevel* GetLODLevel(int32 LODLevel);
	virtual UParticleLODLevel* GetBestLODLevel(int32 LODLevel) const;
	virtual bool CalculateMaxActiveParticleCount();
	void CalculateTypeParticleSizeAndOffsets(UParticleModuleTypeDataBase* HighTypeData);
	bool CalculatePerParticleSizeAndOffsets(UParticleModuleTypeDataBase* HighTypeData, int32 ModuleIdx,
	                                        UParticleModule* ParticleModule);
	void CalculatePerInstanceParticleSizeAndOffset(int32 ModuleIdx, UParticleModule* ParticleModule,
	                                               int32 TempInstanceBytes);
	virtual void Build() {}
	virtual void CacheEmitterModuleInfo();
	virtual bool HasAnyEnabledLODs() const;

private:
	UParticleLODLevel* DuplicateLODLevelForEmitter(UParticleLODLevel* Source, int32 NewLevelIndex);
};
