#pragma once

#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Core/Types/CoreTypes.h"

#include "Source/Engine/Physics/BodySetup/AggregateGeom.generated.h"

// UE: Engine/Classes/PhysicsEngine/AggregateGeom.h (simple collision subset)
USTRUCT()
struct FKBoxElem
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Center")
	FVector Center = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Rotation")
	FRotator Rotation = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="X")
	float X = 0.1f;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Y")
	float Y = 0.1f;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Z")
	float Z = 0.1f;
};

USTRUCT()
struct FKSphereElem
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Center")
	FVector Center = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Radius")
	float Radius = 0.2f;
};

USTRUCT()
struct FKSphylElem
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Center")
	FVector Center = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Rotation")
	FRotator Rotation = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Radius")
	float Radius = 0.2f;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Length")
	float Length = 0.5f;
};

USTRUCT()
struct FKAggregateGeom
{
	GENERATED_BODY()

	UPROPERTY(Save, Category="Collision")
	TArray<FKSphereElem> SphereElems;

	UPROPERTY(Save, Category="Collision")
	TArray<FKBoxElem> BoxElems;

	UPROPERTY(Save, Category="Collision")
	TArray<FKSphylElem> SphylElems;

	bool IsEmpty() const
	{
		return BoxElems.empty() && SphereElems.empty() && SphylElems.empty();
	}
};
