#pragma once

#include "Core/EngineTypes.h"
#include "Core/CollisionTypes.h"
#include "Math/FloatCurve.h"
#include "Object/Object.h"
#include "Particle/ParticleEventCollideData.h"
#include "Particle/ParticleEmitterTypes.h"
#include "ParticleModule.generated.h"

struct FParticleEmitterInstance;
class UMaterialInterface;
class UParticleEmitter;
class UParticleModuleTypeDataBase;
class UStaticMesh;
class UParticleLODLevel;
struct FBaseParticle;
struct FParticleEventInstancePayload;

/** ModuleType
 *	Indicates the kind of emitter the module can be applied to.
 *	ie, EPMT_Beam - only applies to beam emitters.
 *
 *	The TypeData field is present to speed up finding the TypeData module.
 */
UENUM()
enum EModuleType : int
{
	EPMT_General,
	EPMT_TypeData,
	EPMT_Beam,
	EPMT_Trail,
	EPMT_Spawn,
	EPMT_Required,
	EPMT_Event,
	EPMT_Light,
	EPMT_SubUV,
	EPMT_MAX
};

UENUM()
enum EParticleSortMode : int
{
	PSORTMODE_None,
	PSORTMODE_ViewProjDepth,
	PSORTMODE_DistanceToView,
	PSORTMODE_Age_OldestFirst,
	PSORTMODE_Age_NewestFirst,
	PSORTMODE_MAX
};

UENUM()
enum EBeamTangentMethod : int
{
	PEBTANM_Direct,
	PEBTANM_UserSet,
	PEBTANM_MAX
};

UENUM()
enum class EParticleCollisionResponseMode : uint8
{
	Bounce = 0,
	Stop = 1,
	Kill = 2,
};

enum class EParticleDistributionMode : uint8
{
	Constant,
	Uniform,
	ConstantCurve,
	UniformCurve,
};

struct FParticleDistributionFloat
{
	EParticleDistributionMode Mode = EParticleDistributionMode::Constant;
	float Constant = 0.0f;
	float Min = 0.0f;
	float Max = 0.0f;
	FFloatCurve ConstantCurve;
	FFloatCurve MinCurve;
	FFloatCurve MaxCurve;

	void SetConstant(float Value);
	void SetUniform(float InMin, float InMax);
	void SetConstantCurve(float Time0, float Value0, float Time1, float Value1);
	void SetUniformCurve(float Time0, float Min0, float Max0, float Time1, float Min1, float Max1);
	float Evaluate(float Time) const;
	float EvaluateRandom(float Time) const;
	float GetMaxValue() const;
	bool UsesCurve() const;
	FFloatCurve* GetCurve(bool bMaxCurve = false);
	const FFloatCurve* GetCurve(bool bMaxCurve = false) const;
};

struct FParticleDistributionVector
{
	FParticleDistributionFloat X;
	FParticleDistributionFloat Y;
	FParticleDistributionFloat Z;

	void SetConstant(const FVector& Value);
	void SetUniform(const FVector& MinValue, const FVector& MaxValue);
	void SetConstantCurve(float Time0, const FVector& Value0, float Time1, const FVector& Value1);
	void SetUniformCurve(float Time0, const FVector& Min0, const FVector& Max0, float Time1, const FVector& Min1, const FVector& Max1);
	FVector Evaluate(float Time) const;
	FVector EvaluateRandom(float Time) const;
	FVector GetMaxValue() const;
	bool UsesCurve() const;
	FParticleDistributionFloat* GetChannel(int32 ChannelIndex);
	const FParticleDistributionFloat* GetChannel(int32 ChannelIndex) const;
};

UCLASS()
class UParticleModule : public UObject
{
public:
	GENERATED_BODY(UParticleModule)

	uint8 bSpawnModule : 1 = false;
	uint8 bUpdateModule : 1 = false;
	uint8 bFinalUpdateModule : 1 = false;
	uint8 bEnabled : 1 = true;
	uint8 bEditable : 1 = false;
	uint8 LODValidity = 0xff;

