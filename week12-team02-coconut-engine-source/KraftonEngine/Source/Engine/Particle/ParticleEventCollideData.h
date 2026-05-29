#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Object/FName.h"

class AActor;
class UPrimitiveComponent;

enum EParticleEventType : int
{
	EPET_Any,
	EPET_Spawn,
	EPET_Death,
	EPET_Collision,
	EPET_Burst,
	EPET_Blueprint,
	EPET_MAX,
};

struct FParticleEventData
{
	EParticleEventType Type = EPET_Any;
	FName EventName = FName::None;
	float EmitterTime = 0.0f;

	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
};

struct FParticleExistingData : public FParticleEventData
{
	FVector Direction = FVector::ZeroVector;
	float ParticleTime = 0.0f;
};

struct FParticleEventSpawnData : public FParticleEventData
{
	FParticleEventSpawnData()
	{
		Type = EPET_Spawn;
	}
};

struct FParticleEventDeathData : public FParticleExistingData
{
	FParticleEventDeathData()
	{
		Type = EPET_Death;
	}
};

struct FParticleEventCollideData : public FParticleExistingData
{
	FParticleEventCollideData()
	{
		Type = EPET_Collision;
	}

	int32 EmitterIndex = -1;
	int32 ParticleIndex = -1;
	uint16 ParticleDirectIndex = 0;
	uint32 ParticleId = 0;

	FVector OldLocation = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	float ParticleRelativeTime = 0.0f;
	float HitTime = 0.0f;
	bool bParticleWasKilled = false;

	AActor* HitActor = nullptr;
	UPrimitiveComponent* HitComponent = nullptr;
};

struct FParticleEventBurstData : public FParticleEventData
{
	FParticleEventBurstData()
	{
		Type = EPET_Burst;
	}

	int32 ParticleCount = 0;
};
