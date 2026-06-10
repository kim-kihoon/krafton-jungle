#pragma once

#include "GameFramework/Pawn/Pawn.h"
#include "Core/Types/CollisionTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Physics/PhysicsTypes.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UCharacterMovementComponent;
class UPrimitiveComponent;

enum class ECharacterPhysicsOwnershipMode : uint8
{
	CharacterDriven,
	TransitionToRagdoll,
	RagdollDriven,
	TransitionFromRagdoll
};

enum class ECharacterPhysicsCollisionMode : uint8
{
	CharacterDriven,
	FullRagdollOwned,
	PartialHybrid
};

enum class ECharacterQueryCollisionMode : uint8
{
	CharacterDriven,
	Disabled,
	ReservedForFullRagdollProxy
};

enum class ECharacterGameplayOverlapOwnershipMode : uint8
{
	None,
	CharacterDriven,
	FullRagdollQueryProxy,
	PartialHybrid
};

struct FCharacterCollisionOwnershipSnapshot
{
	ECollisionEnabled CapsuleCollisionEnabled = ECollisionEnabled::NoCollision;
	ECollisionEnabled MeshCollisionEnabled = ECollisionEnabled::NoCollision;
	ECharacterPhysicsCollisionMode PhysicsCollisionMode = ECharacterPhysicsCollisionMode::CharacterDriven;
	ECharacterQueryCollisionMode QueryCollisionMode = ECharacterQueryCollisionMode::CharacterDriven;
};

// UE 의 ACharacter 패턴 — Capsule(Root) → SkeletalMesh + CharacterMovement 의 표준 구성.
//
//   Root: UCapsuleComponent           (충돌/이동 본체. CharacterMovement 의 UpdatedComponent)
//     └ USkeletalMeshComponent (Mesh) (시각화. Animation 시스템과 통합)
//   UCharacterMovementComponent       (non-scene — Capsule 을 UpdatedComponent 로 가리킴)
//
// minimal: Z=0 평지 가정, gravity/jump/floor sweep 없음. 후속 phase 에서 확장.
// LuaScriptComponent 는 이 베이스에 부착하지 않는다 — Lua 로직이 필요하면 ALuaCharacter 사용.

#include "Source/Engine/GameFramework/Pawn/Character.generated.h"

class FArchive;

UCLASS()
class ACharacter : public APawn
{
public:
	GENERATED_BODY()
	ACharacter() = default;
	~ACharacter() override = default;

	// SkeletalMesh fbx (또는 .sketbin path) 받아 default 컴포넌트 구성.
	// 자식 (예: ALuaCharacter) 이 Super 호출 후 자기 컴포넌트 추가 가능.
	virtual void InitDefaultComponents(const FString& SkeletalMeshFileName);

	void OnPostLoad(FArchive& Ar) override;
	void PostDuplicate() override;

	// CharacterMovement->AddInputVector 의 액터 레벨 wrapper. UE 의 APawn::AddMovementInput 대응.
	UFUNCTION(Callable, Category="Character|Movement")
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue = 1.0f);

	// CharacterMovement->Jump 의 액터 레벨 wrapper. Walking 중에만 effective.
	UFUNCTION(Callable, Exec, Category="Character|Movement")
	void Jump();
	void SetMouseSensitivity(float InSensitivity);
	float GetMouseSensitivity() const { return MouseSensitivity; }
	void SetInvertMouseY(bool bInvert);
	bool IsInvertMouseY() const { return bInvertMouseY; }

	UFUNCTION(Callable, Category="Character|Physics")
	bool EnterRagdoll();
	UFUNCTION(Callable, Category="Character|Physics")
	void ExitRagdoll();
	UFUNCTION(Callable, Category="Character|Physics")
	bool BeginRagdollRecovery();
	UFUNCTION(Pure, Category="Character|Physics")
	bool IsInRagdoll() const;
	ECharacterPhysicsOwnershipMode GetPhysicsOwnershipMode() const { return PhysicsOwnershipMode; }
	ECharacterPhysicsCollisionMode GetCharacterPhysicsCollisionMode() const { return CharacterPhysicsCollisionMode; }
	ECharacterQueryCollisionMode GetCharacterQueryCollisionMode() const { return CharacterQueryCollisionMode; }
	ECharacterGameplayOverlapOwnershipMode GetGameplayOverlapOwnershipMode() const { return GameplayOverlapOwnershipMode; }
	UFUNCTION(Pure, Category="Character|Physics")
	bool IsAwaitingRagdollRecoveryRestore() const { return bAwaitingRagdollRecoveryRestore; }
	UFUNCTION(Pure, Category="Character|Physics")
	bool IsUsingFullRagdollQueryProxy() const;
	EPhysicsCollisionRole GetCapsuleCollisionRole() const;
	EPhysicsCollisionRole GetMeshCollisionRole() const;
	EPhysicsCollisionRole GetPartialReactionBodyCollisionRole() const;
	EPhysicsCollisionRole GetCollisionRoleForComponent(const UPrimitiveComponent* Component) const;
	bool IsCapsuleGameplayOverlapOwner() const;
	bool IsMeshGameplayOverlapOwner() const;
	bool ArePartialReactionBodiesGameplayOverlapOwners() const;
	bool IsGameplayOverlapOwnerComponent(const UPrimitiveComponent* Component) const;
	UFUNCTION(Pure, Category="Character|Physics")
	ECollisionEnabled GetCurrentCapsuleCollisionEnabled() const;
	UFUNCTION(Pure, Category="Character|Physics")
	ECollisionEnabled GetCurrentMeshCollisionEnabled() const;

	UFUNCTION(Pure, Category="Character|Components")
	UCapsuleComponent* GetCapsuleComponent()  const { return CapsuleComponent; }
	UFUNCTION(Pure, Category="Character|Components")
	USkeletalMeshComponent* GetMesh()              const { return Mesh; }
	UFUNCTION(Pure, Category="Character|Components")
	UCharacterMovementComponent* GetCharacterMovement() const { return CharacterMovement; }

	// 자동 WASD 매핑/binding — SetupInputComponent 가 InputComponent 에 등록.
	// 게임에선 보통 false 로 끄고 자식이 자기 매핑/binding 추가. 데모 편의용 기본 true.
	UPROPERTY(Edit, Save, Category="Character|Input", DisplayName="Auto Input WASD")
	bool bAutoInputWASD = true;

	// 자동 Space jump 매핑/binding. FPS처럼 Space를 장비 전환에 쓰는 캐릭터는 false로 둔다.
	UPROPERTY(Edit, Save, Category="Character|Input", DisplayName="Auto Input Jump")
	bool bAutoInputJump = true;

	// 자동 mouse look — InputComponent 의 MouseX / MouseY axis 를 통해 APawn::ControlRotation 누적.
	// capsule 자체 회전은 안 함 — SpringArm 의 bUsePawnControlRotation 가 ControlRotation 사용해
	// 카메라만 회전. WASD 도 ControlRotation.Yaw 기준 forward/right 로 이동.
	bool  bAutoInputMouseLook = true;
	bool  bInvertMouseY       = false;
	float MouseSensitivity    = 0.2f;   // deg / pixel — yaw/pitch 공통
	float MinCameraPitch      = -80.0f; // 위 한도 (마이너스 = 위)
	float MaxCameraPitch      =  60.0f; // 아래 한도

