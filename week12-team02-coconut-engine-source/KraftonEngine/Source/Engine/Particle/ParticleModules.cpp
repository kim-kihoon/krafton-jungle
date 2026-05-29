#include "Particle/ParticleModule.h"

#include "Component/ParticleSystemComponent.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"
#include "Math/Quat.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "Particle/ParticleEmitterInstances.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace
{
	float RandomRange(float MinValue, float MaxValue)
	{
		if (MaxValue < MinValue)
		{
			std::swap(MinValue, MaxValue);
		}

		static thread_local std::mt19937 Generator{ std::random_device{}() };
		std::uniform_real_distribution<float> Distribution(MinValue, MaxValue);
		return Distribution(Generator);
	}

	FVector RandomRange(const FVector& MinValue, const FVector& MaxValue)
	{
		return FVector(
			RandomRange(MinValue.X, MaxValue.X),
			RandomRange(MinValue.Y, MaxValue.Y),
			RandomRange(MinValue.Z, MaxValue.Z));
	}

	FVector RotateOrbitOffset(const FVector& Offset, const FVector& RotationDegrees)
	{
		FQuat Rotation =
			FQuat::FromAxisAngle(FVector::XAxisVector, RotationDegrees.X * FMath::DegToRad) *
			FQuat::FromAxisAngle(FVector::YAxisVector, RotationDegrees.Y * FMath::DegToRad) *
			FQuat::FromAxisAngle(FVector::ZAxisVector, RotationDegrees.Z * FMath::DegToRad);
		Rotation.Normalize();
		return Rotation.RotateVector(Offset);
	}
}

void FParticleDistributionFloat::SetConstant(float Value)
{
	Mode = EParticleDistributionMode::Constant;
	Constant = Value;
	Min = Value;
	Max = Value;
	ConstantCurve.DefaultValue = Value;
	MinCurve.DefaultValue = Value;
	MaxCurve.DefaultValue = Value;
}

void FParticleDistributionFloat::SetUniform(float InMin, float InMax)
{
	Mode = EParticleDistributionMode::Uniform;
	Min = InMin;
	Max = InMax;
	Constant = InMax;
	MinCurve.DefaultValue = InMin;
	MaxCurve.DefaultValue = InMax;
	ConstantCurve.DefaultValue = InMax;
}

void FParticleDistributionFloat::SetConstantCurve(float Time0, float Value0, float Time1, float Value1)
{
	Mode = EParticleDistributionMode::ConstantCurve;
	Constant = Value1;
	Min = Value0;
	Max = Value1;
	ConstantCurve.Reset();
	ConstantCurve.DefaultValue = Value1;
	ConstantCurve.AddKey(Time0, Value0);
	ConstantCurve.AddKey(Time1, Value1);
	ConstantCurve.SortKeys();
	ConstantCurve.AutoSetTangents();
}

void FParticleDistributionFloat::SetUniformCurve(float Time0, float Min0, float Max0, float Time1, float Min1, float Max1)
{
	Mode = EParticleDistributionMode::UniformCurve;
	Constant = Max1;
	Min = Min0;
	Max = Max1;
	MinCurve.Reset();
	MinCurve.DefaultValue = Min0;
	MinCurve.AddKey(Time0, Min0);
	MinCurve.AddKey(Time1, Min1);
	MinCurve.SortKeys();
	MinCurve.AutoSetTangents();

	MaxCurve.Reset();
	MaxCurve.DefaultValue = Max0;
	MaxCurve.AddKey(Time0, Max0);
	MaxCurve.AddKey(Time1, Max1);
	MaxCurve.SortKeys();
	MaxCurve.AutoSetTangents();
}

float FParticleDistributionFloat::Evaluate(float Time) const
{
	switch (Mode)
	{
	case EParticleDistributionMode::ConstantCurve:
		return ConstantCurve.Evaluate(Time);
	case EParticleDistributionMode::UniformCurve:
		return MaxCurve.Evaluate(Time);
	case EParticleDistributionMode::Uniform:
		return Max;
	case EParticleDistributionMode::Constant:
	default:
		return Constant;
	}
}

float FParticleDistributionFloat::EvaluateRandom(float Time) const
{
	if (Mode == EParticleDistributionMode::UniformCurve)
	{
		return RandomRange(MinCurve.Evaluate(Time), MaxCurve.Evaluate(Time));
	}
	if (Mode == EParticleDistributionMode::Uniform)
	{
		return RandomRange(Min, Max);
	}
	return Evaluate(Time);
}

float FParticleDistributionFloat::GetMaxValue() const
{
	float Result = (std::max)(Constant, Max);
	auto IncludeCurve = [&Result](const FFloatCurve& Curve)
	{
		for (const FCurveKey& Key : Curve.Keys)
		{
			Result = (std::max)(Result, Key.Value);
		}
	};

	IncludeCurve(ConstantCurve);
	IncludeCurve(MinCurve);
	IncludeCurve(MaxCurve);
	return Result;
}

bool FParticleDistributionFloat::UsesCurve() const
{
	return Mode == EParticleDistributionMode::ConstantCurve || Mode == EParticleDistributionMode::UniformCurve;
}

FFloatCurve* FParticleDistributionFloat::GetCurve(bool bMaxCurve)
{
	if (Mode == EParticleDistributionMode::UniformCurve)
	{
		return bMaxCurve ? &MaxCurve : &MinCurve;
	}
	return &ConstantCurve;
}

const FFloatCurve* FParticleDistributionFloat::GetCurve(bool bMaxCurve) const
{
	if (Mode == EParticleDistributionMode::UniformCurve)
	{
		return bMaxCurve ? &MaxCurve : &MinCurve;
	}
	return &ConstantCurve;
}

void FParticleDistributionVector::SetConstant(const FVector& Value)
{
	X.SetConstant(Value.X);
	Y.SetConstant(Value.Y);
	Z.SetConstant(Value.Z);
}

