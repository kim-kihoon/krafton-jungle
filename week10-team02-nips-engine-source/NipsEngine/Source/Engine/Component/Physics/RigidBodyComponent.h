#pragma once

#include "Component/ActorComponent.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

class USceneComponent;

enum class EPhysicsBodyType : int32
{
	Static = 0,
	Dynamic = 1,
	Kinematic = 2
};

class URigidBodyComponent : public UActorComponent
{
public:
	DECLARE_CLASS(URigidBodyComponent, UActorComponent)

	URigidBodyComponent();

	void BeginPlay() override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void SetUpdatedComponent(USceneComponent* InComponent) { UpdatedComponent = InComponent; }
	USceneComponent* GetUpdatedComponent() const;

	EPhysicsBodyType GetBodyType() const { return static_cast<EPhysicsBodyType>(BodyType); }
	void SetBodyType(EPhysicsBodyType InBodyType) { BodyType = static_cast<int32>(InBodyType); ClampEditableValues(); }
	bool IsStaticBody() const { return GetBodyType() == EPhysicsBodyType::Static; }
	bool IsDynamicBody() const { return GetBodyType() == EPhysicsBodyType::Dynamic; }
	bool IsKinematicBody() const { return GetBodyType() == EPhysicsBodyType::Kinematic; }

	void SetSimulatePhysics(bool bInSimulate) { bSimulatePhysics = bInSimulate; }
	void SetUseGravity(bool bInUseGravity) { bUseGravity = bInUseGravity; }
	bool IsSimulatingPhysics() const { return bSimulatePhysics; }
	bool IsUsingJoltPhysics() const { return JoltBodyHandle != InvalidJoltBodyHandle; }

	void SetHeldByPhysicsHandle(bool bHeld);
	bool IsHeldByPhysicsHandle() const { return bHeldByPhysicsHandle; }

	bool CanBePickedUp() const { return IsDynamicBody() && bCanBePickedUp; }
	void SetCanBePickedUp(bool bInCanBePickedUp) { bCanBePickedUp = bInCanBePickedUp; }

	const FVector& GetVelocity() const { return Velocity; }
	void SetVelocity(const FVector& InVelocity);
	const FVector& GetAngularVelocity() const { return AngularVelocity; }
	void SetAngularVelocity(const FVector& InAngularVelocity) { AngularVelocity = InAngularVelocity; }
	void AddImpulse(const FVector& Impulse);
	void NotifyBlockingPushOut(const FVector& PushDelta);

	FVector GetPhysicsLocation() const;
	void SetPhysicsLocation(const FVector& NewLocation);
	void SetPhysicsRotation(const FQuat& NewRotation);

	void PlayPickupSound() const;
	void PlayDropSound() const;
	void QueueDropSound(const FString& SoundPath);
	const FString& GetDropSoundPath() const { return DropSoundPath; }

	float GetMass() const { return Mass; }
	float GetGravityScale() const { return GravityScale; }
	float GetLinearDamping() const { return LinearDamping; }
	float GetAngularDamping() const { return AngularDamping; }
	float GetMaxSpeed() const { return MaxSpeed; }
	float GetMaxAngularSpeed() const { return MaxAngularSpeed; }
	bool IsGravityEnabled() const { return IsDynamicBody() && bUseGravity; }

	uint32 GetJoltBodyHandle() const { return JoltBodyHandle; }
	void SetJoltBodyHandle(uint32 InBodyHandle) { JoltBodyHandle = InBodyHandle; }
	void ClearJoltBodyHandle() { JoltBodyHandle = InvalidJoltBodyHandle; }

protected:
	void TickComponent(float DeltaTime) override;

private:
	void ClampEditableValues();
	void ApplyBlockingResponse();
	void TryPlayPendingDropSound();

	static constexpr uint32 InvalidJoltBodyHandle = 0xffffffffu;

	USceneComponent* UpdatedComponent = nullptr;
	FVector Velocity = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;
	uint32 JoltBodyHandle = InvalidJoltBodyHandle;

	int32 BodyType = static_cast<int32>(EPhysicsBodyType::Dynamic);
	bool bSimulatePhysics = true;
	bool bUseGravity = true;
	bool bCanBePickedUp = true;
	bool bHeldByPhysicsHandle = false;
	bool bWasSimulatingBeforeHold = true;
	bool bGrounded = false;
	bool bGroundPushOutSinceLastTick = false;

	float Mass = 1.0f;
	float GravityScale = 1.0f;
	float LinearDamping = 1.5f;
	float MaxSpeed = 50.0f;
	float SleepSpeed = 0.03f;
	float AngularDamping = 1.5f;
	float MaxAngularSpeed = 180.0f;

	FString PickupSoundPath;
	FString DropSoundPath;
	FString PendingDropSoundPath;
	bool bDropSoundPending = false;
	bool bDropSoundObservedFalling = false;
};
