#pragma once
#include "SceneComponent.h"

class USpringArmComponent : public USceneComponent
{
public:
    DECLARE_CLASS(USpringArmComponent, USceneComponent)

	float TargetArmLength = 300.f;
	FVector SocketOffset = {};

    float CameraLagSpeed = 10.f;
    bool  bEnableCameraLag = true;

    bool  bDoCollisionTest = true;
    float ProbeRadius = 12.f;

    bool  bEnableCameraRotationLag = false;
    float CameraRotationLagSpeed = 10.f;

    bool  bEnableZoom  = false;
    float ZoomStep     = 200.f;
    float MinArmLength = 0.f;
    float MaxArmLength = 100.f;

public:
    void TickComponent(float DeltaTime) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

    FVector GetTargetLocation() const { return CurrentArmEndpoint; }
    float   GetArmLength()      const { return TargetArmLength; }

private:
    FVector CurrentArmEndpoint = {};
    FQuat CurrentArmRotation = {};
};