void FParticleDistributionVector::SetUniform(const FVector& MinValue, const FVector& MaxValue)
{
	X.SetUniform(MinValue.X, MaxValue.X);
	Y.SetUniform(MinValue.Y, MaxValue.Y);
	Z.SetUniform(MinValue.Z, MaxValue.Z);
}

void FParticleDistributionVector::SetConstantCurve(float Time0, const FVector& Value0, float Time1, const FVector& Value1)
{
	X.SetConstantCurve(Time0, Value0.X, Time1, Value1.X);
	Y.SetConstantCurve(Time0, Value0.Y, Time1, Value1.Y);
	Z.SetConstantCurve(Time0, Value0.Z, Time1, Value1.Z);
}

void FParticleDistributionVector::SetUniformCurve(float Time0, const FVector& Min0, const FVector& Max0, float Time1, const FVector& Min1, const FVector& Max1)
{
	X.SetUniformCurve(Time0, Min0.X, Max0.X, Time1, Min1.X, Max1.X);
	Y.SetUniformCurve(Time0, Min0.Y, Max0.Y, Time1, Min1.Y, Max1.Y);
	Z.SetUniformCurve(Time0, Min0.Z, Max0.Z, Time1, Min1.Z, Max1.Z);
}

FVector FParticleDistributionVector::Evaluate(float Time) const
{
	return FVector(X.Evaluate(Time), Y.Evaluate(Time), Z.Evaluate(Time));
}

FVector FParticleDistributionVector::EvaluateRandom(float Time) const
{
	return FVector(X.EvaluateRandom(Time), Y.EvaluateRandom(Time), Z.EvaluateRandom(Time));
}

FVector FParticleDistributionVector::GetMaxValue() const
{
	return FVector(X.GetMaxValue(), Y.GetMaxValue(), Z.GetMaxValue());
}

bool FParticleDistributionVector::UsesCurve() const
{
	return X.UsesCurve() || Y.UsesCurve() || Z.UsesCurve();
}

FParticleDistributionFloat* FParticleDistributionVector::GetChannel(int32 ChannelIndex)
{
	if (ChannelIndex == 0) return &X;
	if (ChannelIndex == 1) return &Y;
	return &Z;
}

const FParticleDistributionFloat* FParticleDistributionVector::GetChannel(int32 ChannelIndex) const
{
	if (ChannelIndex == 0) return &X;
	if (ChannelIndex == 1) return &Y;
	return &Z;
}

void UParticleModule::CopyModuleBaseTo(UParticleModule* Copy) const
{
	if (!Copy)
	{
		return;
	}

	Copy->bSpawnModule = bSpawnModule;
	Copy->bUpdateModule = bUpdateModule;
	Copy->bFinalUpdateModule = bFinalUpdateModule;
	Copy->bEnabled = bEnabled;
	Copy->bEditable = bEditable;
	Copy->LODValidity = LODValidity;
}

UParticleModule* UParticleModule::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModule* Copy = Cast<UParticleModule>(Duplicate(NewOuter));
	CopyModuleBaseTo(Copy);
	return Copy;
}

UParticleModuleEventGenerator::UParticleModuleEventGenerator()
{
	bSpawnModule = false;
	bUpdateModule = false;
}

uint32 UParticleModuleEventGenerator::RequiredBytesPerInstance()
{
	return sizeof(FParticleEventInstancePayload);
}

uint32 UParticleModuleEventGenerator::PrepPerInstanceBlock(FParticleEmitterInstance* Owner, void* InstData)
{
	(void)Owner;
	if (!InstData)
	{
		return 0;
	}

	FParticleEventInstancePayload* Payload = static_cast<FParticleEventInstancePayload*>(InstData);
	*Payload = FParticleEventInstancePayload();

	for (const FParticleEvent_GenerateInfo& EventInfo : Events)
	{
		switch (EventInfo.Type)
		{
		case EPET_Spawn:
			Payload->bSpawnEventsPresent = true;
			break;
		case EPET_Death:
			Payload->bDeathEventsPresent = true;
			break;
		case EPET_Collision:
			Payload->bCollisionEventsPresent = true;
			break;
		case EPET_Burst:
			Payload->bBurstEventsPresent = true;
			break;
		default:
			break;
		}
	}

	return sizeof(FParticleEventInstancePayload);
}

UParticleModule* UParticleModuleEventGenerator::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleEventGenerator* Copy = GUObjectArray.CreateObject<UParticleModuleEventGenerator>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->Events = Events;
	return Copy;
}

static bool ShouldGenerateParticleEvent(const FParticleEvent_GenerateInfo& EventInfo, int32 TrackingCount, const FBaseParticle* Particle)
{
	if (EventInfo.Frequency > 0 && (TrackingCount % EventInfo.Frequency) != 0)
	{
		return false;
	}
	if (Particle && EventInfo.ParticleFrequency > 0 && (Particle->ParticleId % static_cast<uint32>(EventInfo.ParticleFrequency)) != 0)
	{
		return false;
	}
	return true;
}

bool UParticleModuleEventGenerator::HandleParticleSpawned(FParticleEmitterInstance* Owner,
	FParticleEventInstancePayload* EventPayload, FBaseParticle* NewParticle)
{
	if (!Owner || !Owner->Component || !EventPayload || !NewParticle || !EventPayload->bSpawnEventsPresent)
	{
		return false;
	}

	++EventPayload->SpawnTrackingCount;
	bool bProcessed = false;
	for (const FParticleEvent_GenerateInfo& EventInfo : Events)
	{
		if (EventInfo.Type != EPET_Spawn || !ShouldGenerateParticleEvent(EventInfo, EventPayload->SpawnTrackingCount, NewParticle))
		{
			continue;
		}

		Owner->Component->ReportEventSpawn(EventInfo.CustomName, Owner->EmitterTime, NewParticle->Location, NewParticle->Velocity);
		bProcessed = true;
	}
	return bProcessed;
}

