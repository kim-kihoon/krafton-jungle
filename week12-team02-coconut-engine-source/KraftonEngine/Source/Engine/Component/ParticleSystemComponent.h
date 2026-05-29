#pragma once

#include "Component/PrimitiveComponent.h"
#include "Core/Delegate.h"
#include "Core/UObject/TSoftObjectPtr.h"
#include "Particle/ParticleEventCollideData.h"
#include "Particle/ParticleLODContext.h"
#include "Particle/ParticleSystem.h"
#include "ParticleSystemComponent.generated.h"

class UParticleSystemComponent;
class AParticleEventManager;
class UFXSystemAsset;
class FParticleSystemSceneProxy;
struct FParticleEmitterInstance;

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FParticleCollideSignature,
	UParticleSystemComponent* /*ParticleSystemComponent*/,
	const FParticleEventCollideData& /*EventData*/
);

UCLASS(HiddenInComponentList)
class UFXSystemComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY(UFXSystemComponent)

	virtual UFXSystemAsset* GetFXSystemAsset() const { return nullptr; }
};

UCLASS()
class UParticleSystemComponent : public UFXSystemComponent
{
public:
	GENERATED_BODY(UParticleSystemComponent)
	~UParticleSystemComponent() override;

	//Important Logics
	void BeginPlay() override;
	void PostDuplicate() override;
	virtual void InitParticles();
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	
	
	void EndPlay() override;
	void PostEditProperty(const char* PropertyName) override;
	void ResetParticles(bool bEmptyInstances = false);
	//Related To Rendering
	FParticleSystemSceneProxy* GetSceneProxy() const;
	FPrimitiveSceneProxy* CreateSceneProxy() override;
	//Related To LOD 
	int32 DecideLODLevel(const FParticleLODContext& Context) const;
	void SetForcedLODLevel(int32 InLODLevel);
	void ClearForcedLODLevel();
	void BuildInstances(UParticleSystem* ParticleSystemTemplate);

	//Related To Collision
	void QueueParticleCollisionEvent(const FParticleEventCollideData& EventData);
	void ReportEventSpawn(FName InEventName, float InEmitterTime, const FVector& InLocation, const FVector& InVelocity);
	void ReportEventDeath(FName InEventName, float InEmitterTime, const FVector& InLocation, const FVector& InVelocity,
		float InParticleTime, const FVector& InDirection);
	void ReportEventCollision(FName InEventName, float InEmitterTime, const FVector& InLocation, const FVector& InDirection,
		const FVector& InVelocity, float InParticleTime, const FVector& InNormal, float InHitTime);
	void ReportEventBurst(FName InEventName, float InEmitterTime, int32 InParticleCount, const FVector& InLocation);
	void DispatchParticleCollisionEvents();
	void ClearParticleCollisionEvents();
	void ProcessParticleEventReceivers(float DeltaTime);
	void DispatchParticleEvents();
	void ClearParticleEvents();
	
	//Wrapper
	void InitializeSystem();

	//Getter/Setter
	UFXSystemAsset* GetFXSystemAsset() const override;
	void SetTemplate(UParticleSystem* NewTemplate);
	UParticleSystem* ResolveTemplate();
	
	
	UPROPERTY(Edit, Category="Particles", DisplayName="Template", Type=SoftObject, Class=UParticleSystem)
	TSoftObjectPtr<UParticleSystem> Template;

	UPROPERTY(Edit, Category="Particles", DisplayName="Particle System Priority", Min=0, Max=65535, Speed=1.0f)
	int32 SortPriority = 0;

	UPROPERTY(Edit, Category="Particles", DisplayName="Show Particles")
	bool bShowParticles = true;

	TArray<FParticleEventCollideData> ParticleEventCollideDatas;
	TArray<FParticleEventSpawnData> SpawnEvents;
	TArray<FParticleEventDeathData> DeathEvents;
	TArray<FParticleEventCollideData> CollisionEvents;
	TArray<FParticleEventBurstData> BurstEvents;
	FParticleCollideSignature OnParticleCollide;
	int32 MaxParticleCollisionEventsPerFrame = 256;
	bool bDispatchingParticleCollisionEvents = false;
	
	TArray<FParticleEmitterInstance*> EmitterInstances;
	int32 LODLevel = 0;
	int32 ForcedLODLevel = -1;
	TArray<float> LODDistances;
};
