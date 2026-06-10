#pragma once

#include "Physics/IPhysicsScene.h"
#include "Physics/BodyInstance.h"
#include "Core/Types/CoreTypes.h"
#include <vector>

class AActor;

// Forward declarations — PhysX types
namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxScene;
	class PxDefaultCpuDispatcher;
	class PxMaterial;
	class PxRigidActor;
	class PxRigidDynamic;
	class PxVehicleDrive4W;
	class PxVehicleDrivableSurfaceToTireFrictionPairs;
	class PxVehicleDrive4WRawInputData;
	struct PxVehicleWheelQueryResult;
	struct PxWheelQueryResult;
}

class FPhysXSimulationCallback;
class UFourWheeledVehicleMovementComponent;

// ============================================================
// FPhysXPhysicsScene — PhysX 4.1 기반 물리 시스템
//
// IPhysicsScene 인터페이스를 통해 Native와 교체 가능.
// UE-style: 각 UPrimitiveComponent가 자신의 FBodyInstance / PxRigidActor를 소유.
// ============================================================
class FPhysXPhysicsScene : public IPhysicsScene
{
public:
	void Initialize(UWorld* InWorld) override;
	void Shutdown() override;

	void RegisterComponent(UPrimitiveComponent* Comp) override;
	void UnregisterComponent(UPrimitiveComponent* Comp) override;
	void RebuildBody(UPrimitiveComponent* Comp) override;

	void Tick(float DeltaTime) override;

	void AddForce(UPrimitiveComponent* Comp, const FVector& Force) override;
	void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) override;
	void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) override;

	FVector GetLinearVelocity(UPrimitiveComponent* Comp) const override;
	void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;
	FVector GetAngularVelocity(UPrimitiveComponent* Comp) const override;
	void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;

	void SetEnableGravity(UPrimitiveComponent* Comp, bool bEnable) override;

	void SetMass(UPrimitiveComponent* Comp, float Mass) override;
	float GetMass(UPrimitiveComponent* Comp) const override;
	void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) override;
	FVector GetCenterOfMass(UPrimitiveComponent* Comp) const override;

	bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const override;
	bool Sweep(const FVector& Start, const FVector& Dir, float MaxDist, const FCollisionShape& Shape, const FQuat& ShapeRot, FHitResult& OutHit, ECollisionChannel TraceChannel, const AActor* IgnoreActor) const override;
	bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) const override;

	void RegisterVehicle(UFourWheeledVehicleMovementComponent* VehicleComp, const FFourWheeledVehicleRuntimeParams& Params) override;
	void UnregisterVehicle(UFourWheeledVehicleMovementComponent* VehicleComp) override;
	void UpdateVehicle(UFourWheeledVehicleMovementComponent* VehicleComp, const FFourWheeledVehicleRuntimeParams& Params) override;
	bool GetVehicleState(const UFourWheeledVehicleMovementComponent* VehicleComp, FFourWheeledVehicleRuntimeState& OutState) const override;

private:
	struct FPhysXVehicleInstance;
	struct FVehicleSceneQueryResources;
	struct FPendingForceAtLocation
	{
		FVector Force = FVector(0.0f, 0.0f, 0.0f);
		FVector WorldLocation = FVector(0.0f, 0.0f, 0.0f);
	};

	struct FPendingBodyForces
	{
		UPrimitiveComponent* Comp = nullptr;
		FVector Force = FVector(0.0f, 0.0f, 0.0f);
		FVector Torque = FVector(0.0f, 0.0f, 0.0f);
		TArray<FPendingForceAtLocation> ForcesAtLocation;
	};

	static constexpr uint32 MaxVehicles = 8;
	static constexpr uint32 WheelsPerVehicle = 4;

	UWorld* World = nullptr;

	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxScene* Scene = nullptr;
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
	FPhysXSimulationCallback* EventCallback = nullptr;

	TArray<UPrimitiveComponent*> BodyInstanceComponents;
	TArray<FPhysXVehicleInstance*> Vehicles;
	TArray<FPendingBodyForces> PendingBodyForces;

	FVehicleSceneQueryResources* VehicleSceneQuery = nullptr;
	physx::PxVehicleDrivableSurfaceToTireFrictionPairs* VehicleFrictionPairs = nullptr;

	bool bSharedPhysXAcquired = false;
	bool bShutdownComplete = true;
	float PhysicsAccumulator = 0.0f;

	struct FBodyInstanceInitParams MakeBodyInstanceInitParams() const;
	void ReleaseBodyInstances();
	void PruneInvalidBodyInstanceComponents();
	bool ShouldIgnoreActorForQuery(const physx::PxRigidActor* Actor, const AActor* IgnoreActor) const;
	physx::PxRigidDynamic* GetDynamicActorForComponent(UPrimitiveComponent* Comp) const;
	FPendingBodyForces* FindOrAddPendingBodyForces(UPrimitiveComponent* Comp);
	void ClearPendingForcesForComponent(UPrimitiveComponent* Comp);
	void ApplyPendingForces();
	FPhysXVehicleInstance* FindVehicle(UFourWheeledVehicleMovementComponent* VehicleComp) const;
	bool IsVehicleChassisComponent(const UPrimitiveComponent* Comp) const;
	void EnsureVehicleFrictionPairs();
	void EnsureVehicleBatchQuery();
	void ReleaseVehicleSceneQuery();
	void RunVehicleSuspensionRaycasts();
	void RunVehicleUpdates(float DeltaTime);
	void ReleaseVehicles();
	void CacheVehicleAeroParams(FPhysXVehicleInstance& Instance, const FFourWheeledVehicleRuntimeParams& Params) const;
	void ApplySimpleDownforce(FPhysXVehicleInstance& Instance) const;
	void ApplyVehicleTireFriction(const FFourWheeledVehicleRuntimeParams& Params);
};