bool UParticleModuleEventGenerator::HandleParticleKilled(FParticleEmitterInstance* Owner,
	FParticleEventInstancePayload* EventPayload, FBaseParticle* DeadParticle)
{
	if (!Owner || !Owner->Component || !EventPayload || !DeadParticle || !EventPayload->bDeathEventsPresent)
	{
		return false;
	}

	++EventPayload->DeathTrackingCount;
	bool bProcessed = false;
	for (const FParticleEvent_GenerateInfo& EventInfo : Events)
	{
		if (EventInfo.Type != EPET_Death || !ShouldGenerateParticleEvent(EventInfo, EventPayload->DeathTrackingCount, DeadParticle))
		{
			continue;
		}

		FVector Direction = DeadParticle->Velocity;
		Direction.Normalize();
		Owner->Component->ReportEventDeath(EventInfo.CustomName, Owner->EmitterTime,
			DeadParticle->Location, DeadParticle->Velocity, DeadParticle->RelativeTime, Direction);
		bProcessed = true;
	}
	return bProcessed;
}

bool UParticleModuleEventGenerator::HandleParticleCollision(FParticleEmitterInstance* Owner,
	FParticleEventInstancePayload* EventPayload, FBaseParticle* CollideParticle,
	const FVector& HitLocation, const FVector& HitNormal, float HitTime)
{
	if (!Owner || !Owner->Component || !EventPayload || !CollideParticle || !EventPayload->bCollisionEventsPresent)
	{
		return false;
	}

	++EventPayload->CollisionTrackingCount;
	bool bProcessed = false;
	for (const FParticleEvent_GenerateInfo& EventInfo : Events)
	{
		if (EventInfo.Type != EPET_Collision || !ShouldGenerateParticleEvent(EventInfo, EventPayload->CollisionTrackingCount, CollideParticle))
		{
			continue;
		}
		if (EventInfo.FirstTimeOnly && (CollideParticle->Flags & STATE_Particle_CollisionHasOccurred) != 0)
		{
			continue;
		}
		if (EventInfo.LastTimeOnly && (CollideParticle->RelativeTime < 1.0f))
		{
			continue;
		}

		FVector Direction = EventInfo.UseReflectedImpactVector
			? CollideParticle->Velocity - HitNormal * (2.0f * CollideParticle->Velocity.Dot(HitNormal))
			: CollideParticle->Velocity;
		Direction.Normalize();

		Owner->Component->ReportEventCollision(EventInfo.CustomName, Owner->EmitterTime, HitLocation,
			Direction, CollideParticle->Velocity, CollideParticle->RelativeTime, HitNormal, HitTime);
		bProcessed = true;
	}
	return bProcessed;
}

bool UParticleModuleEventGenerator::HandleParticleBurst(FParticleEmitterInstance* Owner,
	FParticleEventInstancePayload* EventPayload, int32 ParticleCount)
{
	if (!Owner || !Owner->Component || !EventPayload || ParticleCount <= 0 || !EventPayload->bBurstEventsPresent)
	{
		return false;
	}

	++EventPayload->BurstTrackingCount;
	bool bProcessed = false;
	for (const FParticleEvent_GenerateInfo& EventInfo : Events)
	{
		if (EventInfo.Type != EPET_Burst || !ShouldGenerateParticleEvent(EventInfo, EventPayload->BurstTrackingCount, nullptr))
		{
			continue;
		}

		Owner->Component->ReportEventBurst(EventInfo.CustomName, Owner->EmitterTime, ParticleCount, Owner->Location);
		bProcessed = true;
	}
	return bProcessed;
}

UParticleModuleEventReceiverSpawn::UParticleModuleEventReceiverSpawn()
{
	EventGeneratorType = EPET_Collision;
}

bool UParticleModuleEventReceiverSpawn::ProcessParticleEvent(FParticleEmitterInstance* Owner, FParticleEventData& InEvent, float DeltaTime)
{
	if (!Owner || !WillProcessParticleEvent(InEvent.Type) || SpawnCount <= 0)
	{
		return false;
	}

	const bool bHasNameFilter = EventName.IsValid() && EventName != FName::None;
	if (bHasNameFilter && EventName != InEvent.EventName)
	{
		return false;
	}

	FParticleEventInstancePayload* EventPayload = nullptr;
	if (Owner->CurrentLODLevel && Owner->CurrentLODLevel->EventGenerator)
	{
		EventPayload = reinterpret_cast<FParticleEventInstancePayload*>(
			Owner->GetModuleInstanceData(Owner->CurrentLODLevel->EventGenerator));
	}

	const int32 Count = std::clamp(SpawnCount, 0, 1024);
	const FVector SpawnLocation = InEvent.Location + SpawnLocationOffset;
	const FVector SpawnVelocity = bInheritEventVelocity
		? InEvent.Velocity * std::max(0.0f, EventVelocityScale)
		: FVector::ZeroVector;
	const float Increment = Count > 0 ? std::max(0.0f, DeltaTime) / static_cast<float>(Count) : 0.0f;
	Owner->SpawnParticles(Count, 0.0f, Increment, SpawnLocation, SpawnVelocity, EventPayload);
	return true;
}

UParticleModule* UParticleModuleEventReceiverSpawn::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleEventReceiverSpawn* Copy = GUObjectArray.CreateObject<UParticleModuleEventReceiverSpawn>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->EventGeneratorType = EventGeneratorType;
	Copy->EventName = EventName;
	Copy->SpawnCount = SpawnCount;
	Copy->bSpawnOnlyOnEvent = bSpawnOnlyOnEvent;
	Copy->SpawnLocationOffset = SpawnLocationOffset;
	Copy->bInheritEventVelocity = bInheritEventVelocity;
	Copy->EventVelocityScale = EventVelocityScale;
	return Copy;
}

