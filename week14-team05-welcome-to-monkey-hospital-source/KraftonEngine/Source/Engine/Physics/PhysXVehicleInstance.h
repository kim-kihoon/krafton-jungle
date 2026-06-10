#pragma once

#include "Physics/Vehicle/VehicleTypes.h"

#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleSDK.h>
#include <vehicle/PxVehicleUtilControl.h>
#include <vehicle/PxVehicleUtilSetup.h>
#include <vehicle/PxVehicleUpdate.h>

struct FPhysXVehicleInstance
{
    FVehicleHandle Handle;

    uint32 OwnerActorId             = 0;
    uint32 OwnerComponentId         = 0;
    uint32 OwnerComponentGeneration = 0;

    FVehicleDesc Desc;

    physx::PxRigidDynamic*   ChassisActor = nullptr;
    physx::PxVehicleDrive4W* Vehicle      = nullptr;
    bool                     bChassisActorRegisteredInScene = false;

    physx::PxBatchQuery* BatchQuery = nullptr;

    TArray<physx::PxRaycastQueryResult> RaycastResults;
    TArray<physx::PxRaycastHit>         RaycastHitBuffer;

    TArray<physx::PxWheelQueryResult> WheelQueryResults;
    physx::PxVehicleWheelQueryResult  VehicleQueryResult;

    physx::PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = nullptr;

    physx::PxVehicleDrive4WRawInputData RawInput;
    FVehicleInputState             Input;
    bool                           bReverseGearActive = false;

    FTransform PreviousChassisTransform;
    FTransform CurrentChassisTransform;

    bool bPendingFirstRaycast = true;
};