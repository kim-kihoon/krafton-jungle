#include "GameFramework/AnimTestPawn.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimationSequenceBase.h"
#include "Animation/AnimStateMachineAsset.h"
#include "Animation/AnimStateMachineNode.h"
#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/SpringArmComponent.h"
#include "Core/Logging/Log.h"
#include "Core/ResourceManager.h"
#include "Engine/Input/GameplayInputTypes.h"
#include "GameFramework/PlayerController.h"
#include "Math/Rotator.h"
#include "Math/Utils.h"
#include "Object/Object.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

DEFINE_CLASS(AAnimTestPawn, APawn)  
REGISTER_FACTORY(AAnimTestPawn)

namespace
{
    const FName NAME_LightAttack1("LightAttack1");
    const FName NAME_LightAttack3("LightAttack3");
    const FName NAME_HeavyAttack("HeavyAttack");
    const FName NAME_WalkToIdle("WalkToIdle");
    const FName NAME_RunToIdle("RunToIdle");    
    const FName NAME_Attack1ToIdle("Attack1ToIdle");
    const FName NAME_Attack2ToIdle("Attack2ToIdle");
    const FName NAME_Attack3ToIdle("Attack3ToIdle");
    const FName NAME_LightAttack1Start("GwenLightAttack1Start");
    const FName NAME_LightAttack3Start("GwenLightAttack3Start");
    const FName NAME_HeavyAttackStart("GwenHeavyAttackStart");

    bool IsActionActive(const FInputActionState* Action)
    {
        return Action &&
            (Action->TriggerEvent == EInputTriggerEvent::Started ||
             Action->TriggerEvent == EInputTriggerEvent::Triggered);
    }

    bool IsActionStarted(const FInputActionState* Action)
    {
        return Action && Action->TriggerEvent == EInputTriggerEvent::Started;
    }

    float AxisLength(const FVector2& Axis)
    {
        return std::sqrt(Axis.X * Axis.X + Axis.Y * Axis.Y);
    }
}

AAnimTestPawn::~AAnimTestPawn()
{
    if (SkeletalMeshComp)
    {
        if (UAnimInstance* AnimInstance = SkeletalMeshComp->GetAnimInstance())
        {
            AnimInstance->SetStateMachineAsset(nullptr);
        }
    }

    if (LocomotionStateMachine)
    {
        UObjectManager::Get().DestroyObject(LocomotionStateMachine);
        LocomotionStateMachine = nullptr;
    }
}

void AAnimTestPawn::InitDefaultComponents()
{
    SceneRoot = AddComponent<USceneComponent>();
    SetRootComponent(SceneRoot);

    SkeletalMeshComp = AddComponent<USkeletalMeshComponent>();
    SkeletalMeshComp->AttachToComponent(SceneRoot);
    SkeletalMeshComp->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
    SkeletalMeshComp->SetRelativeScale(FVector(0.01f, 0.01f, 0.01f));

    SpringArmComp = AddComponent<USpringArmComponent>();
    SpringArmComp->AttachToComponent(SceneRoot);
    ApplyCameraSettings();

    CameraComp = AddComponent<UCameraComponent>();
    CameraComp->AttachToComponent(SpringArmComp);

    AddTag("AnimTestPawn");
    LoadConfiguredSkeletalMesh();
}

void AAnimTestPawn::BeginPlay()
{
    APawn::BeginPlay();

    LoadConfiguredSkeletalMesh();
    if (bAutoConfigureAnimation)
    {
        ConfigureLocomotionStateMachine();
    }

	if (GEngine && !BGMPath.empty())
	{
        GEngine->GetAudioSystem().PlayBGM(BGMPath);
	}

    IntroSoundTimer = std::max(0.0f, IntroSoundDelay);
    bIntroSoundPlayed = false;
}

void AAnimTestPawn::Tick(float DeltaTime)
{
    APawn::Tick(DeltaTime);
    if (!bIntroSoundPlayed)
    {
        IntroSoundTimer -= std::max(0.0f, DeltaTime);
        if (IntroSoundTimer <= 0.0f)
        {
            if (GEngine && !IntroSoundPath.empty())
            {
                GEngine->GetAudioSystem().PlaySFX(IntroSoundPath, IntroSoundVolumeScale);
            }
            bIntroSoundPlayed = true;
        }
    }

    UpdateLocomotion(DeltaTime);
}