UParticleModule* UParticleModuleRequired::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleRequired* Copy = GUObjectArray.CreateObject<UParticleModuleRequired>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->Material = Material;
	Copy->EmitterOrigin = EmitterOrigin;
	Copy->ScreenAlignment = ScreenAlignment;
	Copy->SubImages_Horizontal = SubImages_Horizontal;
	Copy->SubImages_Vertical = SubImages_Vertical;
	Copy->AlphaSource = AlphaSource;
	Copy->AlphaThreshold = AlphaThreshold;
	Copy->AlphaPower = AlphaPower;
	Copy->ColorIntensity = ColorIntensity;
	Copy->bUseLocalSpace = bUseLocalSpace;
	Copy->bKillOnDeactivate = bKillOnDeactivate;
	Copy->bKillOnCompleted = bKillOnCompleted;
	Copy->SortMode = SortMode;
	Copy->EmitterDuration = EmitterDuration;
	Copy->EmitterDelay = EmitterDelay;
	Copy->EmitterLoops = EmitterLoops;
	Copy->MaxDrawCount = MaxDrawCount;
	return Copy;
}

UParticleModuleSpawn::UParticleModuleSpawn()
{
	bEnabled = true;
	bProcessSpawnRate = true;
	bProcessBurstList = true;
	RateDistribution.SetConstant(Rate);
}

bool UParticleModuleSpawn::GetSpawnAmount(const FContext& Context, int32 Offset, float OldLeftover, float DeltaTime,
	int32& Number, float& OutRate)
{
	(void)Context;
	(void)Offset;
	(void)OldLeftover;
	(void)DeltaTime;
	Number = 0;
	OutRate = std::max(0.0f, RateDistribution.Evaluate(Context.Owner.EmitterTime));
	return true;
}

int32 UParticleModuleSpawn::GetMaximumBurstCount()
{
	int32 MaxBurst = 0;
	for (const FParticleBurst& Burst : BurstList)
	{
		MaxBurst += std::max(Burst.Count, Burst.CountLow);
	}
	return std::max(0, MaxBurst);
}

UParticleModule* UParticleModuleSpawn::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleSpawn* Copy = GUObjectArray.CreateObject<UParticleModuleSpawn>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->Rate = Rate;
	Copy->RateDistribution = RateDistribution;
	Copy->BurstList = BurstList;
	Copy->ParticleBurstMethod = ParticleBurstMethod;
	return Copy;
}

UParticleModuleLifetime::UParticleModuleLifetime()
{
	bSpawnModule = true;
	LifetimeDistribution.SetUniform(LifetimeMin, LifetimeMax);
}

void UParticleModuleLifetime::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const float SpawnLifetime = std::max(LifetimeDistribution.EvaluateRandom(Context.SpawnTime), 0.0001f);
	Context.ParticleBase->OneOverMaxLifetime = 1.0f / SpawnLifetime;
}

UParticleModule* UParticleModuleLifetime::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleLifetime* Copy = GUObjectArray.CreateObject<UParticleModuleLifetime>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->Lifetime = Lifetime;
	Copy->LifetimeMin = LifetimeMin;
	Copy->LifetimeMax = LifetimeMax;
	Copy->LifetimeDistribution = LifetimeDistribution;
	return Copy;
}

UParticleModuleLocation::UParticleModuleLocation()
{
	bSpawnModule = true;
	StartLocationDistribution.SetUniform(StartLocationMin, StartLocationMax);
}

void UParticleModuleLocation::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	Context.ParticleBase->Location = Context.ParticleBase->Location + StartLocationDistribution.EvaluateRandom(Context.SpawnTime);
	Context.ParticleBase->OldLocation = Context.ParticleBase->Location;
}

UParticleModule* UParticleModuleLocation::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleLocation* Copy = GUObjectArray.CreateObject<UParticleModuleLocation>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->StartLocation = StartLocation;
	Copy->StartLocationMin = StartLocationMin;
	Copy->StartLocationMax = StartLocationMax;
	Copy->StartLocationDistribution = StartLocationDistribution;
	return Copy;
}

UParticleModuleVelocity::UParticleModuleVelocity()
{
	bSpawnModule = true;
	StartVelocityDistribution.SetUniform(StartVelocityMin, StartVelocityMax);
}

void UParticleModuleVelocity::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector SpawnVelocity = StartVelocityDistribution.EvaluateRandom(Context.SpawnTime);
	Context.ParticleBase->BaseVelocity = SpawnVelocity;
	Context.ParticleBase->Velocity = SpawnVelocity;
}

UParticleModule* UParticleModuleVelocity::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleVelocity* Copy = GUObjectArray.CreateObject<UParticleModuleVelocity>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->StartVelocity = StartVelocity;
	Copy->StartVelocityMin = StartVelocityMin;
	Copy->StartVelocityMax = StartVelocityMax;
	Copy->StartVelocityDistribution = StartVelocityDistribution;
	Copy->bInWorldSpace = bInWorldSpace;
	Copy->bApplyOwnerScale = bApplyOwnerScale;
	return Copy;
}

UParticleModuleInitialRotation::UParticleModuleInitialRotation()
{
	bSpawnModule = true;
	StartRotationDistribution.SetUniform(StartRotationDegreesMin, StartRotationDegreesMax);
}

void UParticleModuleInitialRotation::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector SpawnRotationDegrees = StartRotationDistribution.EvaluateRandom(Context.SpawnTime);
	Context.ParticleBase->Rotation = SpawnRotationDegrees * FMath::DegToRad;
}

UParticleModule* UParticleModuleInitialRotation::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleInitialRotation* Copy = GUObjectArray.CreateObject<UParticleModuleInitialRotation>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->StartRotationDegrees = StartRotationDegrees;
	Copy->StartRotationDegreesMin = StartRotationDegreesMin;
	Copy->StartRotationDegreesMax = StartRotationDegreesMax;
	Copy->StartRotationDistribution = StartRotationDistribution;
	return Copy;
}

UParticleModuleInitialRotationRate::UParticleModuleInitialRotationRate()
{
	bSpawnModule = true;
	StartRotationRateDistribution.SetUniform(StartRotationRateDegreesMin, StartRotationRateDegreesMax);
}

