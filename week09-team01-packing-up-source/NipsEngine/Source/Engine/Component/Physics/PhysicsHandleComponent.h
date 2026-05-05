#pragma once

#include "Component/ActorComponent.h"
#include "Core/CollisionTypes.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

class FViewportCamera;
class URigidBodyComponent;
class UWorld;

class UPhysicsHandleComponent : public UActorComponent
{
public:
	DECLARE_CLASS(UPhysicsHandleComponent, UActorComponent)

	UPhysicsHandleComponent();

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	bool TryGrab(UWorld* World, const FViewportCamera* Camera);
	bool TryGrab(UWorld* World, const FVector& CameraLocation, const FVector& CameraForward);
	URigidBodyComponent* FindPickableBody(UWorld* World, const FVector& CameraLocation, const FVector& CameraForward, FHitResult* OutHit = nullptr) const;
	void Release();
	void TickHandle(float DeltaTime, const FViewportCamera* Camera, const FVector& TargetOffset = FVector::ZeroVector, const FQuat* TargetRotation = nullptr, bool bSnapToTarget = false);
	void TickHandle(float DeltaTime, const FVector& CameraLocation, const FVector& CameraForward, const FVector& TargetOffset = FVector::ZeroVector, const FQuat* TargetRotation = nullptr, bool bSnapToTarget = false);

	bool IsHolding() const { return HeldBody != nullptr; }
	URigidBodyComponent* GetHeldBody() const { return HeldBody; }
	void SetHeldMovementCollisionSuppressed(bool bSuppress);

	void SetTraceDistance(float InTraceDistance) { TraceDistance = InTraceDistance; }
	void SetHoldDistance(float InHoldDistance, bool bUseSizeDistanceOffset = true)
	{
		HoldDistance = InHoldDistance;
		if (!bUseSizeDistanceOffset)
		{
			HoldDistanceOffset = 0.0f;
		}
		ClampEditableValues();
		CurrentHoldDistance = HoldDistance + (bUseSizeDistanceOffset ? HoldDistanceOffset : 0.0f);
	}
	void ResetHoldDistance() { HoldDistance = DefaultHoldDistance; ClampEditableValues(); CurrentHoldDistance = HoldDistance + HoldDistanceOffset; }

private:
	URigidBodyComponent* FindRigidBodyFromHit(const FHitResult& Hit) const;
	URigidBodyComponent* FindOwnerRigidBody() const;
	FVector GetHoldTarget(const FVector& CameraLocation, const FVector& CameraForward, const FVector& TargetOffset) const;
	float ComputeHoldDistanceOffset(URigidBodyComponent* Body, const FVector& CameraLocation, const FVector& CameraForward) const;
	void ClampEditableValues();

private:
	URigidBodyComponent* HeldBody = nullptr;
	UWorld* HeldWorld = nullptr;
	FVector HoldLocation = FVector::ZeroVector;
	FVector HoldVelocity = FVector::ZeroVector;
	FVector LastHoldLocation = FVector::ZeroVector;
	float HoldDistanceOffset = 0.0f;
	float CurrentHoldDistance = 0.0f;
	bool bHeldMovementCollisionSuppressed = false;

	float TraceDistance = 5.0f;
	float DefaultHoldDistance = 4.0f;
	float HoldDistance = 4.0f;
	float SizeDistanceScale = 2.0f;
	float MaxSizeDistanceOffset = 5.0f;
	float SpringStrength = 350.0f;
	float Damping = 22.0f;
	float MaxHoldSpeed = 120.0f;
};
