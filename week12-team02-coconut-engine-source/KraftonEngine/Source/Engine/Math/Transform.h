#pragma once
#include "Engine/Math/Matrix.h"
#include "Engine/Math/Rotator.h"
#include "Engine/Math/Quat.h"
#include "Core/Property/PropertyTypes.h"
#include "Object/ObjectMacros.h"
#include "Transform.generated.h"

USTRUCT()
struct FTransform
{
	GENERATED_BODY(FTransform)

	UPROPERTY(Edit, Category="Transform")
	FVector Location;

	FQuat Rotation;   // Edited via USceneComponent::CachedEditRotator — FQuat has no editor type.

	UPROPERTY(Edit, Category="Transform")
	FVector Scale;

	FTransform() : Location(0.0f, 0.0f, 0.0f), Rotation(FQuat::Identity), Scale(1.0f, 1.0f, 1.0f){}
	FTransform(const FVector& NewLocation, const FQuat& NewRotation, const FVector& NewScale)
		: Location(NewLocation), Rotation(NewRotation), Scale(NewScale) {}

	// FRotator 호환
	FTransform(const FVector& NewLocation, const FRotator& NewRotation, const FVector& NewScale)
		: Location(NewLocation), Rotation(NewRotation.ToQuaternion()), Scale(NewScale) {}

	// FVector 오일러 호환 (X=Roll, Y=Pitch, Z=Yaw)
	FTransform(const FVector& NewLocation, const FVector& EulerRotation, const FVector& NewScale)
		: Location(NewLocation), Rotation(FRotator(EulerRotation).ToQuaternion()), Scale(NewScale) {}

	FTransform(const FMatrix& Mat);

	FRotator GetRotator() const { return Rotation.ToRotator(); }
	void SetRotation(const FRotator& Rot) { Rotation = Rot.ToQuaternion(); }
	void SetRotation(const FQuat& Quat) { Rotation = Quat; }

	FMatrix ToMatrix() const;
};