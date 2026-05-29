#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Particle/ParticleHelper.h"
#include "Render/Particle/ParticleDynamicData.h"

class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class UParticleSystemComponent;


// General event instance payload
struct FParticleEventInstancePayload;


struct FParticleEmitterInstanceFixLayout
{
	virtual ~FParticleEmitterInstanceFixLayout() = default;
};

struct FParticleEmitterInstance : FParticleEmitterInstanceFixLayout
{
	UParticleEmitter* EmitterTemplate = nullptr;

	// Owner
	UParticleSystemComponent* Component = nullptr;
	int32 EmitterIndex = -1;

	int32 CurrentLODLevelIndex = 0;
	UParticleLODLevel* CurrentLODLevel = nullptr;

	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;
	uint8* InstanceData = nullptr;
	int32 InstancePayloadSize = 0;
	int32 TypeDataOffset = 0;
	int32 TypeDataInstanceOffset = -1;
	int32 PayloadOffset = 0;
	int32 ParticleSize = 0;
	int32 ParticleStride = 0;
	int32 ActiveParticles = 0;
	uint32 ParticleCounter = 0;
	int32 MaxActiveParticles = 0;
	float SpawnFraction = 0.0f;
	float SecondsSinceCreation = 0.0f;
	float EmitterTime = 0.0f;
	float LastDeltaTime = 0.0f;
	FVector Location = FVector::ZeroVector;
	FVector OldLocation = FVector::ZeroVector;
	TArray<uint8> BurstFired;

	FParticleDataContainer DataContainer;

	FParticleEmitterInstance() = default;
	explicit FParticleEmitterInstance(UParticleSystemComponent* InComponent);
	virtual ~FParticleEmitterInstance();

	virtual void InitParameters(UParticleEmitter* InTemplate);
	virtual void RebuildTemplateModuleList();
	virtual bool Resize(int32 NewMaxActiveParticles, bool bSetMaxActiveCount = true);
	virtual void SetCurrentLODLevel(int32 LODLevel);
	virtual void Tick(float DeltaTime, int32 LODLevel, bool bSuppressSpawning);
	virtual void ResetParticleParameters(float DeltaTime);
	virtual void Tick_ModuleUpdate(float DeltaTime, UParticleLODLevel* CurrentLODLevel);
	virtual void Tick_ModulePostUpdate(float DeltaTime, UParticleLODLevel* CurrentLODLevel);
	virtual void Tick_ModuleFinalUpdate(float DeltaTime, UParticleLODLevel* CurrentLODLevel);
	virtual float Tick_SpawnParticles(float DeltaTime, UParticleLODLevel* CurrentLODLevel, bool bSuppressSpawning, bool bFirstTime);
	virtual float Spawn(float DeltaTime);
	virtual void ResetBurstList();
	virtual int32 GetCurrentBurstCount(float DeltaTime);
	virtual void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation,
		const FVector& InitialVelocity, FParticleEventInstancePayload* EventPayload);
	virtual void KillParticle(int32 Index);
	virtual void KillParticles();
	virtual void UpdateParticles(float DeltaTime);
	virtual FDynamicEmitterReplayDataBase* GetReplayData();
	virtual bool FillReplayData(FDynamicEmitterReplayDataBase& OutData);

	// Payload calculating and accessing functions
	virtual uint32 RequiredBytes();
	virtual uint32 GetModuleDataOffset(UParticleModule* Module);
	virtual uint8* GetModuleInstanceData(UParticleModule* Module);
	virtual uint8* GetTypeDataModuleInstanceData();
	virtual uint32 CalculateParticleStride(uint32 InParticleSize);

	FBaseParticle* GetParticleDirect(int32 DirectIndex) const;
	virtual void AddCollisionEvent(const FBaseParticle& Particle, uint16 DirectIndex, const FVector& HitLocation,
		const FVector& HitNormal, float HitTime, bool bParticleWasKilled);

protected:
	// Spawning and updating functions
	virtual void PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation, const FVector& InitialVelocity);
	virtual void PostSpawn(FBaseParticle* Particle, float Interp, float SpawnTime);
};