void AAnimTestPawn::Serialize(FArchive& Ar)
{
    APawn::Serialize(Ar);

    Ar << "SkeletalMeshPath" << SkeletalMeshPath;
    Ar << "IdleAnimationPath" << IdleAnimationPath;
    Ar << "IntoRunAnimationPath" << IntoRunAnimationPath;
    Ar << "RunAnimationPath" << RunAnimationPath;
    Ar << "HomeguardAnimationPath" << HomeguardAnimationPath;
    Ar << "WalkToIdleAnimationPath" << WalkToIdleAnimationPath;
    Ar << "RunToIdleAnimationPath" << RunToIdleAnimationPath;
    Ar << "LightAttackAnimationPath" << LightAttackAnimationPath;
    Ar << "HeavyAttackAnimationPath" << HeavyAttackAnimationPath;
    Ar << "LightAttack3AnimationPath" << LightAttack3AnimationPath;
    Ar << "Attack1ToIdleAnimationPath" << Attack1ToIdleAnimationPath;
    Ar << "Attack2ToIdleAnimationPath" << Attack2ToIdleAnimationPath;
    Ar << "Attack3ToIdleAnimationPath" << Attack3ToIdleAnimationPath;
    Ar << "LightAttackSoundPath1" << LightAttackSoundPath1;
    Ar << "LightAttackSoundPath2" << LightAttackSoundPath2;
    Ar << "LightAttackSoundPath3" << LightAttackSoundPath3;
    Ar << "HeavyAttackSoundPath" << HeavyAttackSoundPath;
    Ar << "VoiceSoundPath1" << VoiceSoundPath1;
    Ar << "VoiceSoundPath2" << VoiceSoundPath1;
    Ar << "VoiceSoundPath3" << VoiceSoundPath1;
    Ar << "VoiceSoundPath4" << VoiceSoundPath1;
    Ar << "BGMPath" << BGMPath;
    Ar << "IntroSoundPath" << IntroSoundPath;
    Ar << "MoveSpeed" << MoveSpeed;
    Ar << "SprintSpeedMultiplier" << SprintSpeedMultiplier;
    Ar << "LookSensitivityDegrees" << LookSensitivityDegrees;
    Ar << "CameraInitialYawDegrees" << CameraInitialYawDegrees;
    Ar << "CameraInitialPitchDegrees" << CameraInitialPitchDegrees;
    Ar << "CameraPivotHeight" << CameraPivotHeight;
    Ar << "CameraArmLength" << CameraArmLength;
    Ar << "CameraSocketOffset" << CameraSocketOffset;
    Ar << "MeshTurnSpeedDegreesPerSecond" << MeshTurnSpeedDegreesPerSecond;
    Ar << "LocomotionBlendTime" << LocomotionBlendTime;
    Ar << "IntoRunDuration" << IntoRunDuration;
    Ar << "WalkToIdleDuration" << WalkToIdleDuration;
    Ar << "RunToIdleDuration" << RunToIdleDuration;
    Ar << "LightAttackDuration" << LightAttackDuration;
    Ar << "HeavyAttackDuration" << HeavyAttackDuration;
    Ar << "LightAttack3Duration" << LightAttack3Duration;
    Ar << "Attack1ToIdleDuration" << Attack1ToIdleDuration;
    Ar << "Attack2ToIdleDuration" << Attack2ToIdleDuration;
    Ar << "Attack3ToIdleDuration" << Attack3ToIdleDuration;
    Ar << "IntroSoundDelay" << IntroSoundDelay;
    Ar << "IntroSoundVolumeScale" << IntroSoundVolumeScale;
    Ar << "MoveStartSpeedThreshold" << MoveStartSpeedThreshold;
    Ar << "RotateToMovement" << bRotateToMovement;
    Ar << "AutoConfigureAnimation" << bAutoConfigureAnimation;

    if (Ar.IsLoading())
    {
        MoveSpeed = std::max(0.0f, MoveSpeed);
        SprintSpeedMultiplier = std::max(1.0f, SprintSpeedMultiplier);
        LookSensitivityDegrees = std::max(0.0f, LookSensitivityDegrees);
        CameraInitialPitchDegrees = MathUtil::Clamp(CameraInitialPitchDegrees, -89.0f, 89.0f);
        CameraPivotHeight = std::max(0.0f, CameraPivotHeight);
        CameraArmLength = std::max(0.0f, CameraArmLength);
        MeshTurnSpeedDegreesPerSecond = std::max(0.0f, MeshTurnSpeedDegreesPerSecond);
        LocomotionBlendTime = std::max(0.0f, LocomotionBlendTime);
        IntoRunDuration = std::max(0.0f, IntoRunDuration);
        WalkToIdleDuration = std::max(0.0f, WalkToIdleDuration);
        RunToIdleDuration = std::max(0.0f, RunToIdleDuration);
        LightAttackDuration = std::max(0.0f, LightAttackDuration);
        HeavyAttackDuration = std::max(0.0f, HeavyAttackDuration);
        LightAttack3Duration = std::max(0.0f, LightAttack3Duration);
        Attack1ToIdleDuration = std::max(0.0f, Attack1ToIdleDuration);
        Attack2ToIdleDuration = std::max(0.0f, Attack2ToIdleDuration);
        Attack3ToIdleDuration = std::max(0.0f, Attack3ToIdleDuration);
        IntroSoundDelay = std::max(0.0f, IntroSoundDelay);
        IntroSoundVolumeScale = std::max(0.0f, IntroSoundVolumeScale);
        MoveStartSpeedThreshold = std::max(0.0f, MoveStartSpeedThreshold);

        LoadConfiguredSkeletalMesh();
        ApplyCameraSettings();
        if (bAutoConfigureAnimation)
        {
            ConfigureLocomotionStateMachine();
        }
    }
}