void UParticleModuleInitialRotationRate::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector SpawnRotationRateDegrees = StartRotationRateDistribution.EvaluateRandom(Context.SpawnTime);
	const FVector SpawnRotationRate = SpawnRotationRateDegrees * FMath::DegToRad;
	Context.ParticleBase->BaseRotationRate = SpawnRotationRate;
	Context.ParticleBase->RotationRate = SpawnRotationRate;
}

UParticleModule* UParticleModuleInitialRotationRate::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleInitialRotationRate* Copy = GUObjectArray.CreateObject<UParticleModuleInitialRotationRate>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->StartRotationRateDegrees = StartRotationRateDegrees;
	Copy->StartRotationRateDegreesMin = StartRotationRateDegreesMin;
	Copy->StartRotationRateDegreesMax = StartRotationRateDegreesMax;
	Copy->StartRotationRateDistribution = StartRotationRateDistribution;
	return Copy;
}

UParticleModuleAcceleration::UParticleModuleAcceleration()
{
	bUpdateModule = true;
	AccelerationDistribution.SetConstant(Acceleration);
}

void UParticleModuleAcceleration::Update(const FUpdateContext& Context)
{
	FParticleEmitterInstance& Owner = Context.Owner;
	if (!Owner.ParticleData || !Owner.ParticleIndices)
	{
		return;
	}

	for (int32 ParticleIndex = 0; ParticleIndex < Owner.ActiveParticles; ++ParticleIndex)
	{
		FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(
			Owner.ParticleData + Owner.ParticleStride * Owner.ParticleIndices[ParticleIndex]);
		if (!Particle)
		{
			continue;
		}

		const FVector CurrentAcceleration = AccelerationDistribution.Evaluate(std::clamp(Particle->RelativeTime, 0.0f, 1.0f));
		const FVector VelocityDelta = CurrentAcceleration * Context.DeltaTime;
		Particle->BaseVelocity = Particle->BaseVelocity + VelocityDelta;
		Particle->Velocity = Particle->BaseVelocity;
	}
}

UParticleModule* UParticleModuleAcceleration::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleAcceleration* Copy = GUObjectArray.CreateObject<UParticleModuleAcceleration>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->Acceleration = Acceleration;
	Copy->AccelerationDistribution = AccelerationDistribution;
	return Copy;
}

UParticleModuleCollision::UParticleModuleCollision()
{
	bFinalUpdateModule = true;
}

uint32 UParticleModuleCollision::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	(void)TypeData;
	return sizeof(FParticleCollisionPayload);
}       

void UParticleModuleCollision::FinalUpdate(const FUpdateContext& Context)
{
	FParticleEmitterInstance& Owner = Context.Owner;
	if (!Owner.Component || !Owner.ParticleData || !Owner.ParticleIndices || MaxCollisions <= 0)
	{
		return;
	}

	UWorld* World = Owner.Component->GetWorld();
	if (!World)
	{
		return;
	}

	const float ClampedDamping = std::clamp(DampingFactor, 0.0f, 1.0f);
	const float ClampedOffset = std::max(CollisionOffset, 0.0f);
	const float ClampedRadiusScale = std::max(CollisionRadiusScale, 0.0f);

	for (int32 ParticleIndex = Owner.ActiveParticles - 1; ParticleIndex >= 0; --ParticleIndex)
	{
		FBaseParticle* Particle = Owner.GetParticleDirect(Owner.ParticleIndices[ParticleIndex]);
		if (!Particle || (Particle->Flags & STATE_Particle_CollisionIgnoreCheck) != 0)
		{
			continue;
		}

		FParticleCollisionPayload* Payload = reinterpret_cast<FParticleCollisionPayload*>(
			reinterpret_cast<uint8*>(Particle) + Context.Offset);
		if (!Payload || Payload->CollisionCount >= MaxCollisions)
		{
			continue;
		}

		const FVector Segment = Particle->Location - Particle->OldLocation;
		const float SegmentLength = Segment.Length();
		if (SegmentLength <= 0.0001f)
		{
			continue;
		}

		FVector Direction = Segment / SegmentLength;
		FHitResult Hit;
		const float ParticleScale = std::max({
			std::abs(Particle->Size.X),
			std::abs(Particle->Size.Y),
			std::abs(Particle->Size.Z)
		});
		float CollisionRadius = ParticleScale;
		if (Owner.CurrentLODLevel && Owner.CurrentLODLevel->TypeDataModule && Owner.CurrentLODLevel->TypeDataModule->IsAMeshEmitter())
		{
			if (UParticleModuleTypeDataMesh* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(Owner.CurrentLODLevel->TypeDataModule))
			{
				if (MeshTypeData->Mesh)
				{
					if (FStaticMesh* MeshAsset = MeshTypeData->Mesh->GetStaticMeshAsset())
					{
						if (!MeshAsset->bBoundsValid)
						{
							MeshAsset->CacheBounds();
						}
						CollisionRadius = std::max({
							std::abs(MeshAsset->BoundsExtent.X),
							std::abs(MeshAsset->BoundsExtent.Y),
							std::abs(MeshAsset->BoundsExtent.Z)
						}) * ParticleScale;
					}
				}
			}
		}
		CollisionRadius *= ClampedRadiusScale;
		const bool bHit = CollisionRadius > 0.0001f
			? World->PhysicsSphereSweep(Particle->OldLocation, Direction, SegmentLength, CollisionRadius, Hit, TraceChannel, Owner.Component->GetOwner())
			: World->PhysicsRaycast(Particle->OldLocation, Direction, SegmentLength, Hit, TraceChannel, Owner.Component->GetOwner());
		if (!bHit)
		{
			continue;
		}

		FVector Normal = Hit.ImpactNormal.IsNearlyZero() ? Hit.WorldNormal : Hit.ImpactNormal;
		if (Normal.IsNearlyZero())
		{
			Normal = Direction * -1.0f;
		}
		Normal.Normalize();

		FParticleEventCollideData EventData;
		EventData.EmitterIndex = Owner.EmitterIndex;
		EventData.ParticleIndex = ParticleIndex;
		EventData.Location = Hit.WorldHitLocation;
		EventData.OldLocation = Particle->OldLocation;
		EventData.Velocity = Particle->BaseVelocity;
		EventData.Direction = Particle->BaseVelocity;
		EventData.Direction.Normalize();
		EventData.Normal = Normal;
		EventData.EmitterTime = Owner.EmitterTime;
		EventData.ParticleRelativeTime = Particle->RelativeTime;
		EventData.ParticleTime = Particle->RelativeTime;
		EventData.HitTime = 0.0f;
		EventData.HitActor = Hit.HitActor;
		EventData.HitComponent = Hit.HitComponent;

		bool bEventGenerated = false;
		if (Owner.CurrentLODLevel && Owner.CurrentLODLevel->EventGenerator)
		{
			FParticleEventInstancePayload* EventPayload = reinterpret_cast<FParticleEventInstancePayload*>(
				Owner.GetModuleInstanceData(Owner.CurrentLODLevel->EventGenerator));
			bEventGenerated = Owner.CurrentLODLevel->EventGenerator->HandleParticleCollision(&Owner, EventPayload,
				Particle, Hit.WorldHitLocation, Normal, 0.0f);
		}
		if (!bEventGenerated)
		{
			Owner.Component->QueueParticleCollisionEvent(EventData);
		}

		++Payload->CollisionCount;
		Particle->Flags |= STATE_Particle_CollisionHasOccurred;
		Particle->Location = Hit.WorldHitLocation + Normal * (CollisionRadius + ClampedOffset);

		if (ResponseMode == EParticleCollisionResponseMode::Kill)
		{
			Owner.KillParticle(ParticleIndex);
			continue;
		}

		if (ResponseMode == EParticleCollisionResponseMode::Stop)
		{
			Particle->BaseVelocity = FVector::ZeroVector;
			Particle->Velocity = FVector::ZeroVector;
			Particle->Flags |= STATE_Particle_FreezeTranslation;
		}
		else
		{
			const FVector IncomingVelocity = Particle->BaseVelocity;
			const float NormalVelocity = IncomingVelocity.Dot(Normal);
			FVector ReflectedVelocity = IncomingVelocity;
			if (NormalVelocity < 0.0f)
			{
				ReflectedVelocity = IncomingVelocity - Normal * (2.0f * NormalVelocity);
			}
			Particle->BaseVelocity = ReflectedVelocity * ClampedDamping;
			Particle->Velocity = Particle->BaseVelocity;
		}

		if (Payload->CollisionCount >= MaxCollisions)
		{
			Particle->Flags |= STATE_Particle_IgnoreCollisions;
		}
	}
}