	struct FContext
	{
		FParticleEmitterInstance& Owner;
		FContext(FParticleEmitterInstance& InOwner) : Owner(InOwner) {}
	};

	struct FSpawnContext : FContext
	{
		int32 Offset;
		float SpawnTime;
		FBaseParticle* ParticleBase;

		FSpawnContext(FParticleEmitterInstance& InOwner, int32 InOffset, float InSpawnTime, FBaseParticle* InParticleBase)
			: FContext(InOwner), Offset(InOffset), SpawnTime(InSpawnTime), ParticleBase(InParticleBase)
		{
		}
	};

	struct FUpdateContext : FContext
	{
		int32 Offset;
		float DeltaTime;

		FUpdateContext(FParticleEmitterInstance& InOwner, int32 InOffset, float InDeltaTime)
			: FContext(InOwner), Offset(InOffset), DeltaTime(InDeltaTime)
		{
		}
	};

	virtual void Spawn(const FSpawnContext& Context) { (void)Context; }
	virtual void Update(const FUpdateContext& Context) { (void)Context; }
	virtual void FinalUpdate(const FUpdateContext& Context) { (void)Context; }
	virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) { (void)TypeData; return 0; }
	virtual uint32 RequiredBytesPerInstance() { return 0; }
	virtual uint32 PrepPerInstanceBlock(FParticleEmitterInstance* Owner, void* InstData) { (void)Owner; (void)InstData; return 0; }
	virtual void SetToSensibleDefaults(UParticleEmitter* Owner) { (void)Owner; }
	virtual EModuleType GetModuleType() const { return EPMT_General; }
	virtual bool IsOnSpawnModule() const { return bSpawnModule != 0; }
	virtual bool IsUpdateModule() const { return bUpdateModule != 0; }
	virtual bool IsSizeMultiplyLife() { return false; }
	virtual bool TouchesMeshRotation() const { return false; }
	virtual UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const;

protected:
	void CopyModuleBaseTo(UParticleModule* Copy) const;
};

UCLASS()
class UParticleModuleEventBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleEventBase)

	EModuleType GetModuleType() const override { return EPMT_Event; }
};

struct FParticleEvent_GenerateInfo
{
	EParticleEventType Type = EPET_Any;
	FName CustomName = FName::None;
	int32 Frequency = 0;
	int32 ParticleFrequency = 0;
	bool FirstTimeOnly = false;
	bool LastTimeOnly = false;
	bool UseReflectedImpactVector = false;
	bool bUseOrbitOffset = false;
};

UCLASS()
class UParticleModuleEventGenerator : public UParticleModuleEventBase
{
public:
	GENERATED_BODY(UParticleModuleEventGenerator)

	UParticleModuleEventGenerator();

	TArray<FParticleEvent_GenerateInfo> Events;

	void Spawn(const FSpawnContext& Context) override { (void)Context; }
	void Update(const FUpdateContext& Context) override { (void)Context; }
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }
	uint32 RequiredBytesPerInstance() override;
	uint32 PrepPerInstanceBlock(FParticleEmitterInstance* Owner, void* InstData) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;

	bool HandleParticleSpawned(FParticleEmitterInstance* Owner, FParticleEventInstancePayload* EventPayload, FBaseParticle* NewParticle);
	bool HandleParticleKilled(FParticleEmitterInstance* Owner, FParticleEventInstancePayload* EventPayload, FBaseParticle* DeadParticle);
	bool HandleParticleCollision(FParticleEmitterInstance* Owner, FParticleEventInstancePayload* EventPayload,
		FBaseParticle* CollideParticle, const FVector& HitLocation, const FVector& HitNormal, float HitTime);
	bool HandleParticleBurst(FParticleEmitterInstance* Owner, FParticleEventInstancePayload* EventPayload, int32 ParticleCount);
};

UCLASS()
class UParticleModuleEventReceiverBase : public UParticleModuleEventBase
{
public:
	GENERATED_BODY(UParticleModuleEventReceiverBase)

