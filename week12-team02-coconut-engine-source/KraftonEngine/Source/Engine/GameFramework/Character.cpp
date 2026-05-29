#include "Character.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/Archive.h"
#include "Animation/AnimGraphInstance.h"
#include <Component/CameraComponent.h>

void ACharacter::BeginPlay()
{
	ConfigureTickOrder();
	SyncSpringArmRotationMode();
	APawn::BeginPlay();
}

void ACharacter::Tick(float DeltaTime)
{
	APawn::Tick(DeltaTime);

	// 착지 감지: 지난 프레임 Falling → 이번 프레임 Walking
	const bool bIsFallingNow = CharacterMovement->IsFalling();
	if (bWasFallingLastFrame && !bIsFallingNow)
	{
		LandingTimer = LandingDuration;
	}
	bWasFallingLastFrame = bIsFallingNow;

	// Land 타이머 소진
	if (LandingTimer > 0.0f)
	{
		LandingTimer -= DeltaTime;
		if (LandingTimer < 0.0f) LandingTimer = 0.0f;
	}

	UAnimGraphInstance* AnimInst = Cast<UAnimGraphInstance>(Mesh->GetAnimInstance());
	if (AnimInst)
	{
		AnimInst->SetFloatParameter("Speed", CharacterMovement->GetSpeed2D());
		AnimInst->SetFloatParameter("JumpState", GetJumpState());
	}

	SyncSpringArmRotationMode();
}

void ACharacter::InitDefaultComponents()
{
	CapsuleComponent = AddComponent<UCapsuleComponent>();
	SetRootComponent(CapsuleComponent);

	SpringArm = AddComponent<USpringArmComponent>();
	GetRootComponent()->AddChild(SpringArm);
	SpringArm->TargetArmLength = 10;
	SpringArm->TargetOffset = FVector(0.f, 0.f, 10.f);

	Camera = AddComponent<UCameraComponent>();
	SpringArm->AddChild(Camera);
	Camera->SetRelativeRotation(FVector(0.f, 45.f, 0.f));

	CharacterMovement = AddComponent<UCharacterMovementComponent>();
	CharacterMovement->SetUpdatedComponent(GetRootComponent());
	ConfigureTickOrder();
	SyncSpringArmRotationMode();

	LuaScript = AddComponent<ULuaScriptComponent>();
	LuaScript->SetScriptFile("PlayerController.lua");
	ConfigureTickOrder();
}

void ACharacter::PostDuplicate()
{
	CapsuleComponent = Cast<UCapsuleComponent>(GetRootComponent());
	if (!CapsuleComponent)
	{
		CapsuleComponent = GetComponentByClass<UCapsuleComponent>();
	}

	Mesh = GetComponentByClass<USkeletalMeshComponent>();
	SpringArm = GetComponentByClass<USpringArmComponent>();
	Camera = GetComponentByClass<UCameraComponent>();
	CharacterMovement = GetComponentByClass<UCharacterMovementComponent>();
	LuaScript = GetComponentByClass<ULuaScriptComponent>();

	if (CharacterMovement && CapsuleComponent)
	{
		CharacterMovement->SetUpdatedComponent(CapsuleComponent);
	}

	ConfigureTickOrder();
	SyncSpringArmRotationMode();

	if (LuaScript && LuaScript->GetScriptFile().empty())
	{
		LuaScript->SetScriptFile("PlayerController.lua");
	}
}

void ACharacter::PossessedBy(APlayerController* PC)
{
	APawn::PossessedBy(PC);
}

void ACharacter::UnPossessed()
{
	APawn::UnPossessed();
}

void ACharacter::ConfigureTickOrder()
{
	if (LuaScript)
	{
		// 입력은 이동보다 먼저 갱신한다. CMC는 이 프레임의 Lua 입력을 소비한다.
		LuaScript->PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	}

	if (CharacterMovement)
	{
		// 루트 이동/회전은 카메라 보정 전에 끝나야 한다.
		CharacterMovement->PrimaryComponentTick.SetTickGroup(TG_DuringPhysics);
	}

	PrimaryActorTick.SetTickGroup(TG_PostUpdateWork);
	if (Mesh)
	{
		// ACharacter::Tick에서 갱신한 애니메이션 파라미터를 같은 프레임에 소비한다.
		Mesh->PrimaryComponentTick.SetTickGroup(TG_PostUpdateWork);
	}

	if (SpringArm)
	{
		// SpringArm은 루트 회전 이후에 월드 고정 yaw를 상대 transform으로 환산한다.
		SpringArm->PrimaryComponentTick.SetTickGroup(TG_PostUpdateWork);
	}
}

// ── 점프 ─────────────────────────────────────────────────────
void ACharacter::Jump()
{
	if (!CharacterMovement || !CharacterMovement->IsMovingOnGround())
	{
		return;
	}
	CharacterMovement->Jump();
}

// ── 상태 조회 ─────────────────────────────────────────────────

bool ACharacter::IsJumping() const
{
	return CharacterMovement
		&& CharacterMovement->IsFalling()
		&& CharacterMovement->GetVelocity().Z > 0.0f;
}

bool ACharacter::IsOnGround() const
{
	return CharacterMovement && CharacterMovement->IsMovingOnGround();
}

void ACharacter::SyncSpringArmRotationMode()
{
	if (!SpringArm || !CharacterMovement)
	{
		return;
	}

	// 카메라는 캐릭터 루트 회전 상속 대신 controller pitch/yaw를 직접 사용
	SpringArm->bInheritParentRotation = false;
	SpringArm->SetFixedWorldRotation(
		CharacterMovement->GetControllerDesiredPitch(),
		CharacterMovement->GetControllerDesiredYaw());
}

float ACharacter::GetJumpState() const
{
	if (LandingTimer > 0.0f)                          return 3.0f; // Land
	if (CharacterMovement->IsFalling())
	{
		return (CharacterMovement->GetVelocity().Z > 0.0f) ? 1.0f : 2.0f;
	}
	return 0.0f; // Ground
}