UParticleModule* UParticleModuleCollision::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleCollision* Copy = GUObjectArray.CreateObject<UParticleModuleCollision>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->TraceChannel = TraceChannel;
	Copy->ResponseMode = ResponseMode;
	Copy->DampingFactor = DampingFactor;
	Copy->CollisionOffset = CollisionOffset;
	Copy->CollisionRadiusScale = CollisionRadiusScale;
	Copy->MaxCollisions = MaxCollisions;
	return Copy;
}

UParticleModuleOrbit::UParticleModuleOrbit()
{
	bSpawnModule = true;
	bFinalUpdateModule = true;
	OffsetDistribution.SetConstant(Offset);
	RotationDistribution.SetConstant(RotationDegrees);
	RotationRateDistribution.SetConstant(RotationRateDegrees);
}

uint32 UParticleModuleOrbit::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	(void)TypeData;
	return sizeof(FParticleOrbitPayload);
}

void UParticleModuleOrbit::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	FParticleOrbitPayload* Payload = reinterpret_cast<FParticleOrbitPayload*>(
		reinterpret_cast<uint8*>(Context.ParticleBase) + Context.Offset);
	if (!Payload)
	{
		return;
	}

	Payload->InitialOffset = OffsetDistribution.EvaluateRandom(Context.SpawnTime);
	Payload->CurrentRotationDegrees = RotationDistribution.EvaluateRandom(Context.SpawnTime);
	Payload->RotationRateDegrees = RotationRateDistribution.EvaluateRandom(Context.SpawnTime);
	Payload->RotationRateAccumulatedDegrees = FVector::ZeroVector;
	Payload->LastOffset = RotateOrbitOffset(Payload->InitialOffset, Payload->CurrentRotationDegrees);

	Context.ParticleBase->Location = Context.ParticleBase->Location + Payload->LastOffset;
	Context.ParticleBase->OldLocation = Context.ParticleBase->OldLocation + Payload->LastOffset;
}

void UParticleModuleOrbit::FinalUpdate(const FUpdateContext& Context)
{
	FParticleEmitterInstance& Owner = Context.Owner;
	if (!Owner.ParticleData || !Owner.ParticleIndices)
	{
		return;
	}

	for (int32 ParticleIndex = 0; ParticleIndex < Owner.ActiveParticles; ++ParticleIndex)
	{
		FBaseParticle* Particle = Owner.GetParticleDirect(Owner.ParticleIndices[ParticleIndex]);
		if (!Particle)
		{
			continue;
		}

		FParticleOrbitPayload* Payload = reinterpret_cast<FParticleOrbitPayload*>(
			reinterpret_cast<uint8*>(Particle) + Context.Offset);
		if (!Payload)
		{
			continue;
		}

		const FVector BaseLocation = Particle->Location - Payload->LastOffset;
		const float T = std::clamp(Particle->RelativeTime, 0.0f, 1.0f);
		const FVector OrbitOffset = OffsetDistribution.UsesCurve()
			? OffsetDistribution.Evaluate(T)
			: Payload->InitialOffset;

		if ((Particle->Flags & STATE_Particle_JustSpawned) == 0)
		{
			const FVector RotationRate = RotationRateDistribution.UsesCurve()
				? RotationRateDistribution.Evaluate(T)
				: Payload->RotationRateDegrees;
			Payload->RotationRateAccumulatedDegrees = Payload->RotationRateAccumulatedDegrees + RotationRate * Context.DeltaTime;
		}

		const FVector RotationDegrees = (RotationDistribution.UsesCurve()
			? RotationDistribution.Evaluate(T)
			: Payload->CurrentRotationDegrees) + Payload->RotationRateAccumulatedDegrees;
		Payload->LastOffset = RotateOrbitOffset(OrbitOffset, RotationDegrees);
		Particle->Location = BaseLocation + Payload->LastOffset;
		Particle->Velocity = Particle->BaseVelocity;
	}
}