	EParticleEventType EventGeneratorType = EPET_Any;

	virtual bool WillProcessParticleEvent(EParticleEventType InEventType) const
	{
		return EventGeneratorType == EPET_Any || EventGeneratorType == InEventType;
	}

	virtual bool ProcessParticleEvent(FParticleEmitterInstance* Owner, FParticleEventData& InEvent, float DeltaTime)
	{
		(void)Owner;
		(void)InEvent;
		(void)DeltaTime;
		return false;
	}
};

UCLASS()
class UParticleModuleEventReceiverSpawn : public UParticleModuleEventReceiverBase
{
public:
	GENERATED_BODY(UParticleModuleEventReceiverSpawn)

	UParticleModuleEventReceiverSpawn();

	UPROPERTY(Edit, Category="Event")
	FName EventName = FName::None;

	UPROPERTY(Edit, Category="Spawn", Min=0, Max=1024, Speed=1.0f)
	int32 SpawnCount = 1;

	UPROPERTY(Edit, Category="Spawn")
	bool bSpawnOnlyOnEvent = true;

	UPROPERTY(Edit, Category="Spawn")
	FVector SpawnLocationOffset = FVector::ZeroVector;

	UPROPERTY(Edit, Category="Spawn")
	bool bInheritEventVelocity = false;

	UPROPERTY(Edit, Category="Spawn", Min=0.0f, Max=100.0f, Speed=0.1f)
	float EventVelocityScale = 1.0f;

	bool ProcessParticleEvent(FParticleEmitterInstance* Owner, FParticleEventData& InEvent, float DeltaTime) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleRequired)

	EModuleType GetModuleType() const override { return EPMT_Required; }
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }

	UMaterialInterface* Material = nullptr;
	FVector EmitterOrigin = FVector::ZeroVector;
	EParticleScreenAlignment ScreenAlignment = PSA_FacingCameraPosition;
	int32 SubImages_Horizontal = 1;
	int32 SubImages_Vertical = 1;
	int32 AlphaSource = 0; // 0: texture alpha, 1: texture luminance
	float AlphaThreshold = 0.0f;
	float AlphaPower = 1.0f;
	float ColorIntensity = 1.0f;
	uint8 bUseLocalSpace : 1 = false;
	uint8 bKillOnDeactivate : 1 = false;
	uint8 bKillOnCompleted : 1 = false;
	EParticleSortMode SortMode = PSORTMODE_None;
	float EmitterDuration = 1.0f;
	float EmitterDelay = 0.0f;
	int32 EmitterLoops = 0;
	int32 MaxDrawCount = 0;
};

UCLASS()
class UParticleModuleSpawnBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleSpawnBase)

	uint32 bProcessSpawnRate : 1 = false;
	uint32 bProcessBurstList : 1 = false;

	EModuleType GetModuleType() const override { return EPMT_Spawn; }
	bool IsOnSpawnModule() const override { return true; }

	virtual bool GetSpawnAmount(const FContext& Context, int32 Offset, float OldLeftover, float DeltaTime, int32& Number, float& Rate)
	{
		(void)Context; (void)Offset; (void)OldLeftover; (void)DeltaTime;
		Number = 0;
		Rate = 0.0f;
		return bProcessSpawnRate != 0;
	}

	virtual bool GetBurstCount(FParticleEmitterInstance* Owner, int32 Offset, float OldLeftover, float DeltaTime, int32& Number)
	{
		(void)Owner; (void)Offset; (void)OldLeftover; (void)DeltaTime;
		Number = 0;
		return bProcessBurstList != 0;
	}

	virtual float GetMaximumSpawnRate() { return 0.0f; }
	virtual float GetEstimatedSpawnRate() { return 0.0f; }
	virtual int32 GetMaximumBurstCount() { return 0; }
};

UCLASS()
class UParticleModuleSpawn : public UParticleModuleSpawnBase
{
public:
	GENERATED_BODY(UParticleModuleSpawn)

