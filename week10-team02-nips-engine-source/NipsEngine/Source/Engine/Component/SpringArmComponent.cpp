#include "SpringArmComponent.h"
#include "Core/CollisionTypes.h"
#include "ActorComponent.h"
#include "GameFrameWork/World.h"
#include "Engine/Input/InputSystem.h"

DEFINE_CLASS(USpringArmComponent, USceneComponent)
REGISTER_FACTORY(USpringArmComponent)

void USpringArmComponent::TickComponent(float DeltaTime)
{
    if (bEnableZoom)
    {
        const float Notches = InputSystem::Get().GetScrollNotches();
        if (Notches != 0.f)
        {
            TargetArmLength -= Notches * ZoomStep;
            TargetArmLength  = MathUtil::Clamp(TargetArmLength, MinArmLength, MaxArmLength);
            CurrentArmEndpoint = {};
        }
    }

    if (bEnableCameraRotationLag)
    {
        FQuat TargetRot = FQuat::MakeFromEuler(GetRelativeRotation());

        if (CurrentArmRotation.IsIdentity())
            CurrentArmRotation = TargetRot;

        CurrentArmRotation = FQuat::Slerp(
            CurrentArmRotation, TargetRot,
            MathUtil::Clamp(DeltaTime * CameraRotationLagSpeed, 0.f, 1.f));

        SetRelativeRotation(CurrentArmRotation.Euler());
    }

    FVector PivotWorldPos;
	if (GetParent())
	{
        PivotWorldPos = GetParent()->GetWorldTransform().TransformPosition(SocketOffset);
	}
	else
	{
        PivotWorldPos = SocketOffset;
	}

	FVector ForwardVector = GetForwardVector();
    FVector DesiredPos = PivotWorldPos - (ForwardVector * TargetArmLength);

    UWorld* World = GetOwner() ? GetOwner()->GetFocusedWorld() : nullptr;

    if (World && bDoCollisionTest)
    {
        FHitResult Hit;
        FRay Ray(PivotWorldPos, -ForwardVector);

        if (World->LineTraceSingle(Ray, TargetArmLength, Hit, GetOwner()))
        {
            FVector CandidatePos = Hit.Location + (ForwardVector * ProbeRadius);

            float ClampedLen = FVector::DotProduct(PivotWorldPos - CandidatePos, ForwardVector);

            ClampedLen = MathUtil::Clamp(ClampedLen, MinArmLength, TargetArmLength);

            DesiredPos = PivotWorldPos - (ForwardVector * ClampedLen);
        }
    }

    if (bEnableCameraLag)
    {
        if (CurrentArmEndpoint.IsZero())CurrentArmEndpoint = DesiredPos;

        CurrentArmEndpoint = FVector::Lerp(
            CurrentArmEndpoint, DesiredPos,
            MathUtil::Clamp(DeltaTime * CameraLagSpeed, 0.f, 1.f));
    }
    else
    {
        CurrentArmEndpoint = DesiredPos;
    }

    SetWorldLocation(CurrentArmEndpoint);
}

void USpringArmComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    USceneComponent::GetEditableProperties(OutProps);

    OutProps.push_back({ "TargetArmLength", EPropertyType::Float, &TargetArmLength });
    OutProps.push_back({ "SocketOffset", EPropertyType::Vec3, &SocketOffset });

    OutProps.push_back({ "Do Collision Test",   EPropertyType::Bool,  &bDoCollisionTest });
    OutProps.push_back({ "Probe Radius",         EPropertyType::Float, &ProbeRadius      });

    OutProps.push_back({ "Enable Location Lag",  EPropertyType::Bool,  &bEnableCameraLag  });
    OutProps.push_back({ "Location Lag Speed",   EPropertyType::Float, &CameraLagSpeed    });

    OutProps.push_back({ "Enable Rotation Lag",  EPropertyType::Bool,  &bEnableCameraRotationLag  });
    OutProps.push_back({ "Rotation Lag Speed",   EPropertyType::Float, &CameraRotationLagSpeed    });

    OutProps.push_back({ "Enable Zoom",    EPropertyType::Bool,  &bEnableZoom   });
    OutProps.push_back({ "Zoom Step",      EPropertyType::Float, &ZoomStep      });
    OutProps.push_back({ "Min Arm Length", EPropertyType::Float, &MinArmLength  });
    OutProps.push_back({ "Max Arm Length", EPropertyType::Float, &MaxArmLength  });
}

void USpringArmComponent::PostEditProperty(const char* PropertyName)
{
    USceneComponent::PostEditProperty(PropertyName);

    CameraLagSpeed = std::max(0.0f, CameraLagSpeed);
    CameraRotationLagSpeed = std::max(0.0f, CameraRotationLagSpeed);
    ProbeRadius = std::max(0.0f, ProbeRadius);
    ZoomStep = std::max(0.0f, ZoomStep);
    MinArmLength = std::max(1.0f, MinArmLength);
    MaxArmLength = std::max(MinArmLength, MaxArmLength);

    TargetArmLength = MathUtil::Clamp(TargetArmLength, MinArmLength, MaxArmLength);
}

void USpringArmComponent::Serialize(FArchive& Ar)
{
    USceneComponent::Serialize(Ar);

    Ar << "TargetArmLength"        << TargetArmLength;
    Ar << "SocketOffset"           << SocketOffset;

    Ar << "DoCollisionTest"        << bDoCollisionTest;
    Ar << "ProbeRadius"            << ProbeRadius;

    Ar << "EnableCameraLag"        << bEnableCameraLag;
    Ar << "CameraLagSpeed"         << CameraLagSpeed;

    Ar << "EnableCameraRotLag"     << bEnableCameraRotationLag;
    Ar << "CameraRotationLagSpeed" << CameraRotationLagSpeed;
    
	Ar << "EnableZoom"             << bEnableZoom;
    Ar << "ZoomStep"               << ZoomStep;
    Ar << "MinArmLength"           << MinArmLength;
    Ar << "MaxArmLength"           << MaxArmLength;

    if (Ar.IsLoading())
    {
        if (MinArmLength <= 0.f) MinArmLength  = 0.1f;
        if (MaxArmLength <= MinArmLength) MaxArmLength = 100.0f;
        if (TargetArmLength <= 0.f) TargetArmLength = 300.f;
        TargetArmLength = MathUtil::Clamp(TargetArmLength, MinArmLength, MaxArmLength);
    }
}