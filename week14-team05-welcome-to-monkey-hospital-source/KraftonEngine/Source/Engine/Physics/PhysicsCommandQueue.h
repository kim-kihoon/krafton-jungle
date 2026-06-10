#pragma once

#include "Physics/PhysicsTypes.h"
#include "Physics/Vehicle/VehicleTypes.h"

#include <cstdint>
#include <mutex>

enum class EPhysicsCommandType : uint8
{
    CreateBody,
    CreateConstraint,
    UnregisterComponent,
    RebuildComponentBody,

    // Lifetime
    DestroyBody,
    DestroyConstraint,

    // Transform
    SetTransform,
    SetKinematicTarget,

    // Force / Torque
    AddForce,
    AddForceAtLocation,
    AddTorque,
    AddImpulse,

    // Velocity
    SetVelocity, // linear=VectorValue, angular=VectorValue2 (둘 다 설정)
    SetLinearVelocity,
    SetAngularVelocity,

    // Mass / COM
    SetMass,
    SetCenterOfMass,

    // DOF Lock
    SetLinearLock,
    SetAngularLock,

    // Damping / flags
    SetLinearDamping,
    SetAngularDamping,
    SetGravityEnabled,
    SetCCDEnabled,

    // Sleep
    WakeBody,
    PutBodyToSleep,

    CreateVehicle,
    DestroyVehicle,
    SetVehicleInput,
    ResetVehicle,
};

struct FPhysicsCommand
{
    // 동일 frame 내 명령 순서 보장용 (CreateBody 이후 SetMass/AddForce, DestroyBody 등).
    uint64 Sequence = 0;

    EPhysicsCommandType Type = EPhysicsCommandType::DestroyBody;

    FPhysicsObjectKey Object;

    FPhysicsBodyHandle       Body;
    FPhysicsBodyHandle       ChildBody;
    FPhysicsShapeHandle      Shape;
    FPhysicsConstraintHandle Constraint;

    FPhysicsBodyCreatePayload CreateBody;
    FConstraintCreationDesc   ConstraintDesc;

    FTransform TransformValue;
    FVector    VectorValue  = FVector::ZeroVector;
    FVector    VectorValue2 = FVector::ZeroVector;

    float FloatValue = 0.0f;

    // Frame duration carried across async frame merging. Continuous forces/torques use this
    // instead of a later aggregated DeltaTime, so AddForce remains frame-rate invariant.
    float DurationSeconds = 0.0f;

    bool BoolX = false;
    bool BoolY = false;
    bool BoolZ = false;

    EPhysicsTeleportMode TeleportMode = EPhysicsTeleportMode::None;

    FVehicleHandle     Vehicle;
    FVehicleDesc       VehicleDesc;
    FVehicleInputState VehicleInput;
};

// Thread-safe command queue.
class FPhysicsCommandQueue
{
public:
    uint64 Enqueue(const FPhysicsCommand& Command)
    {
        std::lock_guard<std::mutex> Lock(Mutex);

        FPhysicsCommand Copy = Command;
        Copy.Sequence        = NextSequence++;
        Commands.push_back(Copy);
        return Copy.Sequence;
    }

    bool IsEmpty() const
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        return Commands.empty();
    }

    int32 Num() const
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        return static_cast<int32>(Commands.size());
    }

    void Drain(TArray<FPhysicsCommand>& OutCommands)
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        OutCommands.insert(OutCommands.end(), Commands.begin(), Commands.end());
        Commands.clear();
    }

    void Clear()
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        Commands.clear();
    }

private:
    mutable std::mutex      Mutex;
    TArray<FPhysicsCommand> Commands;
    uint64                  NextSequence = 1;
};
