#pragma once

#include "Core/Types/CoreTypes.h"
#include "Profiling/Stats/Stats.h"

#if STATS
struct FPhysicsStats
{
	static uint32 RagdollActiveComponentCount;
	static uint32 RagdollBodyCount;
	static uint32 RagdollConstraintCount;
	static uint32 RagdollInvalidBodyCount;
	static uint32 RagdollInvalidConstraintCount;
	static uint32 RagdollInstantiateCount;
	static uint32 RagdollTermCount;

	static void ResetFrame()
	{
		RagdollActiveComponentCount = 0;
		RagdollBodyCount = 0;
		RagdollConstraintCount = 0;
		RagdollInvalidBodyCount = 0;
		RagdollInvalidConstraintCount = 0;
		RagdollInstantiateCount = 0;
		RagdollTermCount = 0;
	}
};

#define PHYSICS_STATS_RESET_FRAME() FPhysicsStats::ResetFrame()
#define PHYSICS_STATS_ADD_RAGDOLL_COMPONENT(ValidBodies, InvalidBodies, ValidConstraints, InvalidConstraints) \
	do \
	{ \
		++FPhysicsStats::RagdollActiveComponentCount; \
		FPhysicsStats::RagdollBodyCount += (ValidBodies); \
		FPhysicsStats::RagdollInvalidBodyCount += (InvalidBodies); \
		FPhysicsStats::RagdollConstraintCount += (ValidConstraints); \
		FPhysicsStats::RagdollInvalidConstraintCount += (InvalidConstraints); \
	} while (0)
#define PHYSICS_STATS_ADD_RAGDOLL_INSTANTIATE() ++FPhysicsStats::RagdollInstantiateCount
#define PHYSICS_STATS_ADD_RAGDOLL_TERM()        ++FPhysicsStats::RagdollTermCount
#else
#define PHYSICS_STATS_RESET_FRAME() ((void)0)
#define PHYSICS_STATS_ADD_RAGDOLL_COMPONENT(ValidBodies, InvalidBodies, ValidConstraints, InvalidConstraints) ((void)0)
#define PHYSICS_STATS_ADD_RAGDOLL_INSTANTIATE() ((void)0)
#define PHYSICS_STATS_ADD_RAGDOLL_TERM()        ((void)0)
#endif
