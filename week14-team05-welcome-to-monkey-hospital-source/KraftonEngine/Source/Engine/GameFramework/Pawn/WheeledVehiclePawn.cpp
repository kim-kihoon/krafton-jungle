#include "GameFramework/Pawn/WheeledVehiclePawn.h"

#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Movement/WheeledVehicleMovementComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Vehicle/VehicleWheelPoseComponent.h"
#include "Mesh/MeshManager.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <cmath>

namespace
{
    float NormalizeVehicleCameraAngle(float Angle)
    {
        Angle = std::fmod(Angle, 360.0f);
        if (Angle > 180.0f)
        {
            Angle -= 360.0f;
        }
        else if (Angle <= -180.0f)
        {
            Angle += 360.0f;
        }
        return Angle;
    }

    float ClampVehicleCameraPitch(float Value, float MinPitch, float MaxPitch)
    {
        if (MinPitch > MaxPitch)
        {
            std::swap(MinPitch, MaxPitch);
        }
        return std::clamp(Value, MinPitch, MaxPitch);
    }

    float ClampVehicleCameraYawOffset(float Value, float MaxAbsOffset)
    {
        const float ClampedMax = std::clamp(MaxAbsOffset, 0.0f, 180.0f);
        return std::clamp(NormalizeVehicleCameraAngle(Value), -ClampedMax, ClampedMax);
    }

    float ExponentialInterpTo(float Current, float Target, float DeltaTime, float Speed)
    {
        if (DeltaTime <= 0.0f || Speed <= 0.0f)
        {
            return Current;
        }

        const float Alpha = 1.0f - std::exp(-Speed * DeltaTime);
        return Current + (Target - Current) * Alpha;
    }

    float ExponentialAngleInterpTo(float Current, float Target, float DeltaTime, float Speed)
    {
        if (DeltaTime <= 0.0f || Speed <= 0.0f)
        {
            return Current;
        }

        const float Delta = NormalizeVehicleCameraAngle(Target - Current);
        const float Alpha = 1.0f - std::exp(-Speed * DeltaTime);
        return NormalizeVehicleCameraAngle(Current + Delta * Alpha);
    }
}

void AWheeledVehiclePawn::InitDefaultComponents(const FString& SkeletalMeshFileName)
{
    Mesh = AddComponent<USkeletalMeshComponent>();
    SetRootComponent(Mesh);

    ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
    if (!SkeletalMeshFileName.empty())
    {
        USkeletalMesh* Asset = FMeshManager::LoadSkeletalMesh(SkeletalMeshFileName, Device);
        Mesh->SetSkeletalMesh(Asset);
    }

    VehicleMovement = AddComponent<UWheeledVehicleMovementComponent>();
    VehicleMovement->SetUpdatedComponent(Mesh);

    WheelPose = AddComponent<UVehicleWheelPoseComponent>();
    WheelPose->SetVehicleMovement(VehicleMovement);
    WheelPose->SetSkeletalMeshComponent(Mesh);

    SpringArm = AddComponent<USpringArmComponent>();
    SpringArm->AttachToComponent(Mesh);
    SpringArm->TargetArmLength = 7.0f;
    SpringArm->SocketOffset = FVector(0.0f, 0.0f, 2.5f);
    SpringArm->bEnableCameraLag = true;
    SpringArm->bEnableCameraRotationLag = true;
    SpringArm->bUsePawnControlRotation = true;
    SpringArm->bInheritPitch = true;
    SpringArm->bInheritYaw = true;
    SpringArm->bInheritRoll = false;

    VehicleCameraPitch = ClampVehicleCameraPitch(VehicleDefaultCameraPitch, VehicleMinCameraPitch, VehicleMaxCameraPitch);
    UpdateVehicleCameraControlRotation();

    Camera = AddComponent<UCameraComponent>();
    Camera->AttachToComponent(SpringArm);
}