	UParticleModuleSpawn();
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }

	UPROPERTY(Edit, Category="Spawn", DisplayName="Rate", Min=0.0f, Max=10000.0f, Speed=1.0f)
	float Rate = 10.0f;
	FParticleDistributionFloat RateDistribution;

	TArray<FParticleBurst> BurstList;
	EParticleBurstMethod ParticleBurstMethod = EPBM_Instant;

	bool GetSpawnAmount(const FContext& Context, int32 Offset, float OldLeftover, float DeltaTime, int32& Number, float& OutRate) override;
	float GetMaximumSpawnRate() override { return Rate; }
	float GetEstimatedSpawnRate() override { return Rate; }
	int32 GetMaximumBurstCount() override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleLifetimeBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleLifetimeBase)

	virtual float GetMaxLifetime() { return 0.0f; }
	virtual float GetLifetimeValue(const FContext& Context, float InTime, UObject* Data = nullptr)
	{
		(void)Context; (void)Data;
		return InTime;
	}
};

UCLASS()
class UParticleModuleLifetime : public UParticleModuleLifetimeBase
{
public:
	GENERATED_BODY(UParticleModuleLifetime)

	UParticleModuleLifetime();
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }

	UPROPERTY(Edit, Category="Lifetime", DisplayName="Lifetime", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float Lifetime = 1.0f;
	float LifetimeMin = 1.0f;
	float LifetimeMax = 1.0f;
	FParticleDistributionFloat LifetimeDistribution;

	void Spawn(const FSpawnContext& Context) override;
	float GetMaxLifetime() override { return LifetimeDistribution.GetMaxValue(); }
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleLocationBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleLocationBase)
};

UCLASS()
class UParticleModuleLocation : public UParticleModuleLocationBase
{
public:
	GENERATED_BODY(UParticleModuleLocation)

	UParticleModuleLocation();
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }

	UPROPERTY(Edit, Category="Location", DisplayName="Start Location")
	FVector StartLocation = FVector::ZeroVector;
	FVector StartLocationMin = FVector::ZeroVector;
	FVector StartLocationMax = FVector::ZeroVector;
	FParticleDistributionVector StartLocationDistribution;

	void Spawn(const FSpawnContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleVelocityBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleVelocityBase)

	uint32 bInWorldSpace : 1 = false;
	uint32 bApplyOwnerScale : 1 = false;
};

UCLASS()
class UParticleModuleVelocity : public UParticleModuleVelocityBase
{
public:
	GENERATED_BODY(UParticleModuleVelocity)

