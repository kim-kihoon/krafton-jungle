#pragma once

#include "GameFramework/Pawn/Pawn.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Pawn/WheeledVehiclePawn.generated.h"

class USkeletalMeshComponent;
class UWheeledVehicleMovementComponent;
class UVehicleWheelPoseComponent;
class USpringArmComponent;
class UCameraComponent;

// Possess 가능한 표준 차량 Pawn.
//
// 차량은 더 이상 임의 Actor + VehicleMovementComponent 조합에 의존하지 않는다. 기본 구성은
// SkeletalMesh(root) + WheeledVehicleMovement + WheelPose + SpringArm/Camera 이며, 입력은
// APawn::InputComponent를 통해 VehicleMovement로 들어간다.
UCLASS()
class AWheeledVehiclePawn : public APawn
{
public:
    GENERATED_BODY()
    AWheeledVehiclePawn() = default;
    ~AWheeledVehiclePawn() override = default;

    virtual void InitDefaultComponents(const FString& SkeletalMeshFileName);
    void BeginPlay() override;
    void Tick(float DeltaTime) override;
    void PostDuplicate() override;

protected:
    void OnPostLoad(FArchive& Ar) override;

public:

    UFUNCTION(Pure, Category="Vehicle|Components")
    USkeletalMeshComponent* GetMesh() const { return Mesh; }
    UFUNCTION(Pure, Category="Vehicle|Components")
    UWheeledVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }
    UFUNCTION(Pure, Category="Vehicle|Components")
    UVehicleWheelPoseComponent* GetWheelPoseComponent() const { return WheelPose; }
    UFUNCTION(Pure, Category="Vehicle|Components")
    USpringArmComponent* GetSpringArm() const { return SpringArm; }
    UFUNCTION(Pure, Category="Vehicle|Components")
    UCameraComponent* GetCamera() const { return Camera; }

protected:
    void SetupInputComponent() override;
    void RebindVehicleComponents();
    void UpdateVehicleCameraControlRotation();
    void UpdateVehicleCameraReturn(float DeltaTime);
    float GetVehicleCameraBaseYaw() const;

protected:
    TWeakObjectPtr<USkeletalMeshComponent> Mesh = nullptr;
    TWeakObjectPtr<UWheeledVehicleMovementComponent> VehicleMovement = nullptr;
    TWeakObjectPtr<UVehicleWheelPoseComponent> WheelPose = nullptr;
    TWeakObjectPtr<USpringArmComponent> SpringArm = nullptr;
    TWeakObjectPtr<UCameraComponent> Camera = nullptr;

    UPROPERTY(Edit, Save, Category="Vehicle|Input", DisplayName="Auto Vehicle Input")
    bool bAutoInputVehicle = true;

    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Auto Vehicle Camera Input")
    bool bAutoInputVehicleCamera = true;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Enable Mouse Look")
    bool bEnableVehicleMouseLook = true;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Mouse Sensitivity", Min=0.0f, Max=10.0f, Speed=0.01f)
    float VehicleMouseSensitivity = 0.10f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Default Camera Pitch", Min=-89.0f, Max=89.0f, Speed=0.1f)
    float VehicleDefaultCameraPitch = 10.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Min Camera Pitch", Min=-89.0f, Max=89.0f, Speed=0.1f)
    float VehicleMinCameraPitch = -5.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Max Camera Pitch", Min=-89.0f, Max=89.0f, Speed=0.1f)
    float VehicleMaxCameraPitch = 25.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Max Yaw Look Offset", Min=0.0f, Max=180.0f, Speed=1.0f)
    float VehicleMaxCameraYawOffset = 135.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Invert Mouse Y")
    bool bInvertVehicleMouseY = false;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Auto Return Camera")
    bool bAutoReturnVehicleCamera = true;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Camera Return Delay", Min=0.0f, Max=5.0f, Speed=0.01f)
    float VehicleCameraReturnDelay = 1.5f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Yaw Return Speed", Min=0.0f, Max=30.0f, Speed=0.1f)
    float VehicleCameraYawReturnSpeed = 1.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Pitch Return Speed", Min=0.0f, Max=30.0f, Speed=0.1f)
    float VehicleCameraPitchReturnSpeed = 4.0f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Moving Return Speed Multiplier", Min=1.0f, Max=10.0f, Speed=0.1f)
    float VehicleCameraMovingReturnMultiplier = 1.75f;
    UPROPERTY(Edit, Save, Category="Vehicle|Camera", DisplayName="Moving Return Speed Threshold", Min=0.0f, Max=100.0f, Speed=0.1f)
    float VehicleCameraMovingReturnSpeedThreshold = 0.5f;

    float VehicleCameraYawOffset = 0.0f;
    float VehicleCameraPitch = 10.0f;
    float VehicleCameraTimeSinceLookInput = 1000.0f;
    bool bVehicleCameraLookInputThisFrame = false;
    float LastVehicleThrottleInput = 0.0f;
    float LastVehicleSteeringInput = 0.0f;
};
