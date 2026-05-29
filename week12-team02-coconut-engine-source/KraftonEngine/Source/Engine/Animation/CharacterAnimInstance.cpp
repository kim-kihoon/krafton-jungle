#include "CharacterAnimInstance.h"
#include "Animation/LuaAnimStateMachine.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Object/FUObjectArray.h"

IMPLEMENT_CLASS(UCharacterAnimInstance, UAnimInstance)

UCharacterAnimInstance::~UCharacterAnimInstance()
{
	if (StateMachine)
	{
		GUObjectArray.DestroyObject(StateMachine);
		StateMachine = nullptr;
	}
}

void UCharacterAnimInstance::Initialize(USkeletalMeshComponent* InOwner, const FString& InScriptPath)
{
	Super::Initialize(InOwner, InScriptPath);

	if (InOwner)
	{
		OwnerCharacter    = Cast<ACharacter>(InOwner->GetOwner());
		MovementComponent = OwnerCharacter ? OwnerCharacter->GetCharacterMovement() : nullptr;
	}

	if (InScriptPath.empty())
		return;

	ULuaAnimStateMachine* SM = GUObjectArray.CreateObject<ULuaAnimStateMachine>();
	SM->Initialize(InOwner, this);
	SM->LoadScript(InScriptPath);

	// LoadScript가 ScriptEnv를 생성하므로 반드시 그 이후에 바인딩
	SM->BindProperty("speed",      &Speed);
	SM->BindProperty("jumpState",  &JumpState);
	SM->BindProperty("isJumping",  &bIsJumping);
	SM->BindProperty("isGrounded", &bIsGrounded);

	SetStateMachine(SM);
}

void UCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (!MovementComponent || !OwnerCharacter)
		return;

	Speed       = MovementComponent->GetSpeed2D();
	JumpState   = OwnerCharacter->GetJumpState();
	bIsJumping  = OwnerCharacter->IsJumping();
	bIsGrounded = OwnerCharacter->IsOnGround();
}