protected:
	// InputComponent 가 부착된 후 호출 — WASD axis mapping + AddMovementInput binding 등록.
	void SetupInputComponent() override;

	// ControlRotation → RootComponent 동기화 등 per-frame 처리.
	void Tick(float DeltaTime) override;
	void SavePreRagdollCharacterState();
	void ApplyCharacterPhysicsCollisionMode(ECharacterPhysicsCollisionMode NewMode, const char* Context = nullptr);
	void ApplyCharacterQueryCollisionMode(ECharacterQueryCollisionMode NewMode, const char* Context = nullptr);
	void ApplyCharacterCollisionOwnershipModes(
		ECharacterPhysicsCollisionMode NewPhysicsMode,
		ECharacterQueryCollisionMode NewQueryMode,
		const char* Context);
	void ApplyCharacterDrivenCollisionPolicy();
	void ApplyFullRagdollCollisionPolicy();
	void ApplyPartialRagdollCollisionPolicy();
	void ReconcileCharacterCollisionOwnership();
	ECollisionEnabled ResolveCharacterDrivenCapsuleCollisionEnabled() const;
	ECollisionEnabled ResolveCharacterDrivenMeshCollisionEnabled() const;
	ECollisionEnabled ResolveCapsuleCollisionEnabledForCurrentOwnership() const;
	ECollisionEnabled ResolveMeshCollisionEnabledForCurrentOwnership() const;
	void SuspendCharacterForRagdoll();
	void RestoreMovementAfterRagdoll();
	void FinalizePendingRagdollEntry();
	void CacheRagdollRestoreLocation();
	void RestoreCharacterAfterRagdoll();
	void RefreshCharacterComponentReferences();

	TWeakObjectPtr<UCapsuleComponent>           CapsuleComponent  = nullptr;
	TWeakObjectPtr<USkeletalMeshComponent>      Mesh              = nullptr;
	TWeakObjectPtr<UCharacterMovementComponent> CharacterMovement = nullptr;
	ECharacterPhysicsOwnershipMode PhysicsOwnershipMode = ECharacterPhysicsOwnershipMode::CharacterDriven;
	ECharacterPhysicsCollisionMode CharacterPhysicsCollisionMode = ECharacterPhysicsCollisionMode::CharacterDriven;
	ECharacterQueryCollisionMode CharacterQueryCollisionMode = ECharacterQueryCollisionMode::CharacterDriven;
	ECharacterGameplayOverlapOwnershipMode GameplayOverlapOwnershipMode = ECharacterGameplayOverlapOwnershipMode::CharacterDriven;
	bool bPendingRagdollBodyEnable = false;
	bool bAwaitingRagdollRecoveryRestore = false;
	bool bSavedPreRagdollCharacterState = false;
	FCharacterCollisionOwnershipSnapshot SavedPreRagdollCollisionOwnership;
	bool bSavedMovementTickEnabled = true;
	bool bSavedMovementActive = true;
	bool bHasCachedRagdollRestoreLocation = false;
	FVector CachedRagdollRestoreLocation = FVector::ZeroVector;
	bool bHasCachedRagdollRestoreYaw = false;
	float CachedRagdollRestoreYawDegrees = 0.0f;
};
