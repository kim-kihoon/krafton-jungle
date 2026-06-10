#pragma once

#include "Physics/PhysXVehicleInstance.h"
#include "Physics/PhysicsStats.h"
#include "Physics/PhysicsWorldSnapshot.h"

#include <memory>

namespace physx
{
    class PxPhysics;
    class PxScene;
    class PxMaterial;
}

class FPhysXVehicleRuntime
{
public:
    FPhysXVehicleRuntime() = default;
    ~FPhysXVehicleRuntime() = default;

    void Initialize(physx::PxPhysics* InPhysics, physx::PxScene* InScene, physx::PxMaterial* InDefaultMaterial);
    void Shutdown();

    FVehicleHandle ReserveHandle();

    FVehicleHandle CreateVehicle(const FVehicleDesc& Desc);
    void DestroyVehicle(FVehicleHandle Vehicle);
    void SetVehicleInput(FVehicleHandle Vehicle, const FVehicleInputState& Input);
    void ResetVehicle(FVehicleHandle Vehicle, const FTransform& WorldTransform);

    void PreSimulate(float FixedDt);
    void BuildSnapshots(TArray<FVehicleSnapshot>& OutVehicles) const;
    void GatherStats(FPhysicsStats& Stats) const;
    float GetLastRaycastMs() const { return LastRaycastMs; }

private:
    FVehicleHandle AllocateVehicle();
    FPhysXVehicleInstance* ResolveVehicle(FVehicleHandle Handle);
    const FPhysXVehicleInstance* ResolveVehicle(FVehicleHandle Handle) const;
    FPhysXVehicleInstance* ResolveAliveVehicle(FVehicleHandle Handle);
    const FPhysXVehicleInstance* ResolveAliveVehicle(FVehicleHandle Handle) const;
    void FreeVehicle(FVehicleHandle Handle);

private:
    physx::PxPhysics* Physics = nullptr;
    physx::PxScene* Scene = nullptr;
    physx::PxMaterial* DefaultMaterial = nullptr;

    TArray<std::unique_ptr<FPhysXVehicleInstance>> Vehicles;
    TArray<uint32> VehicleGenerations;
    TMap<uint32, FVehicleHandle> ComponentToVehicle;

    float LastRaycastMs = 0.0f;
};