UParticleModule* UParticleModuleOrbit::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleOrbit* Copy = GUObjectArray.CreateObject<UParticleModuleOrbit>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->Offset = Offset;
	Copy->OffsetDistribution = OffsetDistribution;
	Copy->RotationDegrees = RotationDegrees;
	Copy->RotationDistribution = RotationDistribution;
	Copy->RotationRateDegrees = RotationRateDegrees;
	Copy->RotationRateDistribution = RotationRateDistribution;
	return Copy;
}

UParticleModuleColor::UParticleModuleColor()
{
	bSpawnModule = true;
	StartColorDistribution.SetUniform(StartColorMin, StartColorMax);
	StartAlphaDistribution.SetUniform(StartAlphaMin, StartAlphaMax);
}

void UParticleModuleColor::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const float Alpha = std::clamp(StartAlphaDistribution.EvaluateRandom(Context.SpawnTime), 0.0f, 1.0f);
	const FVector SpawnColor = StartColorDistribution.EvaluateRandom(Context.SpawnTime);
	Context.ParticleBase->BaseColor = FLinearColor(SpawnColor.X, SpawnColor.Y, SpawnColor.Z, Alpha);
	Context.ParticleBase->Color = Context.ParticleBase->BaseColor;
}

UParticleModule* UParticleModuleColor::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleColor* Copy = GUObjectArray.CreateObject<UParticleModuleColor>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->StartColor = StartColor;
	Copy->StartColorMin = StartColorMin;
	Copy->StartColorMax = StartColorMax;
	Copy->StartColorDistribution = StartColorDistribution;
	Copy->StartAlpha = StartAlpha;
	Copy->StartAlphaMin = StartAlphaMin;
	Copy->StartAlphaMax = StartAlphaMax;
	Copy->StartAlphaDistribution = StartAlphaDistribution;
	return Copy;
}

UParticleModuleColorOverLife::UParticleModuleColorOverLife()
{
	bSpawnModule = true;
	bUpdateModule = true;
	ColorOverLifeDistribution.SetConstant(ColorOverLife);
	AlphaOverLifeDistribution.SetConstant(AlphaOverLife);
}

void UParticleModuleColorOverLife::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	Context.ParticleBase->Color = Context.ParticleBase->BaseColor;
}

void UParticleModuleColorOverLife::Update(const FUpdateContext& Context)
{
	FParticleEmitterInstance& Owner = Context.Owner;
	if (!Owner.ParticleData || !Owner.ParticleIndices)
	{
		return;
	}

	for (int32 ParticleIndex = 0; ParticleIndex < Owner.ActiveParticles; ++ParticleIndex)
	{
		FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(
			Owner.ParticleData + Owner.ParticleStride * Owner.ParticleIndices[ParticleIndex]);
		if (!Particle)
		{
			continue;
		}

		const float T = std::clamp(Particle->RelativeTime, 0.0f, 1.0f);
		const FLinearColor& Base = Particle->BaseColor;
		if (ColorOverLifeDistribution.UsesCurve() || AlphaOverLifeDistribution.UsesCurve())
		{
			const FVector CurveColor = ColorOverLifeDistribution.Evaluate(T);
			const float CurveAlpha = std::clamp(AlphaOverLifeDistribution.Evaluate(T), 0.0f, 1.0f);
			Particle->Color = FLinearColor(CurveColor.X, CurveColor.Y, CurveColor.Z, CurveAlpha);
		}
		else
		{
			const float ClampedAlphaOverLife = std::clamp(AlphaOverLife, 0.0f, 1.0f);
			Particle->Color = FLinearColor(
				Base.R + (ColorOverLife.X - Base.R) * T,
				Base.G + (ColorOverLife.Y - Base.G) * T,
				Base.B + (ColorOverLife.Z - Base.B) * T,
				Base.A + (ClampedAlphaOverLife - Base.A) * T);
		}
	}
}

UParticleModule* UParticleModuleColorOverLife::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleColorOverLife* Copy = GUObjectArray.CreateObject<UParticleModuleColorOverLife>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->ColorOverLife = ColorOverLife;
	Copy->ColorOverLifeDistribution = ColorOverLifeDistribution;
	Copy->AlphaOverLife = AlphaOverLife;
	Copy->AlphaOverLifeDistribution = AlphaOverLifeDistribution;
	return Copy;
}

UParticleModuleColorScaleOverLife::UParticleModuleColorScaleOverLife()
{
	bUpdateModule = true;
	ColorScaleOverLifeDistribution.SetConstant(ColorScaleOverLife);
	AlphaScaleOverLifeDistribution.SetConstant(AlphaScaleOverLife);
}