void AAnimTestPawn::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    APawn::GetEditableProperties(OutProps);
    OutProps.push_back({ "Skeletal Mesh", EPropertyType::String, &SkeletalMeshPath });
    OutProps.push_back({ "Idle Animation", EPropertyType::String, &IdleAnimationPath });
    OutProps.push_back({ "Into Run Animation", EPropertyType::String, &IntoRunAnimationPath });
    OutProps.push_back({ "Run Animation", EPropertyType::String, &RunAnimationPath });
    OutProps.push_back({ "Homeguard Animation", EPropertyType::String, &HomeguardAnimationPath });
    OutProps.push_back({ "Walk To Idle Animation", EPropertyType::String, &WalkToIdleAnimationPath });
    OutProps.push_back({ "Run To Idle Animation", EPropertyType::String, &RunToIdleAnimationPath });
    OutProps.push_back({ "Light Attack 1 Animation", EPropertyType::String, &LightAttackAnimationPath });
    OutProps.push_back({ "Light Attack 3 Animation", EPropertyType::String, &LightAttack3AnimationPath });
    OutProps.push_back({ "Heavy Attack Animation", EPropertyType::String, &HeavyAttackAnimationPath });
    OutProps.push_back({ "Attack 1 To Idle Animation", EPropertyType::String, &Attack1ToIdleAnimationPath });
    OutProps.push_back({ "Attack 2 To Idle Animation", EPropertyType::String, &Attack2ToIdleAnimationPath });
    OutProps.push_back({ "Attack 3 To Idle Animation", EPropertyType::String, &Attack3ToIdleAnimationPath });
    OutProps.push_back({ "Light Attack Sound 1", EPropertyType::String, &LightAttackSoundPath1 });
    OutProps.push_back({ "Light Attack Sound 2", EPropertyType::String, &LightAttackSoundPath2 });
    OutProps.push_back({ "Light Attack Sound 3", EPropertyType::String, &LightAttackSoundPath3 });
    OutProps.push_back({ "Heavy Attack Sound", EPropertyType::String, &HeavyAttackSoundPath });
    OutProps.push_back({ "Voice Sound 1", EPropertyType::String, &VoiceSoundPath1 });
    OutProps.push_back({ "Voice Sound 2", EPropertyType::String, &VoiceSoundPath2 });
    OutProps.push_back({ "Voice Sound 3", EPropertyType::String, &VoiceSoundPath3 });
    OutProps.push_back({ "Voice Sound 4", EPropertyType::String, &VoiceSoundPath4 });
    OutProps.push_back({ "BGM Path", EPropertyType::String, &BGMPath });
    OutProps.push_back({ "Intro Sound Path", EPropertyType::String, &IntroSoundPath });
    OutProps.push_back({ "Intro Sound Delay", EPropertyType::Float, &IntroSoundDelay, 0.0f, 30.0f, 0.1f });
    OutProps.push_back({ "Intro Sound Volume", EPropertyType::Float, &IntroSoundVolumeScale, 0.0f, 5.0f, 0.1f });
    OutProps.push_back({ "Move Speed", EPropertyType::Float, &MoveSpeed, 0.0f, 100.0f, 0.1f });
    OutProps.push_back({ "Sprint Speed Multiplier", EPropertyType::Float, &SprintSpeedMultiplier, 1.0f, 10.0f, 0.05f });
    OutProps.push_back({ "Look Sensitivity", EPropertyType::Float, &LookSensitivityDegrees, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Camera Initial Yaw", EPropertyType::Float, &CameraInitialYawDegrees, -360.0f, 360.0f, 1.0f });
    OutProps.push_back({ "Camera Initial Pitch", EPropertyType::Float, &CameraInitialPitchDegrees, -89.0f, 89.0f, 1.0f });
    OutProps.push_back({ "Camera Pivot Height", EPropertyType::Float, &CameraPivotHeight, 0.0f, 10.0f, 0.05f });
    OutProps.push_back({ "Camera Arm Length", EPropertyType::Float, &CameraArmLength, 0.0f, 20.0f, 0.1f });
    OutProps.push_back({ "Camera Socket Offset", EPropertyType::Vec3, &CameraSocketOffset, 0.0f, 0.0f, 0.05f });
    OutProps.push_back({ "Mesh Turn Speed", EPropertyType::Float, &MeshTurnSpeedDegreesPerSecond, 0.0f, 1440.0f, 10.0f });
    OutProps.push_back({ "Locomotion Blend Time", EPropertyType::Float, &LocomotionBlendTime, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Into Run Duration", EPropertyType::Float, &IntoRunDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Walk To Idle Duration", EPropertyType::Float, &WalkToIdleDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Run To Idle Duration", EPropertyType::Float, &RunToIdleDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Light Attack 1 Duration", EPropertyType::Float, &LightAttackDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Light Attack 3 Duration", EPropertyType::Float, &LightAttack3Duration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Heavy Attack Duration", EPropertyType::Float, &HeavyAttackDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Attack 1 To Idle Duration", EPropertyType::Float, &Attack1ToIdleDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Attack 2 To Idle Duration", EPropertyType::Float, &Attack2ToIdleDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Attack 3 To Idle Duration", EPropertyType::Float, &Attack3ToIdleDuration, 0.0f, 5.0f, 0.01f });
    OutProps.push_back({ "Move Start Speed Threshold", EPropertyType::Float, &MoveStartSpeedThreshold, 0.0f, 100.0f, 0.01f });
    OutProps.push_back({ "Rotate Mesh To Movement", EPropertyType::Bool, &bRotateToMovement });
    OutProps.push_back({ "Auto Configure Animation", EPropertyType::Bool, &bAutoConfigureAnimation });
}

void AAnimTestPawn::PostEditProperty(const char* PropertyName)
{
    APawn::PostEditProperty(PropertyName);
    if (!PropertyName)
    {
        return;
    }

    if (std::strcmp(PropertyName, "Skeletal Mesh") == 0)
    {
        LoadConfiguredSkeletalMesh();
    }

    if (std::strcmp(PropertyName, "Camera Initial Yaw") == 0 ||
        std::strcmp(PropertyName, "Camera Initial Pitch") == 0 ||
        std::strcmp(PropertyName, "Camera Pivot Height") == 0 ||
        std::strcmp(PropertyName, "Camera Arm Length") == 0 ||
        std::strcmp(PropertyName, "Camera Socket Offset") == 0)
    {
        CameraInitialPitchDegrees = MathUtil::Clamp(CameraInitialPitchDegrees, -89.0f, 89.0f);
        CameraPivotHeight = std::max(0.0f, CameraPivotHeight);
        CameraArmLength = std::max(0.0f, CameraArmLength);
        ApplyCameraSettings();
    }

    if (std::strcmp(PropertyName, "Mesh Turn Speed") == 0)
    {
        MeshTurnSpeedDegreesPerSecond = std::max(0.0f, MeshTurnSpeedDegreesPerSecond);
    }

    if (std::strcmp(PropertyName, "Intro Sound Delay") == 0)
    {
        IntroSoundDelay = std::max(0.0f, IntroSoundDelay);
    }

    if (std::strcmp(PropertyName, "Intro Sound Volume") == 0)
    {
        IntroSoundVolumeScale = std::max(0.0f, IntroSoundVolumeScale);
    }

    if (std::strcmp(PropertyName, "Idle Animation") == 0 ||
        std::strcmp(PropertyName, "Into Run Animation") == 0 ||
        std::strcmp(PropertyName, "Run Animation") == 0 ||
        std::strcmp(PropertyName, "Homeguard Animation") == 0 ||
        std::strcmp(PropertyName, "Walk To Idle Animation") == 0 ||
        std::strcmp(PropertyName, "Run To Idle Animation") == 0 ||
        std::strcmp(PropertyName, "Light Attack Animation") == 0 ||
        std::strcmp(PropertyName, "Light Attack 1 Animation") == 0 ||
        std::strcmp(PropertyName, "Light Attack 3 Animation") == 0 ||
        std::strcmp(PropertyName, "Heavy Attack Animation") == 0 ||
        std::strcmp(PropertyName, "Attack 1 To Idle Animation") == 0 ||
        std::strcmp(PropertyName, "Attack 2 To Idle Animation") == 0 ||
        std::strcmp(PropertyName, "Attack 3 To Idle Animation") == 0 ||
        std::strcmp(PropertyName, "Locomotion Blend Time") == 0 ||
        std::strcmp(PropertyName, "Into Run Duration") == 0 ||
        std::strcmp(PropertyName, "Walk To Idle Duration") == 0 ||
        std::strcmp(PropertyName, "Run To Idle Duration") == 0 ||
        std::strcmp(PropertyName, "Light Attack Duration") == 0 ||
        std::strcmp(PropertyName, "Light Attack 1 Duration") == 0 ||
        std::strcmp(PropertyName, "Light Attack 3 Duration") == 0 ||
        std::strcmp(PropertyName, "Heavy Attack Duration") == 0 ||
        std::strcmp(PropertyName, "Attack To Idle Duration") == 0 ||
        std::strcmp(PropertyName, "Attack 1 To Idle Duration") == 0 ||
        std::strcmp(PropertyName, "Attack 2 To Idle Duration") == 0 ||
        std::strcmp(PropertyName, "Attack 3 To Idle Duration") == 0 ||
        std::strcmp(PropertyName, "Move Start Speed Threshold") == 0 ||
        std::strcmp(PropertyName, "Auto Configure Animation") == 0)
    {
        if (bAutoConfigureAnimation)
        {
            ConfigureLocomotionStateMachine();
        }
    }
}

void AAnimTestPawn::LoadConfiguredSkeletalMesh()
{
    if (!SkeletalMeshComp || SkeletalMeshPath.empty())
    {
        return;
    }

    SkeletalMeshComp->SetSkeletalMesh(FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshPath));
}

void AAnimTestPawn::ApplyCameraSettings()
{
    if (!SpringArmComp)
    {
        return;
    }

    SpringArmComp->SetRelativeLocation(FVector(0.0f, 0.0f, CameraPivotHeight));
    SpringArmComp->SetRelativeRotation(FVector(0.0f, 0.0f, 0.0f));
    SpringArmComp->AddYawInput(CameraInitialYawDegrees);
    SpringArmComp->AddPitchInput(CameraInitialPitchDegrees);
    SpringArmComp->SetTargetArmLength(CameraArmLength);
    SpringArmComp->SetSocketOffset(CameraSocketOffset);
}

void AAnimTestPawn::ConfigureLocomotionStateMachine()
{
    if (!SkeletalMeshComp ||
        IdleAnimationPath.empty() ||
        IntoRunAnimationPath.empty() ||
        RunAnimationPath.empty() ||
        HomeguardAnimationPath.empty() ||
        WalkToIdleAnimationPath.empty() ||
        RunToIdleAnimationPath.empty() ||
        LightAttackAnimationPath.empty() ||
        LightAttack3AnimationPath.empty() ||
        HeavyAttackAnimationPath.empty() ||
        Attack1ToIdleAnimationPath.empty() ||
        Attack2ToIdleAnimationPath.empty() ||
        Attack3ToIdleAnimationPath.empty())
    {
        return;
    }

    UAnimInstance* AnimInstance = SkeletalMeshComp->GetOrCreateDefaultAnimInstance();
    if (!AnimInstance)
    {
        return;
    }

    AnimInstance->RegisterAnimationPath(FName("Idle"), IdleAnimationPath);
    AnimInstance->RegisterAnimationPath(FName("IntoRun"), IntoRunAnimationPath);
    AnimInstance->RegisterAnimationPath(FName("Run"), RunAnimationPath);
    AnimInstance->RegisterAnimationPath(FName("Homeguard"), HomeguardAnimationPath);
    AnimInstance->RegisterAnimationPath(NAME_WalkToIdle, WalkToIdleAnimationPath);
    AnimInstance->RegisterAnimationPath(NAME_RunToIdle, RunToIdleAnimationPath);
    AnimInstance->RegisterAnimationPath(NAME_LightAttack1, LightAttackAnimationPath);
    AnimInstance->RegisterAnimationPath(NAME_LightAttack3, LightAttack3AnimationPath);
    AnimInstance->RegisterAnimationPath(NAME_HeavyAttack, HeavyAttackAnimationPath);
    AnimInstance->RegisterAnimationPath(NAME_Attack1ToIdle, Attack1ToIdleAnimationPath);
    AnimInstance->RegisterAnimationPath(NAME_Attack2ToIdle, Attack2ToIdleAnimationPath);
    AnimInstance->RegisterAnimationPath(NAME_Attack3ToIdle, Attack3ToIdleAnimationPath);
    AnimInstance->SetAnimFloatParameter(FName("Speed"), 0.0f);
    AnimInstance->SetAnimFloatParameter(FName("GroundSpeed"), 0.0f);
    AnimInstance->SetAnimBoolParameter(FName("bMoving"), false);
    AnimInstance->SetAnimBoolParameter(FName("bSprinting"), false);

    if (LocomotionStateMachine)
    {
        UObjectManager::Get().DestroyObject(LocomotionStateMachine);
        LocomotionStateMachine = nullptr;
    }

    LocomotionStateMachine = UObjectManager::Get().CreateObject<UAnimStateMachineAsset>();
    if (!LocomotionStateMachine)
    {
        return;
    }

    LocomotionStateMachine->SetEntryState(FName("Idle"));
    LocomotionStateMachine->AddState(FName("Idle"), FName("Idle"), true, IdleAnimationPath);
    LocomotionStateMachine->AddState(FName("IntoRun"), FName("IntoRun"), false, IntoRunAnimationPath);
    LocomotionStateMachine->AddState(FName("Run"), FName("Run"), true, RunAnimationPath);
    LocomotionStateMachine->AddState(FName("Homeguard"), FName("Homeguard"), true, HomeguardAnimationPath);
    LocomotionStateMachine->AddState(NAME_WalkToIdle, NAME_WalkToIdle, false, WalkToIdleAnimationPath);
    LocomotionStateMachine->AddState(NAME_RunToIdle, NAME_RunToIdle, false, RunToIdleAnimationPath);
    LocomotionStateMachine->AddState(NAME_LightAttack1, NAME_LightAttack1, false, LightAttackAnimationPath);
    LocomotionStateMachine->AddState(NAME_LightAttack3, NAME_LightAttack3, false, LightAttack3AnimationPath);
    LocomotionStateMachine->AddState(NAME_HeavyAttack, NAME_HeavyAttack, false, HeavyAttackAnimationPath);
    LocomotionStateMachine->AddState(NAME_Attack1ToIdle, NAME_Attack1ToIdle, false, Attack1ToIdleAnimationPath);
    LocomotionStateMachine->AddState(NAME_Attack2ToIdle, NAME_Attack2ToIdle, false, Attack2ToIdleAnimationPath);
    LocomotionStateMachine->AddState(NAME_Attack3ToIdle, NAME_Attack3ToIdle, false, Attack3ToIdleAnimationPath);

    LocomotionStateMachine->AddBoolTransition(
        FName("Idle"),
        FName("Homeguard"),
        FName("bSprinting"),
        EAnimCompareOp::IsTrue,
        true,
        LocomotionBlendTime,
        20,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        FName("Idle"),
        FName("IntoRun"),
        FName("bMoving"),
        EAnimCompareOp::IsTrue,
        true,
        LocomotionBlendTime,
        10,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        FName("IntoRun"),
        FName("Homeguard"),
        FName("bSprinting"),
        EAnimCompareOp::IsTrue,
        true,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        FName("IntoRun"),
        NAME_WalkToIdle,
        FName("bMoving"),
        EAnimCompareOp::IsFalse,
        false,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddFloatTransition(
        FName("IntoRun"),
        FName("Run"),
        FName("StateElapsedTime"),
        EAnimCompareOp::GreaterEqual,
        IntoRunDuration,
        LocomotionBlendTime,
        10,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        FName("Run"),
        FName("Homeguard"),
        FName("bSprinting"),
        EAnimCompareOp::IsTrue,
        true,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        FName("Run"),
        NAME_WalkToIdle,
        FName("bMoving"),
        EAnimCompareOp::IsFalse,
        false,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        FName("Homeguard"),
        NAME_RunToIdle,
        FName("bMoving"),
        EAnimCompareOp::IsFalse,
        false,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        FName("Homeguard"),
        FName("Run"),
        FName("bSprinting"),
        EAnimCompareOp::IsFalse,
        false,
        LocomotionBlendTime,
        90,
        EAnimBlendEaseOption::EaseInOut);

    LocomotionStateMachine->AddBoolTransition(
        NAME_WalkToIdle,
        FName("Homeguard"),
        FName("bSprinting"),
        EAnimCompareOp::IsTrue,
        true,
        LocomotionBlendTime,
        120,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        NAME_WalkToIdle,
        FName("Run"),
        FName("bMoving"),
        EAnimCompareOp::IsTrue,
        true,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddFloatTransition(
        NAME_WalkToIdle,
        FName("Idle"),
        FName("StateElapsedTime"),
        EAnimCompareOp::GreaterEqual,
        WalkToIdleDuration,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);

    LocomotionStateMachine->AddBoolTransition(
        NAME_RunToIdle,
        FName("Homeguard"),
        FName("bSprinting"),
        EAnimCompareOp::IsTrue,
        true,
        LocomotionBlendTime,
        120,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddBoolTransition(
        NAME_RunToIdle,
        FName("Run"),
        FName("bMoving"),
        EAnimCompareOp::IsTrue,
        true,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);
    LocomotionStateMachine->AddFloatTransition(
        NAME_RunToIdle,
        FName("Idle"),
        FName("StateElapsedTime"),
        EAnimCompareOp::GreaterEqual,
        RunToIdleDuration,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);

    const FName LocomotionStates[] = {
        FName("Idle"),
        FName("IntoRun"),
        FName("Run"),
        FName("Homeguard"),
        NAME_WalkToIdle,
        NAME_RunToIdle,
    };

    for (const FName& StateName : LocomotionStates)
    {
        LocomotionStateMachine->AddTriggerTransition(
            StateName,
            NAME_HeavyAttack,
            NAME_HeavyAttack,
            LocomotionBlendTime,
            200,
            EAnimBlendEaseOption::EaseInOut);
        LocomotionStateMachine->AddTriggerTransition(
            StateName,
            NAME_LightAttack1,
            NAME_LightAttack1,
            LocomotionBlendTime,
            190,
            EAnimBlendEaseOption::EaseInOut);
        LocomotionStateMachine->AddTriggerTransition(
            StateName,
            NAME_LightAttack3,
            NAME_LightAttack3,
            LocomotionBlendTime,
            190,
            EAnimBlendEaseOption::EaseInOut);
    }

    const FName AttackRecoveryStates[] = {
        NAME_Attack1ToIdle,
        NAME_Attack2ToIdle,
        NAME_Attack3ToIdle,
    };

    for (const FName& StateName : AttackRecoveryStates)
    {
        LocomotionStateMachine->AddTriggerTransition(
            StateName,
            NAME_HeavyAttack,
            NAME_HeavyAttack,
            LocomotionBlendTime,
            200,
            EAnimBlendEaseOption::EaseInOut);
        LocomotionStateMachine->AddTriggerTransition(
            StateName,
            NAME_LightAttack1,
            NAME_LightAttack1,
            LocomotionBlendTime,
            190,
            EAnimBlendEaseOption::EaseInOut);
        LocomotionStateMachine->AddTriggerTransition(
            StateName,
            NAME_LightAttack3,
            NAME_LightAttack3,
            LocomotionBlendTime,
            190,
            EAnimBlendEaseOption::EaseInOut);
    }

    FAnimTransitionCondition AttackTimeCondition;
    AttackTimeCondition.ParameterName = FName("StateElapsedTime");
    AttackTimeCondition.CompareOp = EAnimCompareOp::GreaterEqual;

    FAnimTransitionCondition RecoveryMovingCondition;
    RecoveryMovingCondition.ParameterName = FName("bMoving");
    RecoveryMovingCondition.CompareOp = EAnimCompareOp::IsTrue;
    RecoveryMovingCondition.CompareValue.BoolValue = true;

    FAnimTransitionCondition RecoverySprintingCondition;
    RecoverySprintingCondition.ParameterName = FName("bSprinting");
    RecoverySprintingCondition.CompareOp = EAnimCompareOp::IsTrue;
    RecoverySprintingCondition.CompareValue.BoolValue = true;

    TArray<FAnimTransitionCondition> Attack1ToIdleStartConditions;
    AttackTimeCondition.CompareValue.FloatValue = LightAttackDuration;
    Attack1ToIdleStartConditions.push_back(AttackTimeCondition);

    TArray<FAnimTransitionCondition> Attack1DirectToHomeguardConditions = Attack1ToIdleStartConditions;
    Attack1DirectToHomeguardConditions.push_back(RecoverySprintingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_LightAttack1,
        FName("Homeguard"),
        Attack1DirectToHomeguardConditions,
        LocomotionBlendTime,
        120,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack1DirectToRunConditions = Attack1ToIdleStartConditions;
    Attack1DirectToRunConditions.push_back(RecoveryMovingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_LightAttack1,
        FName("Run"),
        Attack1DirectToRunConditions,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);

    LocomotionStateMachine->AddTransition(
        NAME_LightAttack1,
        NAME_Attack1ToIdle,
        Attack1ToIdleStartConditions,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack2ToIdleStartConditions;
    AttackTimeCondition.CompareValue.FloatValue = HeavyAttackDuration;
    Attack2ToIdleStartConditions.push_back(AttackTimeCondition);

    TArray<FAnimTransitionCondition> Attack2DirectToHomeguardConditions = Attack2ToIdleStartConditions;
    Attack2DirectToHomeguardConditions.push_back(RecoverySprintingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_HeavyAttack,
        FName("Homeguard"),
        Attack2DirectToHomeguardConditions,
        LocomotionBlendTime,
        120,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack2DirectToRunConditions = Attack2ToIdleStartConditions;
    Attack2DirectToRunConditions.push_back(RecoveryMovingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_HeavyAttack,
        FName("Run"),
        Attack2DirectToRunConditions,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);

    LocomotionStateMachine->AddTransition(
        NAME_HeavyAttack,
        NAME_Attack2ToIdle,
        Attack2ToIdleStartConditions,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack3ToIdleStartConditions;
    AttackTimeCondition.CompareValue.FloatValue = LightAttack3Duration;
    Attack3ToIdleStartConditions.push_back(AttackTimeCondition);

    TArray<FAnimTransitionCondition> Attack3DirectToHomeguardConditions = Attack3ToIdleStartConditions;
    Attack3DirectToHomeguardConditions.push_back(RecoverySprintingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_LightAttack3,
        FName("Homeguard"),
        Attack3DirectToHomeguardConditions,
        LocomotionBlendTime,
        120,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack3DirectToRunConditions = Attack3ToIdleStartConditions;
    Attack3DirectToRunConditions.push_back(RecoveryMovingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_LightAttack3,
        FName("Run"),
        Attack3DirectToRunConditions,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);

    LocomotionStateMachine->AddTransition(
        NAME_LightAttack3,
        NAME_Attack3ToIdle,
        Attack3ToIdleStartConditions,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack1ToIdleFinishConditions;
    AttackTimeCondition.CompareValue.FloatValue = Attack1ToIdleDuration;
    Attack1ToIdleFinishConditions.push_back(AttackTimeCondition);

    TArray<FAnimTransitionCondition> Attack1ToHomeguardConditions;
    Attack1ToHomeguardConditions.push_back(RecoverySprintingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_Attack1ToIdle,
        FName("Homeguard"),
        Attack1ToHomeguardConditions,
        LocomotionBlendTime,
        120,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack1ToRunConditions;
    Attack1ToRunConditions.push_back(RecoveryMovingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_Attack1ToIdle,
        FName("Run"),
        Attack1ToRunConditions,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);

    LocomotionStateMachine->AddTransition(
        NAME_Attack1ToIdle,
        FName("Idle"),
        Attack1ToIdleFinishConditions,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack2ToIdleFinishConditions;
    AttackTimeCondition.CompareValue.FloatValue = Attack2ToIdleDuration;
    Attack2ToIdleFinishConditions.push_back(AttackTimeCondition);

    TArray<FAnimTransitionCondition> Attack2ToHomeguardConditions;
    Attack2ToHomeguardConditions.push_back(RecoverySprintingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_Attack2ToIdle,
        FName("Homeguard"),
        Attack2ToHomeguardConditions,
        LocomotionBlendTime,
        120,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack2ToRunConditions;
    Attack2ToRunConditions.push_back(RecoveryMovingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_Attack2ToIdle,
        FName("Run"),
        Attack2ToRunConditions,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);

    LocomotionStateMachine->AddTransition(
        NAME_Attack2ToIdle,
        FName("Idle"),
        Attack2ToIdleFinishConditions,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack3ToIdleFinishConditions;
    AttackTimeCondition.CompareValue.FloatValue = Attack3ToIdleDuration;
    Attack3ToIdleFinishConditions.push_back(AttackTimeCondition);

    TArray<FAnimTransitionCondition> Attack3ToHomeguardConditions;
    Attack3ToHomeguardConditions.push_back(RecoverySprintingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_Attack3ToIdle,
        FName("Homeguard"),
        Attack3ToHomeguardConditions,
        LocomotionBlendTime,
        120,
        EAnimBlendEaseOption::EaseInOut);

    TArray<FAnimTransitionCondition> Attack3ToRunConditions;
    Attack3ToRunConditions.push_back(RecoveryMovingCondition);
    LocomotionStateMachine->AddTransition(
        NAME_Attack3ToIdle,
        FName("Run"),
        Attack3ToRunConditions,
        LocomotionBlendTime,
        110,
        EAnimBlendEaseOption::EaseInOut);

    LocomotionStateMachine->AddTransition(
        NAME_Attack3ToIdle,
        FName("Idle"),
        Attack3ToIdleFinishConditions,
        LocomotionBlendTime,
        100,
        EAnimBlendEaseOption::EaseInOut);

    if (!SkeletalMeshComp->UseStateMachine(LocomotionStateMachine))
    {
        UE_LOG_WARNING("[AnimTestPawn] Failed to activate locomotion state machine");
    }
}

void AAnimTestPawn::UpdateLocomotion(float DeltaTime)
{
    if (DeltaTime <= 0.0f)
    {
        UpdateAnimationParameters(0.0f, false, false);
        return;
    }

    APlayerController* PlayerController = GetController();
    const FGameplayInputSnapshot* Snapshot = PlayerController ? &PlayerController->GetInputSnapshot() : nullptr;
    const FInputActionState* MoveAction = Snapshot ? Snapshot->FindAction("Move") : nullptr;
    const FInputActionState* LookAction = Snapshot ? Snapshot->FindAction("Look") : nullptr;
    const FInputActionState* SprintAction = Snapshot ? Snapshot->FindAction("Sprint") : nullptr;
    const FInputActionState* LightAttackAction = Snapshot ? Snapshot->FindAction("Attack") : nullptr;
    const FInputActionState* HeavyAttackAction = Snapshot ? Snapshot->FindAction("HeavyAttack") : nullptr;
    const bool bLightAttackStarted = IsActionStarted(LightAttackAction);
    const bool bHeavyAttackStarted = IsActionStarted(HeavyAttackAction);
    const bool bInMainAttack = IsInMainAttackState();
    const bool bLockMovement = IsInAttackState() || bLightAttackStarted || bHeavyAttackStarted;

    FVector2 MoveAxis = FVector2::ZeroVector;
    if (MoveAction)
    {
        MoveAxis = MoveAction->Value.Axis2D;
    }

    const float MoveAxisLength = std::min(AxisLength(MoveAxis), 1.0f);
    const bool bSprinting = MoveAxisLength > 0.0f && IsActionActive(SprintAction);
    const float GroundSpeed = MoveAxisLength * MoveSpeed * (bSprinting ? SprintSpeedMultiplier : 1.0f);
    const bool bMoving = GroundSpeed > MoveStartSpeedThreshold;

    if (SpringArmComp && IsActionActive(LookAction))
    {
        const FVector2& LookAxis = LookAction->Value.Axis2D;
        SpringArmComp->AddYawInput(LookAxis.X * LookSensitivityDegrees);
        SpringArmComp->AddPitchInput(-LookAxis.Y * LookSensitivityDegrees);
    }

    if (bMoving && !bLockMovement)
    {
        const FVector MoveDirection = BuildCameraRelativeMoveDirection(MoveAxis);
        AddActorWorldOffset(MoveDirection * (GroundSpeed * DeltaTime));

        if (bRotateToMovement)
        {
            const float YawDegrees = MathUtil::RadiansToDegrees(std::atan2(MoveDirection.Y, MoveDirection.X)) - 90.0f;
            if (SkeletalMeshComp)
            {
                FVector MeshRotation = SkeletalMeshComp->GetRelativeRotation();
                const float TargetRelativeYaw = FRotator::NormalizeAxis(YawDegrees - GetActorRotation().Z);
                const float DeltaYaw = FRotator::NormalizeAxis(TargetRelativeYaw - MeshRotation.Z);
                const float MaxYawStep = MeshTurnSpeedDegreesPerSecond * DeltaTime;
                const float YawStep = MathUtil::Clamp(DeltaYaw, -MaxYawStep, MaxYawStep);
                MeshRotation.Z = FRotator::NormalizeAxis(MeshRotation.Z + YawStep);
                SkeletalMeshComp->SetRelativeRotation(MeshRotation);
            }
        }

        VoiceTimer -= DeltaTime;
        if (VoiceTimer <= 0.0f)
        {
            TArray<FString> Voices;
            if (!VoiceSoundPath1.empty()) Voices.push_back(VoiceSoundPath1);
            if (!VoiceSoundPath2.empty()) Voices.push_back(VoiceSoundPath2);
            if (!VoiceSoundPath3.empty()) Voices.push_back(VoiceSoundPath3);
            if (!VoiceSoundPath4.empty()) Voices.push_back(VoiceSoundPath4);

            if (!Voices.empty())
            {
                if (CurrentVoiceIndex >= static_cast<int32>(Voices.size()))
                {
                    CurrentVoiceIndex = 0;
                }
                PlaySoundAtActor(Voices[CurrentVoiceIndex]);
                CurrentVoiceIndex++;
            }
            VoiceTimer = 3.0f + static_cast<float>(std::rand()) / (static_cast<float>(RAND_MAX / 5.0f));
        }
    }

    UpdateAnimationParameters(GroundSpeed, bMoving, bSprinting);

    if (SkeletalMeshComp)
    {
        if (UAnimInstance* AnimInstance = SkeletalMeshComp->GetAnimInstance())
        {
            if (bInMainAttack)
            {
                return;
            }

            if (bHeavyAttackStarted)
            {
                AnimInstance->SetAnimTriggerParameter(NAME_HeavyAttack);
            }
            else if (bLightAttackStarted)
            {
                if (bNextLightAttackUsesAttack1)
                {
                    AnimInstance->SetAnimTriggerParameter(NAME_LightAttack1);
                    bNextLightAttackUsesAttack1 = false;
                }
                else
                {
                    AnimInstance->SetAnimTriggerParameter(NAME_LightAttack3);
                    bNextLightAttackUsesAttack1 = true;
                }
            }
        }
    }
}

bool AAnimTestPawn::IsInAttackState() const
{
    if (!SkeletalMeshComp)
    {
        return false;
    }

    const UAnimInstance* AnimInstance = SkeletalMeshComp->GetAnimInstance();
    if (!AnimInstance)
    {
        return false;
    }

    const FAnimStateMachineNode* StateMachine = AnimInstance->GetStateMachine();
    if (!StateMachine)
    {
        return false;
    }

    const FName CurrentState = StateMachine->GetCurrentState();
    return CurrentState == NAME_LightAttack1 ||
        CurrentState == NAME_LightAttack3 ||
        CurrentState == NAME_HeavyAttack ||
        CurrentState == NAME_Attack1ToIdle ||
        CurrentState == NAME_Attack2ToIdle ||
        CurrentState == NAME_Attack3ToIdle;
}

bool AAnimTestPawn::IsInMainAttackState() const
{
    if (!SkeletalMeshComp)
    {
        return false;
    }

    const UAnimInstance* AnimInstance = SkeletalMeshComp->GetAnimInstance();
    if (!AnimInstance)
    {
        return false;
    }

    const FAnimStateMachineNode* StateMachine = AnimInstance->GetStateMachine();
    if (!StateMachine)
    {
        return false;
    }

    const FName CurrentState = StateMachine->GetCurrentState();
    return CurrentState == NAME_LightAttack1 ||
        CurrentState == NAME_LightAttack3 ||
        CurrentState == NAME_HeavyAttack;
}

void AAnimTestPawn::AddNotifyToAnimation(
    UAnimInstance* AnimInstance,
    const FName& AnimationName,
    const FName& NotifyName,
    float TriggerTime)
{
    if (!AnimInstance || !AnimationName.IsValid() || !NotifyName.IsValid())
    {
        return;
    }

    UAnimationSequenceBase* Sequence = AnimInstance->FindRegisteredAnimation(AnimationName);
    if (!Sequence)
    {
        return;
    }

    for (const FAnimNotifyEvent& ExistingNotify : Sequence->GetNotifies())
    {
        if (ExistingNotify.NotifyName == NotifyName)
        {
            return;
        }
    }

    FAnimNotifyEvent Notify;
    Notify.TriggerTime = std::max(0.0f, TriggerTime);
    Notify.NotifyName = NotifyName;
    Sequence->AddNotify(Notify);
}

void AAnimTestPawn::HandleAnimNotify(const FAnimNotifyEvent& Notify)
{
    APawn::HandleAnimNotify(Notify);
    PlayAttackNotifySound(Notify);
}

void AAnimTestPawn::PlayAttackNotifySound(const FAnimNotifyEvent& Notify)
{
    if (Notify.NotifyName == NAME_LightAttack1Start ||
        Notify.NotifyName == NAME_LightAttack3Start)
    {
        PlayLightAttackSound();
    }
    else if (Notify.NotifyName == NAME_HeavyAttackStart)
    {
        PlayHeavyAttackSound();
    }
}

void AAnimTestPawn::PlayLightAttackSound()
{
    TArray<FString> SoundPaths;
    if (!LightAttackSoundPath1.empty())
    {
        SoundPaths.push_back(LightAttackSoundPath1);
    }
    if (!LightAttackSoundPath2.empty())
    {
        SoundPaths.push_back(LightAttackSoundPath2);
    }
    if (!LightAttackSoundPath3.empty())
    {
        SoundPaths.push_back(LightAttackSoundPath3);
    }

    if (SoundPaths.empty())
    {
        return;
    }

    const int32 RandomIndex = static_cast<int32>(std::rand() % SoundPaths.size());
    PlaySoundAtActor(SoundPaths[RandomIndex]);
}

void AAnimTestPawn::PlayHeavyAttackSound()
{
    PlaySoundAtActor(HeavyAttackSoundPath);
}

void AAnimTestPawn::PlaySoundAtActor(const FString& SoundPath)
{
    if (GEngine && !SoundPath.empty())
    {
        GEngine->GetAudioSystem().PlaySFX(SoundPath);
    }
}

FVector AAnimTestPawn::BuildCameraRelativeMoveDirection(const FVector2& MoveAxis) const
{
    FVector Forward = SpringArmComp ? SpringArmComp->GetForwardVector() : GetActorForward();
    FVector Right = SpringArmComp ? SpringArmComp->GetRightVector() : GetActorRight();

    Forward.Z = 0.0f;
    Right.Z = 0.0f;
    Forward.Normalize();
    Right.Normalize();

    FVector Direction = Forward * MoveAxis.Y + Right * MoveAxis.X;
    if (!Direction.Normalize())
    {
        return GetActorForward();
    }
    return Direction;
}

void AAnimTestPawn::UpdateAnimationParameters(float GroundSpeed, bool bMoving, bool bSprinting)
{
    if (!SkeletalMeshComp)
    {
        return;
    }

    UAnimInstance* AnimInstance = SkeletalMeshComp->GetAnimInstance();
    if (!AnimInstance)
    {
        return;
    }

    AnimInstance->SetAnimFloatParameter(FName("Speed"), GroundSpeed);
    AnimInstance->SetAnimFloatParameter(FName("GroundSpeed"), GroundSpeed);
    AnimInstance->SetAnimBoolParameter(FName("bMoving"), bMoving);
    AnimInstance->SetAnimBoolParameter(FName("bSprinting"), bSprinting);
}