	UParticleModuleVelocity();
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }

	UPROPERTY(Edit, Category="Velocity", DisplayName="Start Velocity")
	FVector StartVelocity = FVector::UpVector;
	FVector StartVelocityMin = FVector::UpVector;
	FVector StartVelocityMax = FVector::UpVector;
	FParticleDistributionVector StartVelocityDistribution;

	void Spawn(const FSpawnContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleInitialRotation : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleInitialRotation)

	UParticleModuleInitialRotation();

	UPROPERTY(Edit, Category="Rotation", DisplayName="Start Rotation")
	FVector StartRotationDegrees = FVector::ZeroVector;
	FVector StartRotationDegreesMin = FVector::ZeroVector;
	FVector StartRotationDegreesMax = FVector::ZeroVector;
	FParticleDistributionVector StartRotationDistribution;

	void Spawn(const FSpawnContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleInitialRotationRate : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleInitialRotationRate)

	UParticleModuleInitialRotationRate();

	UPROPERTY(Edit, Category="Rotation", DisplayName="Start Rotation Rate")
	FVector StartRotationRateDegrees = FVector::ZeroVector;
	FVector StartRotationRateDegreesMin = FVector::ZeroVector;
	FVector StartRotationRateDegreesMax = FVector::ZeroVector;
	FParticleDistributionVector StartRotationRateDistribution;

	void Spawn(const FSpawnContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleAcceleration : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleAcceleration)

	UParticleModuleAcceleration();

	UPROPERTY(Edit, Category="Acceleration", DisplayName="Acceleration")
	FVector Acceleration = FVector::ZeroVector;
	FParticleDistributionVector AccelerationDistribution;

	void Update(const FUpdateContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

struct FParticleCollisionPayload
{
	int32 CollisionCount = 0;
};

UCLASS()
class UParticleModuleCollision : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleCollision)

	UParticleModuleCollision();

	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override;
	void FinalUpdate(const FUpdateContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;

	UPROPERTY(Edit, Category="Collision", DisplayName="Trace Channel", Type=Enum, Enum=StaticEnum_ECollisionChannel())
	ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic;

	UPROPERTY(Edit, Category="Collision", DisplayName="Response Mode", Type=Enum, Enum=StaticEnum_EParticleCollisionResponseMode())
	EParticleCollisionResponseMode ResponseMode = EParticleCollisionResponseMode::Bounce;

	UPROPERTY(Edit, Category="Collision", DisplayName="Damping Factor", Min=0.0f, Max=1.0f, Speed=0.01f)
	float DampingFactor = 0.5f;

	UPROPERTY(Edit, Category="Collision", DisplayName="Collision Offset", Min=0.0f, Max=100.0f, Speed=0.1f)
	float CollisionOffset = 0.1f;

	UPROPERTY(Edit, Category="Collision", DisplayName="Collision Radius Scale", Min=0.0f, Max=10.0f, Speed=0.05f)
	float CollisionRadiusScale = 1.0f;

	UPROPERTY(Edit, Category="Collision", DisplayName="Max Collisions", Min=0, Max=128, Speed=1.0f)
	int32 MaxCollisions = 1;
};

struct FParticleOrbitPayload
{
	FVector InitialOffset = FVector::ZeroVector;
	FVector CurrentRotationDegrees = FVector::ZeroVector;
	FVector RotationRateDegrees = FVector::ZeroVector;
	FVector RotationRateAccumulatedDegrees = FVector::ZeroVector;
	FVector LastOffset = FVector::ZeroVector;
};

UCLASS()
class UParticleModuleOrbit : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleOrbit)

	UParticleModuleOrbit();

	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override;
	void Spawn(const FSpawnContext& Context) override;
	void FinalUpdate(const FUpdateContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;

	UPROPERTY(Edit, Category="Orbit", DisplayName="Offset")
	FVector Offset = FVector(50.0f, 0.0f, 0.0f);
	FParticleDistributionVector OffsetDistribution;

	UPROPERTY(Edit, Category="Orbit", DisplayName="Rotation")
	FVector RotationDegrees = FVector::ZeroVector;
	FParticleDistributionVector RotationDistribution;

	UPROPERTY(Edit, Category="Orbit", DisplayName="Rotation Rate")
	FVector RotationRateDegrees = FVector(0.0f, 0.0f, 90.0f);
	FParticleDistributionVector RotationRateDistribution;
};

UCLASS()
class UParticleModuleColorBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleColorBase)
};

UCLASS()
class UParticleModuleColor : public UParticleModuleColorBase
{
public:
	GENERATED_BODY(UParticleModuleColor)