void UParticleModuleColorScaleOverLife::Update(const FUpdateContext& Context)
{
	FParticleEmitterInstance& Owner = Context.Owner;
	if (!Owner.ParticleData || !Owner.ParticleIndices)
	{
		return;
	}

	for (int32 ParticleIndex = 0; ParticleIndex < Owner.ActiveParticles; ++ParticleIndex)
	{
		FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(
			Owner.ParticleData + Owner.ParticleStride * Owner.ParticleIndices[ParticleIndex]);
		if (!Particle)
		{
			continue;
		}

		const float T = std::clamp(Particle->RelativeTime, 0.0f, 1.0f);
		FVector Scale;
		float AlphaScale = 1.0f;
		if (ColorScaleOverLifeDistribution.UsesCurve() || AlphaScaleOverLifeDistribution.UsesCurve())
		{
			Scale = ColorScaleOverLifeDistribution.Evaluate(T);
			Scale.X = (std::max)(0.0f, Scale.X);
			Scale.Y = (std::max)(0.0f, Scale.Y);
			Scale.Z = (std::max)(0.0f, Scale.Z);
			AlphaScale = (std::max)(0.0f, AlphaScaleOverLifeDistribution.Evaluate(T));
		}
		else
		{
			const FVector ClampedColorScale(
				(std::max)(0.0f, ColorScaleOverLife.X),
				(std::max)(0.0f, ColorScaleOverLife.Y),
				(std::max)(0.0f, ColorScaleOverLife.Z));
			const float ClampedAlphaScale = (std::max)(0.0f, AlphaScaleOverLife);
			Scale = FVector(
				1.0f + (ClampedColorScale.X - 1.0f) * T,
				1.0f + (ClampedColorScale.Y - 1.0f) * T,
				1.0f + (ClampedColorScale.Z - 1.0f) * T);
			AlphaScale = 1.0f + (ClampedAlphaScale - 1.0f) * T;
		}

		Particle->Color = FLinearColor(
			Particle->Color.R * Scale.X,
			Particle->Color.G * Scale.Y,
			Particle->Color.B * Scale.Z,
			Particle->Color.A * AlphaScale);
	}
}

UParticleModule* UParticleModuleColorScaleOverLife::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleColorScaleOverLife* Copy = GUObjectArray.CreateObject<UParticleModuleColorScaleOverLife>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->ColorScaleOverLife = ColorScaleOverLife;
	Copy->ColorScaleOverLifeDistribution = ColorScaleOverLifeDistribution;
	Copy->AlphaScaleOverLife = AlphaScaleOverLife;
	Copy->AlphaScaleOverLifeDistribution = AlphaScaleOverLifeDistribution;
	return Copy;
}

UParticleModuleSize::UParticleModuleSize()
{
	bSpawnModule = true;
	StartSizeDistribution.SetUniform(StartSizeMin, StartSizeMax);
}

void UParticleModuleSize::Spawn(const FSpawnContext& Context)
{
	if (!Context.ParticleBase)
	{
		return;
	}

	const FVector SpawnSize = StartSizeDistribution.EvaluateRandom(Context.SpawnTime);
	Context.ParticleBase->BaseSize = SpawnSize;
	Context.ParticleBase->Size = SpawnSize;
}

UParticleModule* UParticleModuleSize::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleSize* Copy = GUObjectArray.CreateObject<UParticleModuleSize>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->StartSize = StartSize;
	Copy->StartSizeMin = StartSizeMin;
	Copy->StartSizeMax = StartSizeMax;
	Copy->StartSizeDistribution = StartSizeDistribution;
	return Copy;
}

UParticleModule* UParticleModuleTypeDataMesh::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleTypeDataMesh* Copy = GUObjectArray.CreateObject<UParticleModuleTypeDataMesh>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->MeshPath = MeshPath;
	Copy->Mesh = Mesh;
	return Copy;
}

uint32 UParticleModuleTypeDataRibbon::RequiredBytes(UParticleModuleTypeDataBase* TypeData)
{
	(void)TypeData;
	return sizeof(FRibbonParticlePayload);
}

UParticleModule* UParticleModuleTypeDataRibbon::CloneForLOD(UParticleLODLevel* NewOuter) const
{
	UParticleModuleTypeDataRibbon* Copy = GUObjectArray.CreateObject<UParticleModuleTypeDataRibbon>(NewOuter);
	CopyModuleBaseTo(Copy);
	Copy->MaxTessellationBetweenParticles = MaxTessellationBetweenParticles;
	Copy->SheetsPerTrail = SheetsPerTrail;
	Copy->MaxTrailCount = MaxTrailCount;
	Copy->MaxParticleInTrailCount = MaxParticleInTrailCount;
	Copy->bDeadTrailsOnDeactivate = bDeadTrailsOnDeactivate;
	Copy->bDeadTrailsOnSourceLoss = bDeadTrailsOnSourceLoss;
	Copy->bClipSourceSegment = bClipSourceSegment;
	Copy->bEnablePreviousTangentRecalculation = bEnablePreviousTangentRecalculation;
	Copy->bTangentRecalculationEveryFrame = bTangentRecalculationEveryFrame;
	Copy->bSpawnInitialParticle = bSpawnInitialParticle;
	Copy->RenderAxis = RenderAxis;
	Copy->TangentSpawningScalar = TangentSpawningScalar;
	Copy->bRenderGeometry = bRenderGeometry;
	Copy->bRenderSpawnPoints = bRenderSpawnPoints;
	Copy->bRenderTangents = bRenderTangents;
	Copy->bRenderTessellation = bRenderTessellation;
	Copy->TilingDistance = TilingDistance;
	Copy->DistanceTessellationStepSize = DistanceTessellationStepSize;
	Copy->bEnableTangentDiffInterpScale = bEnableTangentDiffInterpScale;
	Copy->TangentTessellationScalar = TangentTessellationScalar;
	Copy->Width = Width;
	Copy->Color = Color;
	Copy->Alpha = Alpha;
	Copy->bUseSourceEmitter = bUseSourceEmitter;
	Copy->SourceEmitterName = SourceEmitterName;
	Copy->SourceTrailLifetime = SourceTrailLifetime;
	Copy->SourceSampleInterval = SourceSampleInterval;
	Copy->SourceMinSampleDistance = SourceMinSampleDistance;
	Copy->SourceWidthScale = SourceWidthScale;
	return Copy;
}
