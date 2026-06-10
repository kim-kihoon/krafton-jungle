#pragma once

#include "Core/Types/CoreTypes.h"

struct FPhysicsStats
{
    int32 NumBodies          = 0;
    int32 NumStaticBodies    = 0;
    int32 NumDynamicBodies   = 0;
    int32 NumKinematicBodies = 0;

    int32 NumShapes      = 0;
    int32 NumConstraints = 0;

    int32 NumActiveBodies   = 0;
    int32 NumSleepingBodies = 0;

    int32 NumContactPairs = 0;
    int32 NumTriggerPairs = 0;

    int32 NumSubsteps        = 0;
    int32 NumDroppedSubsteps = 0; // Accumulator 가 MaxSubsteps 로 잘려 버린 step 수

    // Command / lifetime
    int32 NumPendingCommands  = 0; // 이번 frame drain 직전 큐에 쌓여 있던 command 수
    int32 NumAppliedCommands  = 0; // 이번 frame 적용된 command 수
    int32 NumDeferredDestroys = 0; // 이번 frame deferred-destroy 로 release 된 body 수

    // Query
    int32 NumRaycasts       = 0;
    int32 NumSweepQueries   = 0;
    int32 NumOverlapQueries = 0;

    float AccumulatorSeconds = 0.0f;
    float InterpolationAlpha = 0.0f;

    // 단계별 타이밍 (substep 합산, ms)
    float PrePhysicsMs          = 0.0f;
    float ApplyCommandsMs       = 0.0f;
    float SyncEngineToPhysicsMs = 0.0f;
    float SimulateMs            = 0.0f;
    float FetchResultsMs        = 0.0f;
    float SyncPhysicsToEngineMs = 0.0f;
    float BuildSnapshotMs       = 0.0f;
    float PostPhysicsMs         = 0.0f;
    float DispatchEventMs       = 0.0f;

    int32 NumVehicles          = 0;
    int32 NumVehicleWheels     = 0;
    int32 NumVehicleWheelInAir = 0;

    float VehicleRaycastMs = 0.0f;
    float VehicleUpdateMs  = 0.0f;
};