	UParticleModuleColor();
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }

	UPROPERTY(Edit, Category="Color", DisplayName="Start Color")
	FVector StartColor = FVector::OneVector;
	FVector StartColorMin = FVector::OneVector;
	FVector StartColorMax = FVector::OneVector;
	FParticleDistributionVector StartColorDistribution;

	UPROPERTY(Edit, Category="Color", DisplayName="Start Alpha", Min=0.0f, Max=1.0f, Speed=0.01f)
	float StartAlpha = 1.0f;
	float StartAlphaMin = 1.0f;
	float StartAlphaMax = 1.0f;
	FParticleDistributionFloat StartAlphaDistribution;

	void Spawn(const FSpawnContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleColorOverLife : public UParticleModuleColorBase
{
public:
	GENERATED_BODY(UParticleModuleColorOverLife)

	UParticleModuleColorOverLife();
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }

	UPROPERTY(Edit, Category="Color", DisplayName="Color Over Life")
	FVector ColorOverLife = FVector::OneVector;
	FParticleDistributionVector ColorOverLifeDistribution;

	UPROPERTY(Edit, Category="Color", DisplayName="Alpha Over Life", Min=0.0f, Max=1.0f, Speed=0.01f)
	float AlphaOverLife = 0.0f;
	FParticleDistributionFloat AlphaOverLifeDistribution;

	void Spawn(const FSpawnContext& Context) override;
	void Update(const FUpdateContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleColorScaleOverLife : public UParticleModuleColorBase
{
public:
	GENERATED_BODY(UParticleModuleColorScaleOverLife)

	UParticleModuleColorScaleOverLife();

	UPROPERTY(Edit, Category="Color", DisplayName="Color Scale Over Life")
	FVector ColorScaleOverLife = FVector::OneVector;
	FParticleDistributionVector ColorScaleOverLifeDistribution;

	UPROPERTY(Edit, Category="Color", DisplayName="Alpha Scale Over Life", Min=0.0f, Max=1.0f, Speed=0.01f)
	float AlphaScaleOverLife = 1.0f;
	FParticleDistributionFloat AlphaScaleOverLifeDistribution;

	void Update(const FUpdateContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
};

UCLASS()
class UParticleModuleSizeBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleSizeBase)
};

UCLASS()
class UParticleModuleSize : public UParticleModuleSizeBase
{
public:
	GENERATED_BODY(UParticleModuleSize)

	UParticleModuleSize();

	UPROPERTY(Edit, Category="Size", DisplayName="Start Size")
	FVector StartSize = FVector::OneVector;
	FVector StartSizeMin = FVector::OneVector;
	FVector StartSizeMax = FVector::OneVector;
	FParticleDistributionVector StartSizeDistribution;

	void Spawn(const FSpawnContext& Context) override;
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override { (void)TypeData; return 0; }
};

UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
public:
	GENERATED_BODY(UParticleModuleTypeDataBase)

	EModuleType GetModuleType() const override { return EPMT_TypeData; }

	virtual bool RequiresBuild() const { return false; }
	virtual bool SupportsSpecificScreenAlignmentFlags() const { return false; }
	virtual bool IsAMeshEmitter() const { return false; }
	virtual bool IsABeamEmitter() const { return false; }
	virtual bool IsARibbonEmitter() const { return false; }
};

// This struct is used to store the 3D orientation of a mesh particle.
// Standard sprite particles only store a single float for 2D rotation,
// but meshes require Euler angles and their rates of change.
struct FMeshRotationPayloadData
{
	FVector Rotation;
	FVector RotationRate;
};

// This struct is utilized when motion blur or velocity alignment is enabled.
// It allows the renderer to calculate the “stretch” or blur between the previous and current frame.
struct FMeshMotionPayloadData
{
	FVector LastLocation;
};

// This is the base payload for any mesh emitter, used for internal synchronization and indexing.
struct FMeshTypeDataPayload
{
	uint32 PayloadData;
};

UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
public:
	GENERATED_BODY(UParticleModuleTypeDataMesh)

	UPROPERTY(Edit, Category="Mesh", DisplayName="Mesh Path")
	FString MeshPath;
	
	UStaticMesh* Mesh = nullptr;

	bool IsAMeshEmitter() const override { return true; }
	UParticleModule* CloneForLOD(UParticleLODLevel* NewOuter) const override;
	uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData = nullptr) override
	{
		(void)TypeData;
		return sizeof(FMeshRotationPayloadData) + sizeof(FMeshMotionPayloadData) + sizeof(FMeshTypeDataPayload);
	}
};


struct FRibbonParticlePayload
{
	uint32 SpawnSequence = 0;
	int32 TrailIndex = 0; //
};
