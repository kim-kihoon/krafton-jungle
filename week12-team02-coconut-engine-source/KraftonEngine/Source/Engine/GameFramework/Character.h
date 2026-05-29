#pragma once
#include "GameFramework/Pawn.h"
#include "Component/CapsuleComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/LuaScriptComponent.h"
#include "Component/SpringArmComponent.h"
#include "Character.generated.h"

class UCameraComponent;

UCLASS(Actor)
class ACharacter : public APawn
{
public:
	GENERATED_BODY(ACharacter)

	ACharacter() = default;
	~ACharacter() override = default;

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	// TODO: 계속 duplicate 오류나서 임시로 사용
	void InitDefaultComponents();

	void PostDuplicate() override;
	

	// ── 점프 ─────────────────────────────────────────────────
	virtual void Jump();

	// ── 상태 조회 ─────────────────────────────────────────────
	bool IsJumping() const;
	bool IsOnGround() const;

	// JumpState: 0=Ground, 1=Rise, 2=Fall, 3=Land
	float GetJumpState() const;

	// ── 컴포넌트 접근자 ───────────────────────────────────────
	UCapsuleComponent*           GetCapsuleComponent()    const { return CapsuleComponent; }
	USkeletalMeshComponent*      GetMesh()                const { return Mesh; }
	UFUNCTION(Lua)
	UCharacterMovementComponent* GetCharacterMovement()   const { return CharacterMovement; }
	ULuaScriptComponent*         GetLuaScript()           const { return LuaScript; }

protected:
	void PossessedBy(APlayerController* PC) override;
	void UnPossessed() override;

private:
	UCapsuleComponent*				CapsuleComponent  = nullptr;
	USkeletalMeshComponent*			Mesh              = nullptr;
	UCharacterMovementComponent*	CharacterMovement = nullptr;
	USpringArmComponent*			SpringArm         = nullptr;
	ULuaScriptComponent*			LuaScript         = nullptr;

	// 시연용 카메라
	// Character를 상속받은 객체에 CameraComponent를 만들어야함
	UCameraComponent*				Camera = nullptr;

	void ConfigureTickOrder();
	void SyncSpringArmRotationMode();

	// 시연용 state를 위한 변수들
	// 착지 상태 관련
	float LandingTimer = 0.0f;
	float LandingDuration = 0.02f; // 애니메이션 길이에 맞게 조정

	bool bWasFallingLastFrame = false;

};
