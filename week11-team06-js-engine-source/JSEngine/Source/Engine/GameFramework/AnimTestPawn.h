#pragma once

#include "GameFramework/Pawn.h"
#include "Math/Vector2.h"

#include "AAnimTestPawn.generated.h"

class UAnimStateMachineAsset;
class UAnimInstance;
class UCameraComponent;
class USceneComponent;
class USkeletalMeshComponent;
class USpringArmComponent;

UCLASS()
class AAnimTestPawn : public APawn
{
public:
    GENERATED_BODY(AAnimTestPawn, APawn)

    AAnimTestPawn() = default;
    ~AAnimTestPawn() override;

    void InitDefaultComponents() override;
    void BeginPlay() override;
    void Tick(float DeltaTime) override;
    void Serialize(FArchive& Ar) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void HandleAnimNotify(const FAnimNotifyEvent& Notify) override;

    USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComp; }
    UCameraComponent* GetCameraComponent() const { return CameraComp; }

private:
    void LoadConfiguredSkeletalMesh();
    void ApplyCameraSettings();
    void ConfigureLocomotionStateMachine();
    void UpdateLocomotion(float DeltaTime);
    FVector BuildCameraRelativeMoveDirection(const FVector2& MoveAxis) const;
    void UpdateAnimationParameters(float GroundSpeed, bool bMoving, bool bSprinting);
    void AddNotifyToAnimation(UAnimInstance* AnimInstance, const FName& AnimationName, const FName& NotifyName, float TriggerTime);
    bool IsInAttackState() const;
    bool IsInMainAttackState() const;
    void PlayAttackNotifySound(const FAnimNotifyEvent& Notify);
    void PlayLightAttackSound();
    void PlayHeavyAttackSound();
    void PlaySoundAtActor(const FString& SoundPath);

private:
    USceneComponent* SceneRoot = nullptr;
    USkeletalMeshComponent* SkeletalMeshComp = nullptr;
    USpringArmComponent* SpringArmComp = nullptr;
    UCameraComponent* CameraComp = nullptr;
    UAnimStateMachineAsset* LocomotionStateMachine = nullptr;

    FString SkeletalMeshPath = "Asset/SkeletalMesh/GwenFBX/Gwen_skeletalmesh_Buffbone_Cstm_Healthbar.bin";
    FString IdleAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Idle.anm.anim";
    FString IntoRunAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Into_Run.anim";
    FString RunAnimationPath = "Asset/SkeletalMesh/GwenFBX/gwen_anim_Skeleton_Run.anm.anim";
    FString HomeguardAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Gwen_Homeguard.anm.anim";
    FString WalkToIdleAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_IdleIn.anim";
    FString RunToIdleAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Idlein2.anim";
    FString LightAttackAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack1.anim";
    FString HeavyAttackAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack2.anim";
    FString LightAttack3AnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack3.anim";
    FString Attack1ToIdleAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack1_To_Idle.anim";
    FString Attack2ToIdleAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack2_To_Idle.anim";
    FString Attack3ToIdleAnimationPath = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack3_To_Idle.anim";
    FString LightAttackSoundPath1 = "Asset/Audio/Gwen_Original_BasicAttack_1.ogg";
    FString LightAttackSoundPath2 = "Asset/Audio/Gwen_Original_BasicAttack_2.ogg";
    FString LightAttackSoundPath3 = "Asset/Audio/Gwen_Original_BasicAttack_3.ogg";
    FString HeavyAttackSoundPath = "Asset/Audio/Gwen_Original_BasicAttack_0.ogg";
    FString VoiceSoundPath1 = "Asset/Audio/Gwen_Original_Move_6.ogg";
    FString VoiceSoundPath2 = "Asset/Audio/Gwen_Original_Move_8.ogg";
    FString VoiceSoundPath3 = "Asset/Audio/Gwen_Original_Move_10.ogg";
    FString VoiceSoundPath4 = "Asset/Audio/Gwen_Original_Move_20.ogg";
    FString BGMPath = "Asset/Audio/StarGuardian_2017_SpawnMusic.ogg";
    FString IntroSoundPath = "Asset/Audio/league-of-legends-original-sounds-welcome-to-summoners-rift.mp3";

    float MoveSpeed = 7.0f;
    float SprintSpeedMultiplier = 1.5f;
    float LookSensitivityDegrees = 0.12f;
    float CameraInitialYawDegrees = 90.0f;
    float CameraInitialPitchDegrees = -18.0f;
    float CameraPivotHeight = 1.35f;
    float CameraArmLength = 5.0f;
    FVector CameraSocketOffset = FVector(0.0f, 0.55f, 0.25f);
    float MeshTurnSpeedDegreesPerSecond = 540.0f;
    float LocomotionBlendTime = 0.0f;
    float IntoRunDuration = 1.2f;
    float WalkToIdleDuration = 3.533f;
    float RunToIdleDuration = 2.233f;
    float LightAttackDuration = 0.8f;
    float HeavyAttackDuration = 1.2f;
    float LightAttack3Duration = 0.8f;
    float Attack1ToIdleDuration = 2.1f;
    float Attack2ToIdleDuration = 1.967f;
    float Attack3ToIdleDuration = 2.633f;
    float IntroSoundDelay = 10.0f;
    float IntroSoundVolumeScale = 2.0f;
    float MoveStartSpeedThreshold = 0.1f;
    bool bRotateToMovement = true;
    bool bAutoConfigureAnimation = true;
    bool bNextLightAttackUsesAttack1 = true;

    float VoiceTimer = 0.0f;
    float IntroSoundTimer = 0.0f;
    int32 CurrentVoiceIndex = 0;
    bool bIntroSoundPlayed = false;
};
