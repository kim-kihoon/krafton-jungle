#include "pch.h"
#include "SceneComponent.h"
#include "Input.h"

IMPLEMENT_CLASS(USceneComponent, UActorComponent)

void USceneComponent::UpdateTransform()
{

	FMatrix TranslationMatrix = FMatrix::MakeTranslation(Position);
	FMatrix RotationMatrix = FMatrix::MakeRotation(Rotation);
	FMatrix ScaleMatrix = FMatrix::MakeScale(Scale);
	// TODO: Lazy Update 고려
	ComponentToWorld = ScaleMatrix * RotationMatrix * TranslationMatrix;
}

FVector USceneComponent::GetComponentLocation() const
{
	return FVector(ComponentToWorld.M[3][0], ComponentToWorld.M[3][1], ComponentToWorld.M[3][2]);
}

FVector USceneComponent::GetRightVector() const
{
	FVector Right = { ComponentToWorld.M[0][0], ComponentToWorld.M[0][1], ComponentToWorld.M[0][2] };
	Right.Normalize();
	return Right;
}

FVector USceneComponent::GetUpVector() const
{
	FVector Up = { ComponentToWorld.M[1][0], ComponentToWorld.M[1][1], ComponentToWorld.M[1][2] };
	Up.Normalize();
	return Up;
}

FVector USceneComponent::GetForwardVector() const
{
	FVector Forward = { ComponentToWorld.M[2][0], ComponentToWorld.M[2][1], ComponentToWorld.M[2][2] };
	Forward.Normalize();
	return Forward;
}