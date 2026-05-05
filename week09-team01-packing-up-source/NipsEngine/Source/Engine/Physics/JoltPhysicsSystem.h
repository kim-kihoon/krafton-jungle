#pragma once

#include "Core/CoreTypes.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

class UPrimitiveComponent;
class URigidBodyComponent;
class UWorld;

class FJoltPhysicsSystem
{
public:
	static FJoltPhysicsSystem& Get();

	bool Initialize();
	void Shutdown();

	bool IsInitialized() const { return bInitialized; }
	bool IsCurrentWorld(const UWorld* World) const { return CurrentWorld == World; }
	bool IsBodyManaged(const URigidBodyComponent* Body) const;

	void RebuildWorld(UWorld* World);
	void Step(UWorld* World, float DeltaTime);

	void SetBodyKinematic(URigidBodyComponent* Body);
	void SetBodyDynamic(URigidBodyComponent* Body);
	void SetBodyTransformFromComponent(URigidBodyComponent* Body);
	void SetHeldBodyOwner(URigidBodyComponent* HeldBody, URigidBodyComponent* HolderBody);
	void ClearHeldBodyOwner(URigidBodyComponent* HeldBody);
	void SetHeldBodyCollisionSuppressed(URigidBodyComponent* HeldBody, bool bSuppressed);
	bool CheckKinematicGround(URigidBodyComponent* Body, float ProbeDistance, float& OutGroundDistance);
	bool MoveKinematicBody(URigidBodyComponent* Body, FVector& InOutTargetLocation, const FQuat& TargetRotation, float DeltaTime);
	bool MoveCharacter(URigidBodyComponent* Body, const FVector& DesiredVelocity, float DeltaTime, float GroundStickDistance, FVector& OutLocation, FVector& OutVelocity, bool& bOutGrounded);
	void SetBodyLinearVelocity(URigidBodyComponent* Body, const FVector& Velocity);
	void AddBodyImpulse(URigidBodyComponent* Body, const FVector& Impulse);

private:
	FJoltPhysicsSystem() = default;
	~FJoltPhysicsSystem();

	FJoltPhysicsSystem(const FJoltPhysicsSystem&) = delete;
	FJoltPhysicsSystem& operator=(const FJoltPhysicsSystem&) = delete;

	void ClearWorld();
	void RegisterStaticActor(class AActor* Actor);
	void RegisterStaticBody(UPrimitiveComponent* ShapeComponent);
	void RegisterDynamicBody(URigidBodyComponent* Body);

private:
	struct FImpl;

	FImpl* Impl = nullptr;
	UWorld* CurrentWorld = nullptr;
	bool bInitialized = false;
};