void AWheeledVehiclePawn::RebindVehicleComponents()
{
    Mesh = GetComponentByClass<USkeletalMeshComponent>();
    VehicleMovement = GetComponentByClass<UWheeledVehicleMovementComponent>();
    WheelPose = GetComponentByClass<UVehicleWheelPoseComponent>();
    SpringArm = GetComponentByClass<USpringArmComponent>();
    Camera = GetComponentByClass<UCameraComponent>();

    if (VehicleMovement && Mesh)
    {
        VehicleMovement->SetUpdatedComponent(Mesh);
    }
    if (WheelPose)
    {
        WheelPose->SetVehicleMovement(VehicleMovement);
        WheelPose->SetSkeletalMeshComponent(Mesh);
    }
}

void AWheeledVehiclePawn::BeginPlay()
{
    RebindVehicleComponents();
    VehicleCameraPitch = ClampVehicleCameraPitch(VehicleDefaultCameraPitch, VehicleMinCameraPitch, VehicleMaxCameraPitch);
    VehicleCameraYawOffset = 0.0f;
    VehicleCameraTimeSinceLookInput = VehicleCameraReturnDelay;
    bVehicleCameraLookInputThisFrame = false;
    LastVehicleThrottleInput = 0.0f;
    LastVehicleSteeringInput = 0.0f;
    UpdateVehicleCameraControlRotation();
    Super::BeginPlay();
}

void AWheeledVehiclePawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UpdateVehicleCameraReturn(DeltaTime);
    UpdateVehicleCameraControlRotation();
}

void AWheeledVehiclePawn::PostDuplicate()
{
    Super::PostDuplicate();
    RebindVehicleComponents();
}

void AWheeledVehiclePawn::OnPostLoad(FArchive& Ar)
{
    Super::OnPostLoad(Ar);
    RebindVehicleComponents();
}

float AWheeledVehiclePawn::GetVehicleCameraBaseYaw() const
{
    if (Mesh)
    {
        return Mesh->GetWorldRotation().Yaw;
    }
    if (const USceneComponent* Root = GetRootComponent())
    {
        return Root->GetWorldRotation().Yaw;
    }
    return GetActorRotation().Yaw;
}

void AWheeledVehiclePawn::UpdateVehicleCameraControlRotation()
{
    if (!bAutoInputVehicleCamera)
    {
        return;
    }

    VehicleCameraPitch = ClampVehicleCameraPitch(VehicleCameraPitch, VehicleMinCameraPitch, VehicleMaxCameraPitch);
    VehicleCameraYawOffset = ClampVehicleCameraYawOffset(VehicleCameraYawOffset, VehicleMaxCameraYawOffset);

    const float BaseYaw = GetVehicleCameraBaseYaw();

    FRotator CameraControl = GetControlRotation();
    CameraControl.Pitch = VehicleCameraPitch;
    CameraControl.Yaw = NormalizeVehicleCameraAngle(BaseYaw + VehicleCameraYawOffset);
    CameraControl.Roll = 0.0f;
    SetControlRotation(CameraControl);
}

void AWheeledVehiclePawn::UpdateVehicleCameraReturn(float DeltaTime)
{
    if (!bAutoInputVehicleCamera || !bAutoReturnVehicleCamera || DeltaTime <= 0.0f)
    {
        bVehicleCameraLookInputThisFrame = false;
        return;
    }

    if (bVehicleCameraLookInputThisFrame)
    {
        VehicleCameraTimeSinceLookInput = 0.0f;
        bVehicleCameraLookInputThisFrame = false;
        return;
    }

    VehicleCameraTimeSinceLookInput += DeltaTime;

    const bool bVehicleInputActive =
        std::abs(LastVehicleThrottleInput) > 0.01f ||
        std::abs(LastVehicleSteeringInput) > 0.01f;
    const bool bVehicleMoving =
        VehicleMovement && std::abs(VehicleMovement->GetForwardSpeed()) > VehicleCameraMovingReturnSpeedThreshold;

    const bool bReturnRequestedByVehicle = bVehicleInputActive || bVehicleMoving;
    if (!bReturnRequestedByVehicle || VehicleCameraTimeSinceLookInput < VehicleCameraReturnDelay)
    {
        return;
    }

    const float SpeedScale = VehicleCameraMovingReturnMultiplier;
    VehicleCameraYawOffset = ExponentialAngleInterpTo(VehicleCameraYawOffset, 0.0f, DeltaTime, VehicleCameraYawReturnSpeed * SpeedScale);
    VehicleCameraPitch = ExponentialInterpTo(VehicleCameraPitch, VehicleDefaultCameraPitch, DeltaTime, VehicleCameraPitchReturnSpeed * SpeedScale);

    if (std::abs(VehicleCameraYawOffset) < 0.01f)
    {
        VehicleCameraYawOffset = 0.0f;
    }
    if (std::abs(VehicleCameraPitch - VehicleDefaultCameraPitch) < 0.01f)
    {
        VehicleCameraPitch = VehicleDefaultCameraPitch;
    }
}

