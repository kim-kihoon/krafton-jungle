#pragma once

#include "Component/Movement/MovementComponent.h"
#include "Math/Vector.h"

class URigidBodyComponent;

class UCharacterMovementComponent : public UMovementComponent
{
public:
	DECLARE_CLASS(UCharacterMovementComponent, UMovementComponent)

	UCharacterMovementComponent();

	void BeginPlay() override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	float GetMaxSpeed() const override { return MaxWalkSpeed; }

	bool IsGrounded() const { return bGrounded; }
	void Jump();
	void SetSpeedMultiplier(float InSpeedMultiplier);
	void SetRigidBody(URigidBodyComponent* InRigidBody) { RigidBody = InRigidBody; }

protected:
	void TickComponent(float DeltaTime) override;

private:
	void RefreshUpdatedReferences();
	void ClampEditableValues();
	float MoveToward(float Current, float Target, float MaxDelta) const;
	void UpdateFootsteps(float DeltaTime, const FVector& ActualDelta);
	void PlayFootstep(float VolumeScale);
	void PlayJumpSound();
	void PlayLandingSound();

private:
	URigidBodyComponent* RigidBody = nullptr;

	float MaxWalkSpeed = 3.5f;
	float JumpSpeed = 5.5f;
	float SpeedMultiplier = 1.0f;
	float Acceleration = 18.0f;
	float BrakingDeceleration = 24.0f;
	float GravityScale = 1.0f;
	float MaxFallSpeed = 18.0f;
	float GroundStickVelocity = -0.5f;
	float GroundProbeDistance = 0.08f;
	bool bEnableFootsteps = true;
	float FootstepVolume = 0.8f;
	float FootstepStepDistance = 1.70f;
	float FootstepMinSpeed = 0.35f;
	float SprintFootstepStepDistanceScale = 1.65f;
	float SprintFootstepVolumeScale = 1.18f;
	float FootstepAccumulatedDistance = 0.0f;
	int32 FootstepIndex = 0;
	bool bGrounded = false;
};
