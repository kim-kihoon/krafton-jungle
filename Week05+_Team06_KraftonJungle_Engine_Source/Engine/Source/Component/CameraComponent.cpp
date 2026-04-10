#include "CameraComponent.h"
#include "Object/Class.h"
#include "Camera/Camera.h"
#include "Math/MathUtility.h"
#include "Math/Transform.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UCameraComponent, USceneComponent)

void UCameraComponent::PostConstruct()
{
	bCanEverTick = true;
	Camera = new FCamera();
}

void UCameraComponent::OnRegister()
{
	USceneComponent::OnRegister();
	ApplyComponentTransformToCamera();
}

UCameraComponent::UCameraComponent(const UCameraComponent& Other)
	: USceneComponent(Other)
{
	if (Other.Camera)
	{
		Camera = new FCamera(*Other.Camera);
	}
	else
	{
		Camera = new FCamera();
	}
}

UCameraComponent::~UCameraComponent()
{
	delete Camera;
	Camera = nullptr;
}

void UCameraComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);

	if (!Ar.IsSaving())
	{
		ApplyComponentTransformToCamera();
	}
}

void UCameraComponent::Tick(float DeltaTime)
{
	USceneComponent::Tick(DeltaTime);

	//TODO : will be add CameraArm, shake and interpolation  
}

void UCameraComponent::MoveForward(float Value)
{
	Camera->MoveForward(Value);
	SyncComponentTransformFromCamera();
}

void UCameraComponent::MoveRight(float Value)
{
	Camera->MoveRight(Value);
	SyncComponentTransformFromCamera();

}

void UCameraComponent::MoveUp(float Value)
{
	Camera->MoveUp(Value);
	SyncComponentTransformFromCamera();

}

void UCameraComponent::Rotate(float DeltaYaw, float DeltaPitch)
{
	Camera->Rotate(DeltaYaw, DeltaPitch);
	SyncComponentTransformFromCamera();
}

FCamera* UCameraComponent::GetCamera() const
{
	return Camera;
}

FMatrix UCameraComponent::GetViewMatrix() const
{
	return Camera->GetViewMatrix();
}

FMatrix UCameraComponent::GetProjectionMatrix() const
{
	return Camera->GetProjectionMatrix();
}

void UCameraComponent::SetFov(float inFov)
{
	Camera->SetFOV(inFov);
}

void UCameraComponent::SetSpeed(float Inspeed)
{
	Camera->SetSpeed(Inspeed);
}

void UCameraComponent::SetSensitivity(float InSetSensitivity)
{
	Camera->SetMouseSensitivity(InSetSensitivity);
}

void UCameraComponent::ApplyComponentTransformToCamera() const
{
	if (Camera == nullptr)
	{
		return;
	}

	const FTransform WorldTransform(GetWorldTransform());
	const FVector Forward = WorldTransform.GetUnitAxis(EAxis::X).GetSafeNormal();
	const float Yaw = FMath::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
	const float ClampedForwardZ = (Forward.Z < -1.0f) ? -1.0f : ((Forward.Z > 1.0f) ? 1.0f : Forward.Z);
	const float Pitch = FMath::RadiansToDegrees(std::asin(ClampedForwardZ));
	Camera->SetPosition(WorldTransform.GetTranslation());
	Camera->SetRotation(Yaw, Pitch);
}

void UCameraComponent::SyncComponentTransformFromCamera()
{
	if (Camera == nullptr)
	{
		return;
	}

	FTransform UpdatedTransform(FRotator(Camera->GetPitch(), Camera->GetYaw(), 0.0f), Camera->GetPosition(), GetRelativeTransform().GetScale3D());
	if (USceneComponent* AttachParent = GetAttachParent())
	{
		const FTransform ParentWorldTransform(AttachParent->GetWorldTransform());
		UpdatedTransform = UpdatedTransform * ParentWorldTransform.Inverse();
	}
	SetRelativeTransform(UpdatedTransform);
}