void AWheeledVehiclePawn::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (!InputComponent)
    {
        return;
    }

    if (bAutoInputVehicle && VehicleMovement)
    {
        InputComponent->AddAxisMapping("VehicleThrottle", "W",  1.0f);
        InputComponent->AddAxisMapping("VehicleThrottle", "S", -1.0f);
        InputComponent->AddAxisMapping("VehicleSteering", "D",  1.0f);
        InputComponent->AddAxisMapping("VehicleSteering", "A", -1.0f);
        InputComponent->AddActionMapping("VehicleHandbrake", "Space");

        InputComponent->BindAxis("VehicleThrottle", [this](float Value)
        {
            if (!VehicleMovement)
            {
                return;
            }

            LastVehicleThrottleInput = Value;
            VehicleMovement->SetThrottleInput(Value);
            VehicleMovement->SetBrakeInput(0.0f);
        });

        InputComponent->BindAxis("VehicleSteering", [this](float Value)
        {
            LastVehicleSteeringInput = Value;
            if (VehicleMovement)
            {
                VehicleMovement->SetSteeringInput(Value);
            }
        });

        InputComponent->BindAction("VehicleHandbrake", EInputEvent::Pressed, [this]()
        {
            if (VehicleMovement)
            {
                VehicleMovement->SetHandbrakeInput(1.0f);
            }
        });

        InputComponent->BindAction("VehicleHandbrake", EInputEvent::Released, [this]()
        {
            if (VehicleMovement)
            {
                VehicleMovement->SetHandbrakeInput(0.0f);
            }
        });
    }

    if (bAutoInputVehicleCamera && bEnableVehicleMouseLook)
    {
        InputComponent->AddMouseAxisMapping("VehicleCameraTurn", EInputAxisSourceType::MouseX, VehicleMouseSensitivity);
        InputComponent->AddMouseAxisMapping("VehicleCameraLookUp", EInputAxisSourceType::MouseY, VehicleMouseSensitivity);

        InputComponent->BindAxis("VehicleCameraTurn", [this](float Value)
        {
            if (std::abs(Value) <= 0.0001f)
            {
                return;
            }

            VehicleCameraTimeSinceLookInput = 0.0f;
            bVehicleCameraLookInputThisFrame = true;
            VehicleCameraYawOffset = ClampVehicleCameraYawOffset(VehicleCameraYawOffset + Value, VehicleMaxCameraYawOffset);
            UpdateVehicleCameraControlRotation();
        });

        InputComponent->BindAxis("VehicleCameraLookUp", [this](float Value)
        {
            if (std::abs(Value) <= 0.0001f)
            {
                return;
            }

            VehicleCameraTimeSinceLookInput = 0.0f;
            bVehicleCameraLookInputThisFrame = true;
            const float Direction = bInvertVehicleMouseY ? -1.0f : 1.0f;
            VehicleCameraPitch = ClampVehicleCameraPitch(VehicleCameraPitch + Value * Direction, VehicleMinCameraPitch, VehicleMaxCameraPitch);
            UpdateVehicleCameraControlRotation();
        });
    }
}
