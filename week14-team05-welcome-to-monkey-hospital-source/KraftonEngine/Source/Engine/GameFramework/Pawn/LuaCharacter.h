#pragma once

#include "GameFramework/Pawn/Character.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Object/Ptr/WeakObjectPtr.h"

class ULuaScriptComponent;
class USpringArmComponent;
class UCameraComponent;
class UPointLightComponent;
class UParticleSystemComponent;

// ACharacter + ULuaScriptComponent — Pawn-level 게임 로직 (BeginPlay/Tick/EndPlay) 을 lua 로 작성.
// AnimInstance lua (ULuaAnimInstance) 와는 책임 분리:
//   - ULuaScriptComponent  : 액터 단위 입력/상태/이벤트 처리
//   - ULuaAnimInstance     : Mesh 의 anim instance — pose 평가/FSM/notify
// 둘 다 lua 라도 environment 가 분리돼 있어 변수 충돌 없음.
//
// 1인칭 카메라: Capsule(Root) → SpringArm(view pivot) → Camera 체인.
// Possess 시 APawn::PossessedBy 가 Camera 를 자동 ActiveCamera 로 잡음.

#include "Source/Engine/GameFramework/Pawn/LuaCharacter.generated.h"

UCLASS()
class ALuaCharacter : public ACharacter
{
public:
	GENERATED_BODY()
	ALuaCharacter() = default;
	~ALuaCharacter() override = default;

	// SkeletalMesh path + Lua script path 를 받아 컴포넌트 구성. ScriptFile 비우면 lua 미부착도 가능.
	void InitDefaultComponents(const FString& SkeletalMeshFileName, const FString& ScriptFile);

	// ACharacter 의 단일 인자 버전 — ScriptFile 미지정.
	void InitDefaultComponents(const FString& SkeletalMeshFileName) override
	{
		InitDefaultComponents(SkeletalMeshFileName, FString());
	}

	void PostDuplicate() override;
	void OnPostLoad(FArchive& Ar) override;
	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	void PlayPistolFireEffect();

	ULuaScriptComponent* GetLuaScriptComponent() const { return LuaScriptComponent; }
	USpringArmComponent* GetSpringArm()          const { return SpringArm; }
	UCameraComponent* GetCamera()             const { return Camera; }

protected:
	void ConfigureFirstPersonViewRig();
	void RefreshLuaCharacterComponentReferences();
	void EnsurePistolMuzzleFlashLight();
	void SetPistolMuzzleFlashVisible(bool bVisible);

	TWeakObjectPtr<ULuaScriptComponent> LuaScriptComponent = nullptr;
	TWeakObjectPtr<USpringArmComponent> SpringArm          = nullptr;
	TWeakObjectPtr<UCameraComponent>    Camera             = nullptr;

	UPROPERTY(Edit, Save, Category="Effects|Pistol", DisplayName="Muzzle Flash Particle", Type=ObjectRef, AllowedClass=UParticleSystemComponent)
	TObjectPtr<UParticleSystemComponent> PistolMuzzleFlashParticle = nullptr;

	UPROPERTY(Edit, Save, Category="Effects|Pistol", DisplayName="Muzzle Flash Point Light", Type=ObjectRef, AllowedClass=UPointLightComponent)
	TObjectPtr<UPointLightComponent> PistolMuzzleFlashLight = nullptr;

	UPROPERTY(Edit, Save, Category="Effects|Pistol", DisplayName="Muzzle Flash Duration", Min=0.0f, Max=1.0f, Speed=0.01f)
	float PistolMuzzleFlashDuration = 0.09f;

	float PistolMuzzleFlashRemaining = 0.0f;
	float PistolMuzzleFlashConfiguredIntensity = 0.0f;
	bool bPistolMuzzleFlashLightInitialized = false;
};
