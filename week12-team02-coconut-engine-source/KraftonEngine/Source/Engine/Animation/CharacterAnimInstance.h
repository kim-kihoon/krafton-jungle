#pragma once
#include "Animation/AnimInstance.h"

class UCharacterMovementComponent;
class ACharacter;

class UCharacterAnimInstance : public UAnimInstance
{
public:
	DECLARE_CLASS(UCharacterAnimInstance, UAnimInstance)

	~UCharacterAnimInstance();

	void Initialize(USkeletalMeshComponent* InOwner, const FString& InScriptPath = "") override;
	void NativeUpdateAnimation(float DeltaSeconds) override;

	// BindProperty가 포인터를 캡처하므로 public
	float Speed      = 0.f;
	float JumpState  = 0.f;   // 0=Ground, 1=Rise, 2=Fall, 3=Land
	bool bIsJumping  = false;
	bool bIsGrounded = true;

private:
	ACharacter*                  OwnerCharacter    = nullptr;
	UCharacterMovementComponent* MovementComponent = nullptr;
};
