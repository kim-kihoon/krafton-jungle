#pragma once

#include "Core/Types/CoreTypes.h"

class UBodySetup;
class UPrimitiveComponent;
struct FPhysicsBodyDebugInfo;

namespace physx
{
	class PxMaterial;
	class PxPhysics;
	class PxRigidActor;
	class PxScene;
	class PxTriangleMesh;
}

// UE: FInitBodySpawnParams — spawn/simulation policy for InitBody.
struct FInitBodySpawnParams
{
	explicit FInitBodySpawnParams(const UPrimitiveComponent* PrimComp = nullptr);

	bool bStaticPhysics = true;
	bool bPhysicsTypeDeterminesSimulation = false;
};

// PhysX backend scene injection for this repo's IPhysicsScene adapter.
// UE passes FPhysScene* separately to InitBody instead of bundling it here.
struct FBodyInstanceInitParams
{
	physx::PxPhysics* Physics = nullptr;
	physx::PxScene* Scene = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
};

struct FBodyInstance
{
	UPrimitiveComponent* OwnerComponent = nullptr;
	UBodySetup* BodySetup = nullptr;

	physx::PxRigidActor* Actor = nullptr;
	TArray<physx::PxTriangleMesh*> RuntimeTriangleMeshes;

	bool bSimulatePhysics = false;
	bool bEnableCollision = true;
	float Mass = 10.0f;

	void InitBody(
		UBodySetup* InBodySetup,
		UPrimitiveComponent* InOwnerComponent,
		const FBodyInstanceInitParams& InitParams,
		const FInitBodySpawnParams& SpawnParams = FInitBodySpawnParams());
	void TermBody(const FBodyInstanceInitParams& InitParams);

	void CreateShapesFromBodySetup(const FBodyInstanceInitParams& InitParams);
	void CreateShapesFromComponent(const FBodyInstanceInitParams& InitParams);
	bool CreateShapesFromStaticMeshComplex(const FBodyInstanceInitParams& InitParams);

	void SyncBodyToComponent();
	void SyncComponentToBody();

	bool IsValidBodyInstance() const { return Actor != nullptr; }

	void GatherDebugInfo(FPhysicsBodyDebugInfo& OutInfo) const;
};
