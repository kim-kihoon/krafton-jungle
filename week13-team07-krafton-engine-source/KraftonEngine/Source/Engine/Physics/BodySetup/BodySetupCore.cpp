#include "Physics/BodySetup/BodySetupCore.h"

#include "Serialization/Archive.h"

void UBodySetupCore::SerializeCore(FArchive& Ar, UBodySetupCore& BodySetupCore)
{
	Ar << BodySetupCore.BoneName;

	uint8 PhysicsTypeValue = static_cast<uint8>(BodySetupCore.PhysicsType);
	Ar << PhysicsTypeValue;
	if (Ar.IsLoading())
	{
		BodySetupCore.PhysicsType = static_cast<EPhysicsType>(PhysicsTypeValue);
	}

	uint8 TraceFlagValue = static_cast<uint8>(BodySetupCore.CollisionTraceFlag);
	Ar << TraceFlagValue;
	if (Ar.IsLoading())
	{
		BodySetupCore.CollisionTraceFlag = static_cast<ECollisionTraceFlag>(TraceFlagValue);
	}

	uint8 ResponseValue = static_cast<uint8>(BodySetupCore.CollisionResponse);
	Ar << ResponseValue;
	if (Ar.IsLoading())
	{
		BodySetupCore.CollisionResponse = static_cast<EBodyCollisionResponse>(ResponseValue);
	}
}
