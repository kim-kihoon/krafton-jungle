#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectMacros.h"

// =============================================================================
// ParticleEvents.h
//   Particle 가 발생시키는 이벤트 (Spawn/Death/Collision/Burst) 의 payload.
//   UParticleModuleEventGenerator 가 EmitterInstance 에서 적재하고
//   AParticleEventManager (or PSC) 가 디스패치한다.
// =============================================================================

UENUM()
enum class EParticleEventType : uint8
{
	Spawn,
	Death,
	Collision,
	Burst,
};

struct FParticleEventDataBase
{
	EParticleEventType Type;
	FName EventName;
	float TimeSeconds = 0.0f;
	FVector Location = { 0, 0, 0 };
	FVector Velocity = { 0, 0, 0 };
};

struct FParticleEventSpawnData : FParticleEventDataBase
{
	int32 ParticleCount = 1;
};

struct FParticleEventDeathData : FParticleEventDataBase
{
	float ParticleAge = 0.0f;
};

struct FParticleEventCollideData : FParticleEventDataBase
{
	FVector Normal = { 0, 0, 1 };
	FVector ImpactVelocity = { 0, 0, 0 };
	int32 Item = 0; // optional: hit primitive id
};

struct FParticleEventBurstData : FParticleEventDataBase
{
	int32 ParticleCount = 0;
};
