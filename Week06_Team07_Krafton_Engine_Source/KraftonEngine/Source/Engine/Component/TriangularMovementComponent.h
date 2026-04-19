#pragma once

#include "Component/MovementComponent.h"
#include "Core/CollisionTypes.h"
#include "Core/CoreTypes.h"
#include "Math/Vector.h"

class UTriangularMovementComponent : public UMovementComponent
{
public:
	DECLARE_CLASS(UTriangularMovementComponent, UMovementComponent)

	UTriangularMovementComponent() = default;
	~UTriangularMovementComponent() override = default;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void BeginPlay() override;

private:
	float RunningTime = 0.f;
	float Radius = 0.f;
	float BaseZ = 0.f;
	float BounceHeight = 1.f;
};