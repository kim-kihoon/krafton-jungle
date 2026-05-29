#pragma once

#include "Core/CoreTypes.h"
#include "Core/EngineTypes.h"
#include "Math/Vector.h"
#include "Render/Types/RenderTypes.h"

class UMaterialInterface;
class UParticleSystem;
class UParticleEmitter;
class UParticleSpriteEmitter;
class UParticleLODLevel;
class UParticleModule;
class UParticleModuleTypeDataBase;
class UParticleSystemComponent;
class UStaticMesh;

/*-----------------------------------------------------------------------------
	Particle Dynamic Data
-----------------------------------------------------------------------------*/
enum EDynamicEmitterType
{
	DET_Unknown = 0,
	DET_Sprite,
	DET_Mesh,
	DET_Beam2,
	DET_Ribbon
};

inline int32 AlignParticleDataSize(int32 Size, int32 Alignment)
{
	return (Size + Alignment - 1) & ~(Alignment - 1);
}

#define DECLARE_PARTICLE_PTR(Name, Address) \
	FBaseParticle& Name = *reinterpret_cast<FBaseParticle*>(Address)

#define BEGIN_UPDATE_LOOP \
	for (int32 ParticleIndex = 0; ParticleIndex < Owner->ActiveParticles; ++ParticleIndex) \
	{ \
		DECLARE_PARTICLE_PTR(Particle, Owner->ParticleData + Owner->ParticleStride * Owner->ParticleIndices[ParticleIndex]);

#define END_UPDATE_LOOP \
	}

/*-----------------------------------------------------------------------------
	FBaseParticle
-----------------------------------------------------------------------------*/
// Mappings for 'standard' particle data
// Only used when required.
struct FBaseParticle
{
	// 48 bytes
	FVector			OldLocation;			// Last frame's location, used for collision
	FVector			Location;				// Current location

	// 24 bytes
	FVector			BaseVelocity;			// Velocity = BaseVelocity at the start of each frame.
	FVector			Rotation;				// Rotation of particle (in Radians, X/Roll, Y/Pitch, Z/Yaw)

	// 24 bytes
	FVector			Velocity;				// Current velocity, gets reset to BaseVelocity each frame to allow 
	FVector			BaseRotationRate;		// Initial angular velocity of particle (in Radians per second)

	// 24 bytes
	FVector 		BaseSize;				// Size = BaseSize at the start of each frame
	FVector			RotationRate;			// Current rotation rate, gets reset to BaseRotationRate each frame

	// 16 bytes
	FVector			Size;					// Current size, gets reset to BaseSize each frame
	int32			Flags;					// Flags indicating various particle states

	// 16 bytes
	FLinearColor	Color;					// Current color of particle.

	// 16 bytes
	FLinearColor	BaseColor;				// Base color of the particle

	// 16 bytes
	float			RelativeTime;			// Relative time, range is 0 (==spawn) to 1 (==death)
	float			OneOverMaxLifetime;		// Reciprocal of lifetime
	uint32			ParticleId;				// Stable id for event validation; slots/direct indices may be reused.
	float			Placeholder1;
};

/*-----------------------------------------------------------------------------
	Particle State Flags
-----------------------------------------------------------------------------*/
enum EParticleStates
{
	/** Ignore updates to the particle						*/
	STATE_Particle_JustSpawned = 0x02000000,
	/** Ignore updates to the particle						*/
	STATE_Particle_Freeze = 0x04000000,
	/** Ignore collision updates to the particle			*/
	STATE_Particle_IgnoreCollisions = 0x08000000,
	/**	Stop translations of the particle					*/
	STATE_Particle_FreezeTranslation = 0x10000000,
	/**	Stop rotations of the particle						*/
	STATE_Particle_FreezeRotation = 0x20000000,
	/** Combination for a single check of 'ignore' flags	*/
	STATE_Particle_CollisionIgnoreCheck = STATE_Particle_Freeze | STATE_Particle_IgnoreCollisions | STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation,
	/** Delay collision updates to the particle				*/
	STATE_Particle_DelayCollisions = 0x40000000,
	/** Flag indicating the particle has had at least one collision	*/
	STATE_Particle_CollisionHasOccurred = 0x80000000,
	/** State mask. */
	STATE_Mask = 0xFE000000,
	/** Counter mask. */
	STATE_CounterMask = (~STATE_Mask)
};

struct FParticleEventInstancePayload
{
	uint32 bSpawnEventsPresent : 1 = false;
	uint32 bDeathEventsPresent : 1 = false;
	uint32 bCollisionEventsPresent : 1 = false;
	uint32 bBurstEventsPresent : 1 = false;

	int32 SpawnTrackingCount = 0;
	int32 DeathTrackingCount = 0;
	int32 CollisionTrackingCount = 0;
	int32 BurstTrackingCount = 0;
};
